#pragma once

#include "allocator.h"
#include "future.h"

namespace coro {

///Iterator to access generators
template<typename Src>
class generator_iterator {
public:


    using storage_type = std::invoke_result_t<Src>;

    using iterator_category = std::input_iterator_tag;
    using value_type = std::add_const_t<typename std::decay_t<Src>::value_type>;
    using reference = std::add_lvalue_reference_t<value_type>;
    using difference_type = std::ptrdiff_t;
    using pointer = std::add_pointer_t<value_type>;

    reference operator*() {
        return _stor;
    }

    pointer operator->() {
        reference &r = _stor;
        return &r;
    }

    generator_iterator &operator++() {
        _stor = _src();
        _is_end = !_stor.has_value();
        return *this;
    }

    bool operator==(const generator_iterator &other) const {
        return _is_end == other._is_end;
    }

    static generator_iterator begin(Src src) {
        generator_iterator ret(src, false);
        ++ret;
        return ret;
    }

    static generator_iterator end(Src src) {
        return generator_iterator(src, true);
    }

protected:
    Src _src;
    bool _is_end;
    mutable storage_type _stor;

    generator_iterator(Src src, bool is_end):_src(src), _is_end(is_end) {

    }

};

///Generator
/**
 * Implements generator coroutine, supporting co_yield operation.
 *
 * @tparam T type of yielded value. This argument can be T or T & (reference).
 * In case that T is reference, the behaviour is same, however, the internal part
 * of the generator just carries a reference to the result which is always avaiable
 * as temporary value when coroutine is suspended on co_yield. This helps to
 * reduce necessery copying.
 *
 * @tparam Alloc A class which is responsible to allocate generator's frame. It must
 * implement coro_allocator concept
 *
 * The coroutine always returns void.
 *
 * The generator IS NOT MT SAFE! Only one thread can call the generator and one must
 * avoid to call the generator again before the result is available.
 *
 * The generator can be destroyed when it is suspended on co_yield.
 *
 * @note The object is movable and move assignable.
 */
template<typename T, CoroAllocator Alloc = StdAllocator>
class generator {
public:

    ///value type
    using value_type = T;
    ///reference type
    using reference = std::add_lvalue_reference_t<T>;

    ///generator's internal promise type
    class promise_type: public _details::coro_promise_base<T>,
                        public coro_allocator_helper<Alloc> {
    public:

        constexpr std::suspend_always initial_suspend() const {return {};}

        struct switch_awaiter {
            static constexpr bool await_ready() noexcept {return false;}
            static constexpr void await_resume() noexcept {}

            std::coroutine_handle<> await_suspend(std::coroutine_handle<promise_type> h) noexcept {
                promise_type &self = h.promise();
                return self.set_resolved();
            }
        };


        template<std::convertible_to<T> X>
        switch_awaiter yield_value(X && val) {
            this->set_value(std::forward<X>(val));
            return {};
        }

        switch_awaiter final_suspend() noexcept {
            return {};
        }

        void return_void() {
        }

        bool done() const {
            return std::coroutine_handle<const promise_type>::from_promise(*this).done();
        }

        generator get_return_object() {return this;}

        deferred_future<T> resume() {
            return [this](auto promise) -> std::coroutine_handle<> {
                if (done()) return {};
                this->fut = promise.release();
                return std::coroutine_handle<promise_type>::from_promise(*this);
            };
        }
    };

    deferred_future<T> operator()() {
        return _prom->resume();
    }


    ///Determines state of generator
    /**
     * @retval true, generator is generating
     * @retval false, generator is done
     *
     * @note if you call the generator, this state is not updated until you actually
     * access the returned (future) value.
     */
    explicit operator bool() const {
        return !_prom->done();
    }

    ///object can be default constructed
    generator() = default;

    template<typename A>
    generator(generator<T, A> &other): _prom(cast_promise(other._prom.release())) {

    }

    ///retrieve begin iterator
    /** You can use range-for to read values. This is always input iterator.
     * Note that access through the iterator is always synchronous. Current version
     * of C++ (20) doesn't support asynchronous range-for
     * @return begin iterator
     */
    auto begin() {
        return generator_iterator<generator &>::begin(*this);
    }
    ///retrieve end iterator
    /** You can use range-for to read values. This is always input iterator.
     * Note that access through the iterator is always synchronous. Current version
     * of C++ (20) doesn't support asynchronous range-for
     * @return end iterator
     */
    auto end() {
        return generator_iterator<generator &>::end(*this);
    }


protected:

    template<typename X>
    static promise_type *cast_promise(X *other) {
        return static_cast<promise_type *>(static_cast<_details::coro_promise_base<T> *>(other));
    }

    generator(promise_type *p):_prom(p) {}

    struct deleter {
        void operator()(promise_type *x) {
            auto h = std::coroutine_handle<promise_type>::from_promise(*x);
            h.destroy();
        }
    };

    std::unique_ptr<promise_type,deleter> _prom;

};



}
