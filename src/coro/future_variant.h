#pragma once
#include "future.h"
#include <variant>

namespace coro {

namespace _details {
    template<typename FutType, typename FutVariant>
    struct future_variant_getter {
        FutVariant *owner;
        using RetVal = decltype(get<FutType>(*owner).get());
        operator RetVal() {
            auto &ft = get<FutType>(*owner);
            return ft.get();
        }
    };

    template<typename FutVariant>
    struct future_variant_getter<void, FutVariant> {
        FutVariant *owner;
        template<typename T>
        operator T &&() {
            if (holds_alternative<deferred_future<T> >(*owner)) {
                return get<deferred_future<T> >(*owner).get();
            } else {
                return get<future<T> >(*owner).get();
            }
        }
    };


    class future_variant_interface {
    public:
        struct awaiter {
            void *ptr;
            std::coroutine_handle<> (*on_suspend)(void *ptr, std::coroutine_handle<> h);
            bool await_ready() const {return ptr == nullptr;}
            std::coroutine_handle<> await_suspend(std::coroutine_handle<> h) {
                return on_suspend(ptr, h);
            }
            void await_resume() noexcept {}
        };

        constexpr virtual ~future_variant_interface() = default;
        virtual void destroy_at(void *ptr) const = 0;
        virtual awaiter on_await(void *ptr) const = 0;
        virtual void on_wait(void *ptr) const = 0;
    };

    template<typename Fut>
    class future_variant_impl : public future_variant_interface{
    public:
        virtual void destroy_at(void *ptr) const override {
            std::destroy_at(reinterpret_cast<Fut *>(ptr));
        }
        virtual awaiter on_await(void *ptr) const override{
            return {ptr, [](void *ptr, std::coroutine_handle<> h){
                return reinterpret_cast<Fut *>(ptr)->await_suspend(h);
            }};
        }
        virtual void on_wait(void *ptr) const override{
            reinterpret_cast<Fut *>(ptr)->wait();
        }
    };

    template<>
    class future_variant_impl<std::nullptr_t> : public future_variant_interface{
    public:
        virtual void destroy_at(void *) const override{}
        virtual awaiter on_await(void *) const override{return {}; }
        virtual void on_wait(void *) const override{}
    };

    template<typename Fut>
    inline constexpr future_variant_impl<Fut> future_variant_control = {};
}

/// makes a variant future - multiple futures shares single space
/**
 * It is similar as variant over futures, but it is much easier to use
 *
 * @tparam Types list of types hosted by this object
 *
 * You can use operator << to redirect result to this future object. This operator
 * returns a reference to a selected future, which can be used to chain a callback
 *
 * @code
 * future_variant<int, double> fut;
 * fut << [&]{return async_double();} >> [&]{
 *      auto res = fut.get();
 * }
 * @endcode;
 *
 * You can use get_promise to initialize future and get promise at the same time
 *
 * @code
 * future_variant<int, double> fut;
 * promise<double> prom;
 * auto &f = fut.get_promise(prom);
 *  // f = future<double>;
 * @endcode
 *
 * @note you can switch future type only when underlying future is resolved
 */
template<typename ... Types>
class future_variant {
public:

    future_variant() = default;
    future_variant(const future_variant &) = delete;
    future_variant &operator=(const future_variant &) = delete;
    ~future_variant() {
        control->destroy_at(buffer);
    }

    /// Redirect return value to future object
    /**
     * @param fn invocable object without arguments, which returns a future or deferred_future,
     * The return value is constructed inside of future_variant instance. Any previous instance
     * is destroyed automatically.
     * @return reference to newly initialized future object in correct type
     */
    template<std::invocable<> Fn>
    auto &operator<<(Fn &&fn) {
        control->destroy_at(buffer);
        using fut_type = std::invoke_result_t<Fn>;
        static_assert(sizeof(fut_type) <= buffer_size, "Returned future is not expected");
        fut_type *ptr = new(buffer) fut_type(fn());
        control = &_details::future_variant_control<fut_type>;
        return *ptr;
    }

      ///Delete any underlying future and return object into uninitalized state
    void reset() {
        control->destroy_at(buffer);
        control = &_details::future_variant_control<std::nullptr_t>;
    }

    ///Initialize underlying future and retrieves promise
    /**
     * @param p reference to unbound promise object
     * @return reference to associated future after promise is bound to it
     */
    template<typename promise_type>
    auto &get_promise(promise_type &p) {
        using fut_type = typename promise_type::FutureType;
        static_assert(sizeof(fut_type) <= buffer_size, "Future type is not expected");
        fut_type *ptr = new(buffer) fut_type;
        p = ptr->get_promise();
        control = &_details::future_variant_control<fut_type>;
      return *ptr;
    }

    ///Retrieve reference to future - if it is current variant
    /**
     * @tparam fut_type type of current variant
     * @param me reference to variant_future
     * @return reference to variant
     */
    template<typename fut_type, typename ... X>
    friend fut_type &get(future_variant<X...> &me);

    ///Retrieve reference to future - if it is current variant
    /**
     * @tparam fut_type type of current variant
     * @param me reference to variant_future
     * @return reference to variant
     */
    template<typename fut_type, typename ... X>
    friend const fut_type &get(const future_variant<X...> &me);


    ///tests whether holds given variant
    /**
     *
     * @tparam fut_type type of current variant
     * @param me reference to variant_future
     * @retval true holds
     * @retval false something else
     */
    template<typename fut_type, typename ... X>
    friend bool holds_alternative(future_variant<X...> &me);

    template<typename FutType, typename FutVariant>
    friend struct future_variant_getter;

    ///Retrieve result of the future
    /**
     * @tparam fut_type (optional) specify variant of the future stored in the
     * instance. If this argument is omitted, it tries to detect the type from
     * requested value. However if there is not clear mapping (T -> future<T>) it
     * fails in runtime
     *
     * @return value of resolved future
     */
    template<typename fut_type = void>
    _details::future_variant_getter<fut_type, future_variant> get() {
        return {this};
    }

    using awaiter = _details::future_variant_interface::awaiter;

    ///Helps to co_wait on future regardless on which variant is active
    /**
     * @return awaiter.
     *
     * @note the awaiter doesn't return value. It only suspends coroutine if
     * the result of current variant is not available. You need to
     * use get() to retrieve value after wait
     */
    awaiter operator co_await() {
        return control->on_await(buffer);
    }

    ///Helps to wait on future regardless on which variant is active
    void wait() {
        return control->on_wait(buffer);
    }


protected:
    static constexpr auto buffer_size = std::max({sizeof(future<Types>)...});
    const _details::future_variant_interface *control = &_details::future_variant_control<std::nullptr_t>;
    char buffer[buffer_size];
};

template<typename fut_type, typename ... Types>
const fut_type &get(const future_variant<Types...> &me) {
    if (me.control != &_details::future_variant_control<fut_type>) {
        throw std::bad_variant_access();
    }
    return *reinterpret_cast<const fut_type *>(me.buffer);
}

template<typename fut_type, typename ... Types>
 fut_type &get( future_variant<Types...> &me) {
    if (me.control != &_details::future_variant_control<fut_type>) {
        throw std::bad_variant_access();
    }
    return *reinterpret_cast<fut_type *>(me.buffer);
}

template<typename fut_type, typename ... Types>
bool holds_alternative(future_variant<Types...> &me) {
    return (me.control == &_details::future_variant_control<fut_type>);
}


}
