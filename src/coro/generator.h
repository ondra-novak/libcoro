#pragma once

#include "allocator.h"
#include "future.h"

namespace coro {

///Iterator to access generators
/**
 * Note the iterator is only input iterator.
 *
 */
template<typename Src>
class generator_iterator {
public:


    using storage_type = std::invoke_result_t<Src>;

    using iterator_category = std::input_iterator_tag;
    using value_type = typename std::decay_t<Src>::value_type;
    using reference = future<value_type> &;
    using difference_type = std::ptrdiff_t;
    using pointer = std::add_pointer_t<value_type>;

    ///retrieve current value
    reference operator*() {
        return _stor;
    }


    ///advance next value
    generator_iterator &operator++() {
        _stor = _src();
        _stor.start();
        if (!_stor.is_pending() && !_stor.has_value()) {
            _is_end = true;
        }
        return *this;
    }

    ///you can only compare with end()
    bool operator==(const generator_iterator &other) const {
        return _is_end == other._is_end;
    }

    ///retrieve iterator to generator
    static generator_iterator begin(Src src) {
        generator_iterator ret(src, false);
        ++ret;
        return ret;
    }

    ///retrieve end iterator
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

///COROUTINE:  Generator
/**
 * Implements generator coroutine, supporting @b co_yield operation.
 *
 * @tparam T type of yielded value. This argument can be T or T & (reference).
 * In case that T is reference, the behaviour is same, however, the internal part
 * of the generator just carries a reference to the result which is always avaiable
 * as temporary value when coroutine is suspended on @b co_yield. This helps to
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
 * The generator can be destroyed when it is suspended on @b co_yield.
 *
 * @note The object is movable and move assignable.
 * @ingroup Coroutines, awaitable
 */
template<typename T, coro_allocator Alloc = std_allocator>
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
                return self.set_resolved_switch();
            }
        };

        /* WORKAROUND: clang 16+ fails here resulting in code crash during test - this is workaround
         *
         * CORO_OPT_BARRIER separates optimization between suspended coroutine and
         * normal code. For the clang it means to disable optimizations
         *
         * DETAILS: return value of set_resolved is prepared_coro which
         * must be initialized in current stack frame. However, the clang's
         * too aggresive optimization causes that this value is initialized
         * in coroutine frame at the same address as next awaiter, which
         * leads to overwritting its content on next (probably nested)
         * suspend event. The code crashes when it returns from suspenssion.
         *
         */

        CORO_OPT_BARRIER inline std::coroutine_handle<> set_resolved_switch() {
            return this->set_resolved().symmetric_transfer();
        }


        template<typename Arg>
        requires future_constructible<T, Arg>
        switch_awaiter yield_value(Arg && val) {
            this->set_value(std::forward<Arg>(val));
            return {};
        }
        switch_awaiter yield_value(std::exception_ptr e) {
            this->set_exception(std::move(e));
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

    ///call the generator
    /**
     * @return future value, note that generator is actually called once the
     * value is requested.
     */
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

    ///convert from different allocator
    template<typename A>
    generator(generator<T, A> &&other): _prom(cast_promise(other._prom.release())) {

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

    template<typename A, coro_allocator B> friend class generator;

    std::unique_ptr<promise_type,deleter> _prom;

};

///Specialization for R() - works similar as standard generator
template<typename R, coro_allocator Alloc>
class generator<R(), Alloc>: public generator<R, Alloc> {
public:
    using generator<R, Alloc>::generator;
};

class fetch_args_tag {};

inline constexpr fetch_args_tag fetch_args = {};

///Generator with arguments
/**
 * This generator can generate values depend on arguments passed to the function.
 * It is declared as specialization, which used function prototype instead of return
 * value. The function prototype can contain return value and arguments
 *
 * @tparam R return value
 * @tparam Alloc arguments
 * @tparam Args arguments
 *
 * To use this generator you simply call it with arguments. To write a coroutine, you
 * need to use `co_yield coro::fetch_args` to fetch arguments. They are returned
 * as a tuple (so you can use structural binding)
 *
 * @code
 * auto [arg1, arg2, arg3 ... ] = co_yield coro::fetch_args;
 * @endcode
 *
 * The return value is also returned over co_yield
 *
 * @code
 * co_yield result;
 * @endcode
 *
 * It is also possible to yield return value and fetch arguments in one command
 * @code
 * auto [arg1, arg2, arg3 ... ] = co_yield result;
 * @endcode
 *
 *
 */
template<typename R, coro_allocator Alloc, typename ... Args>
class generator<R(Args...), Alloc> {
public:

    using return_type = R;

    using arg_type = std::tuple<Args...>;

    ///generator's internal promise type
    class promise_type: public _details::coro_promise_base<R>,
                        public coro_allocator_helper<Alloc> {
    public:

        constexpr std::suspend_always initial_suspend() const {return {};}

        struct fetch_arg_awaiter {
            arg_type *arg;
            static constexpr bool await_ready() noexcept {return true;}
            static constexpr void await_suspend(std::coroutine_handle<>) noexcept {}
            arg_type &await_resume() noexcept {return *arg;};
        };

        struct switch_awaiter {
            promise_type *self = nullptr;
            static constexpr bool await_ready() noexcept {return false;}
            arg_type &await_resume() noexcept {return *self->arg;}

            CORO_OPT_BARRIER std::coroutine_handle<> await_suspend(std::coroutine_handle<promise_type> h) noexcept {
                self = &h.promise();
                return self->set_resolved().symmetric_transfer();
            }
        };

        template<typename Arg>
        requires future_constructible<R, Arg>
        switch_awaiter yield_value(Arg && val) {
            this->set_value(std::forward<Arg>(val));
            return {};
        }

        switch_awaiter yield_value(std::exception_ptr e) {
            this->set_exception(std::move(e));
            return {};
        }


        fetch_arg_awaiter yield_value(std::nullptr_t) {
            return {&(*arg)};
        }

        fetch_arg_awaiter yield_value(fetch_args_tag) {
            return {&(*arg)};
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

        future<R> resume() {
            return [this](auto promise)  {
                if (done()) return;
                this->fut = promise.release();
                std::coroutine_handle<promise_type>::from_promise(*this).resume();
            };
        }

        std::optional<arg_type> arg;

        template<typename ... XArgs>
        void set_arg(XArgs && ... args) {
            arg.emplace(std::forward<XArgs>(args)...);
        }
    };
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

    ///convert from different allocator
    template<typename A>
    generator(generator<R(Args...), A> &&other): _prom(cast_promise(other._prom.release())) {

    }

    ///call the generator
    /**
     * @param args arguments
     * @return future value, note that generator is actually called once the
     * value is requested.
     */
    template<typename ... XArgs>
    future<R> operator()(XArgs &&... args) {
        static_assert(std::is_constructible_v<arg_type, XArgs...>);
        _prom->set_arg(std::forward<XArgs>(args)...);
        return _prom->resume();
    }



protected:

    template<typename X>
    static promise_type *cast_promise(X *other) {
        return static_cast<promise_type *>(static_cast<_details::coro_promise_base<R> *>(other));
    }

    generator(promise_type *p):_prom(p) {}

    struct deleter {
        void operator()(promise_type *x) {
            auto h = std::coroutine_handle<promise_type>::from_promise(*x);
            h.destroy();
        }
    };

    template<typename A, coro_allocator B> friend class generator;

    std::unique_ptr<promise_type,deleter> _prom;



};



}
