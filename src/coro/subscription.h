#pragma once

#include "future.h"

namespace coro {

///Awaiter for subscriptions
/**
 * Represents subscription. You can subscribe a anything (represented as lambda function), and
 * if you need to monitor subscription, you need co_await on instance. If you need to
 * receive all values, you need to repeat co_await in the same thread - without any other
 * co_await in between (in case that you need asynchronous processing, you need to connect
 * subscription with a queue)
 *
 * @tparam T type of object, it can be reference or const reference
 * @tparam SubscribeFn subscription function. Default value also defines prototype of the
 * function
 *
 * Works similar as deferred_future, but it can be awaited repeatedly. Every co_await
 * performs subscription on the source. Synchronous access is possible, but has different API.
 * You need to use pointer access
 *
 * @note object is movable (unless it is locked)
 *
 * @ingroup Coroutines, awaitable
 *
 */
template<typename T, typename SubscribeFn = function<prepared_coro(promise<T>)> >
class subscription {
public:

    static_assert(std::is_constructible_v<function<prepared_coro(promise<T>)>, SubscribeFn>);

    using subscribe_fn = SubscribeFn;

    using canceled_awaiter = _details::has_value_awaiter<subscription,false>;

    ///default constructor
    subscription() = default;
    ///construct and pass subscribe function
    subscription(SubscribeFn &&fn): _fn(std::move(fn)) {}
    ///construct and pass subscribe function
    subscription(const SubscribeFn &fn): _fn(fn) {}
    ///movable
    subscription(subscription &&other):_fn(std::move(_fn)) {}
    ///movable
    subscription &operator=(subscription &&other) {
        if (this != &other) {
            _fn = std::move(other._fn);
        }
        return *this;
    }

    ///retrieve last value
    /**
     * In contrast to future, this function doesn't perform wait. Function can be used
     *  only in coroutine. If you need to retrieve value synchronously, use lock()
     *
     * @return current value
     */
    decltype(auto) get() {return _fut.await_resume();}

    ///co_await support
    /**
     * this awaiter is never ready
     * @return false
     */
    static constexpr bool await_ready() noexcept {return false;}
    ///co_await support Subscribe and wait for a value
    /**
     * @param h handle of current coroutine
     * @return handle of coroutine to switch
     */
    std::coroutine_handle<> await_suspend(std::coroutine_handle<> h) {
        return then([h]() ->prepared_coro {return h;}).symmetric_transfer();
    }
    ///returns current value
    /** use only in coroutine */
    decltype(auto) await_resume() {
        return _fut.await_resume();
    }

    ///wait and determine whether operation is canceled
    /**
     * awaitable - use co_await
     * @retval true canceled
     * @retval false has value
     *
     * You can use !! for negation of meaning
     */
    canceled_awaiter operator!() {return this;}

    ///Determine, whether has value
    bool has_value() const {return _fut.has_value();}


    ///define callback, which is called when value is ready
    /**
     * @param fn callback
     * @return handle to coroutine to switch, if there is such (can be ignored)
     */
    template<std::invocable Fn>
    prepared_coro then(Fn &&fn) {
        auto prom = _fut.get_promise();
        _fut.then(std::forward<Fn>(fn));
        return std::forward<SubscribeFn>(_fn)(std::move(prom));
    }

    ///define callback, which is called when value is ready
    /**
     * @param fn callback
     * @return handle to coroutine to switch, if there is such (can be ignored)
     */
    template<std::invocable Fn>
    void operator>>(Fn &&fn) {
        then(std::forward<Fn>(fn));
    }

    struct Unlocker {
        std::atomic<bool> *unlock = nullptr;
        void operator()(auto) {
            unlock->store(true);
            unlock->notify_all();
        }
    };

    using RetVal = std::decay_t<decltype(std::declval<future<T> >().await_resume())>;

    using LockedPtr = std::unique_ptr<RetVal, Unlocker>;

    ///subscribe and lock value during processing
    /**
     * @param ptr pointer instance (initially initialized to nullptr). The
     * function updates the pointer. It starts to point to the value. When pointer is
     * held, the publisher is blocked. You can process the value, and need to use
     * the pointer to subscribre for next value, which also unblock the publisher.
     * If you drop the pointer, the publisher is also unblocked
     */
    void lock(LockedPtr &ptr) {
        std::atomic<bool> ready = {false};
        std::atomic<bool> *unlock = nullptr;
        then([&]{
            std::atomic<bool> lk;
            unlock = &lk;
            ready.store(true);
            ready.notify_all();
            lk.wait(false);
        });
        ptr.reset();
        ready.wait(false);
        if (has_value()) {
            ptr = std::unique_ptr<RetVal, Unlocker>(&_fut.await_resume(), {unlock});
        } else {
            ptr = std::unique_ptr<RetVal, Unlocker>(nullptr, {unlock});
        }
    }

protected:
    future<T> _fut;
    SubscribeFn _fn;

    future<T> &start() {
        if (!_fut.is_pending()) _fn(_fut.get_promise());
        return _fut;
    }

    void wait_internal() {
        ///no wait possible
    }
    friend class _details::wait_awaiter<subscription>;

};


}
