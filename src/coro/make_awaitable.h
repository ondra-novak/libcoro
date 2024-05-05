#pragma once

#include "common.h"

namespace coro {

///Converts any result to awaitable object
/**
 * This can be useful, when used in template coroutines, when caller supplies own
 * function, which could be awaitable.
 *
 * To use this class, you need to construct it inside of co_await expression
 *
 * @code
 * auto result = co_await coro::make_awaitable([&]{return user_fn(args...);});
 * @endcode
 *
 * The object is constructed using the lambda expression, which must return whatever
 * the user function returns. If the return value is not awaitable, you can
 * still use co_await on the result.
 *
 * If the user function is awaitable, then awaiter just proxies all three
 * required methods to perform true co_await operation.
 *
 * @tparam Fn
 */
template<std::invocable<> Fn>
class make_awaitable: public std::suspend_never {
public:

    static constexpr bool is_async = false;

    make_awaitable(Fn &&fn):_fn(fn) {}
    make_awaitable(Fn &fn) : _fn(fn) {}

    auto await_resume() {
        return _fn();
    }
protected:
    Fn &_fn;
};


template<std::invocable<> Fn>
requires coro::directly_awaitable<std::invoke_result_t<Fn> >
class make_awaitable<Fn> {
public:

    static constexpr bool is_async = true;

    make_awaitable(Fn &&fn):_awaiter(fn()) {}
    make_awaitable(Fn &fn) :_awaiter(fn()) {}

    bool await_ready() {return _awaiter.await_ready();}
    auto await_suspend(std::coroutine_handle<> h) {return _awaiter.await_suspend(h);}
    decltype(auto) await_resume() {return _awaiter.await_resume();}

protected:
    std::invoke_result_t<Fn> _awaiter;
};

template<std::invocable<> Fn>
requires coro::indirectly_awaitable<std::invoke_result_t<Fn> >
class make_awaitable<Fn> {
public:

    static constexpr bool is_async = true;

    make_awaitable(Fn &&fn):_awaitable(fn()),_awaiter(_awaitable.operator co_await()) {}
    make_awaitable(Fn &fn) :_awaitable(fn()),_awaiter(_awaitable.operator co_await()) {}

    bool await_ready() {return _awaiter.await_ready();}
    auto await_suspend(std::coroutine_handle<> h) {return _awaiter.await_suspend(h);}
    decltype(auto) await_resume() {return _awaiter.await_resume();}

protected:
    std::invoke_result_t<Fn> _awaitable;
    decltype(std::declval<std::invoke_result_t<Fn> >().operator co_await()) _awaiter;
};

template<typename T, typename RetVal>
concept maybe_awaitable = awaitable_r<make_awaitable<coro::function<T()> >, RetVal>;


}
