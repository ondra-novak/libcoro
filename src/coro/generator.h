#pragma once

#include "future.h"
#include "allocator.h"

namespace coro {

///Iterator to access generators
template<typename Src>
class generator_iterator {
public:


    using storage_type = std::invoke_result_t<Src>;

    using iterator_category = std::input_iterator_tag;
    using value_type = typename std::decay_t<Src>::value_type;
    using reference = std::add_lvalue_reference_t<value_type>;
    using difference_type = std::ptrdiff_t;
    using pointer = std::add_pointer_t<value_type>;

    reference operator*() const {
        return _stor;
    }

    pointer operator->() const {
        reference &r = _stor;
        return &r;
    }

    generator_iterator &operator++() {
        _stor = _src();
        _is_end = !_stor;
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
 * The generator can be destroyed when it is suspended on co_yield. This interrupts
 * generation similar as uncatchable exception.
 *
 * @note The object is movable and move assignable.
 */
template<typename T, coro_optional_allocator Alloc = void>
class generator;

template<typename T>
class generator<T, void> {
public:

    ///value type
    using value_type = T;
    ///reference type
    using reference = std::add_lvalue_reference_t<T>;

    ///generator's internal promise type
    struct promise_type {

        using yield_value_type = std::decay_t<T>;
        typename lazy_future<T>::promise _awaiter;
        std::exception_ptr _cur_exception;

        constexpr std::suspend_always initial_suspend() const {return {};}

        struct yield_suspender: std::suspend_always {
            yield_value_type & _value;
            yield_suspender(yield_value_type & v):_value(v) {}
            template<typename X>
            std::coroutine_handle<> await_suspend(std::coroutine_handle<X> me) {
                promise_type &p = me.promise();
                return p._awaiter(std::forward<value_type>(_value)).on_await_suspend();
            }
        };

        yield_suspender yield_value(yield_value_type & v) {
            return yield_suspender(v);
        }
        yield_suspender yield_value(yield_value_type && v) {
            return yield_suspender(v);
        }
        struct final_suspender: std::suspend_always {
            template<typename X>
            std::coroutine_handle<> await_suspend(std::coroutine_handle<X> me) noexcept {
                promise_type &p = me.promise();
                std::coroutine_handle<> h;
                if (p._cur_exception) {
                    h = p._awaiter.reject(p._cur_exception).on_await_suspend();
                } else {
                    h = p._awaiter.drop().on_await_suspend();
                }
                return h;
            }
        };

        final_suspender final_suspend() noexcept {return {};}

        void return_void() {}
        void unhandled_exception() {
            _cur_exception = std::current_exception();
        }

        bool is_done()  {
            return std::coroutine_handle<promise_type>::from_promise(*this).done();
        }

        generator get_return_object() {return {this};}

    };

    ///requests to generate a next value
    /**
     * If called for the first time, it starts the generator and the generator is suspended
     * on the first co_yield. Futher calls resumes the coroutine and continues to
     * next co_yield. The coroutine can anytime exit using co_return. If exception
     * is thrown during generation, the exception is captured in the returned future object
     *
     * @return lazy_future<T> object, you need to co_await or sync await to retrieve the
     * value. To detect generator's exit, you need to use has_value(), which returns
     * false in such case. Result of this function can be also co_awaited
     *
     * @note result of this call can be dropped without getting a value. In this case
     * no value is generated
     */
    lazy_future<T> operator()() {
        using lpromise = typename lazy_future<T>::promise;
        if (_prom->is_done()) return {};
        target_simple_activation(_next_target, [prom = _prom.get()](lpromise &&promise){
            auto h = std::coroutine_handle<promise_type>::from_promise(*prom);
            if (!h.done()) {
                if (!prom->_awaiter) {
                    prom->_awaiter = std::move(promise);
                    return h;
                }
            }
            return std::coroutine_handle<promise_type>();
        });
        return _next_target;
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
        return !_prom->is_done();
    }

    ///object can be default constructed
    generator() = default;

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

    generator(promise_type *p):_prom(p) {}

    struct deleter {
        void operator()(promise_type *x) {
            auto h = std::coroutine_handle<promise_type>::from_promise(*x);
            h.destroy();
        }
    };

    std::unique_ptr<promise_type,deleter> _prom;
    typename lazy_future<T>::promise_target_type _next_target;

};

template<typename T, coro_optional_allocator Alloc>
class generator : public generator<T, void>{
public:

    struct promise_type : generator<T, void>::promise_type
                        , promise_type_alloc_support<Alloc> {

        generator<T, Alloc> get_return_object() {return {this};}

    };

protected:
    generator(promise_type *p): generator<T, void>(p) {}

};


}
