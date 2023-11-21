#pragma once

#include "future.h"
#include "allocator.h"

#include <queue>
#include <utility>
namespace coro {



///helper struct which is part of coroutine promise
/** it contains different content for T = void */
template<typename T>
struct async_promise_base { // @suppress("Miss copy constructor or assignment operator")
    future<T> *fut = nullptr;
    template<typename ... Args>
    void return_value(Args &&... args) {
        if (fut) fut->set_value(std::forward<Args>(args)...);
    }
    static auto notify_targets(future<T> *f) {
        return f->notify_targets();
    }
    void unhandled_exception() {
        if (fut) fut->set_exception(std::current_exception());
    }
};


template<typename T>
struct async_promise_type<T, void>: async_promise_base<T> {
    using lazy_target = typename lazy_future<T>::promise_target_type;
    lazy_target _lazy_target;
    struct FinalSuspender {
        bool await_ready() const noexcept {
            return _fut == nullptr;
        }
        std::coroutine_handle<> await_suspend(std::coroutine_handle<> me)  noexcept {
            auto h = async_promise_base<T>::notify_targets(_fut);
            me.destroy();
            return h?h:std::noop_coroutine();
        }
        constexpr void await_resume() const noexcept {}
        future<T> *_fut;
    };

    constexpr std::suspend_always initial_suspend() const noexcept {return {};}
    FinalSuspender final_suspend() const noexcept {return {this->fut};}
    async<T> get_return_object() {return {this};}

    using promise = typename future<T>::promise;


};

///Coroutine future proxy
/**
 * The coroutine can use future<T> or Async<T>. While future<T> represents already
 * running coroutine (while the future is pending), Async<T> represents prepared
 * coroutine, which is already initialized, but it is suspended at the beginning
 *
 * When you declare future<T> coroutine, your coroutine is started immediatelly once
 * it is called. In contrast, by declaring Async<T>, you receive this object which
 * allows you to schedule the start of the coroutine. The coroutine is started once
 * it is converted to future<T>. The object Async<T> allows you to start coroutine detached
 * (so return value is thrown out)
 *
 * @tparam T type of return value
 */
template<typename T, coro_optional_allocator Alloc>
class [[nodiscard]] async {
public:

    ///You can declare an empty variable
    async() = default;
    ///You can move the Async object
    async(async &&other):_h(other._h) {other._h = {};}
    ///Allows to destroy suspended coroutine
    ~async() {if (_h) _h.destroy();}
    ///You can assign from one object to other
    async &operator=(async &&other) {
        if (this != &other) {
            if (_h) _h.destroy();
            _h = other._h;
            other._h = {};
        }
        return *this;
    }


    using promise_type = async_promise_type<T, Alloc>;


    ///Starts the coroutine
    /**
     * @return Returns future of this coroutine.
     * @note once the coroutine is started, this instance of Async<T> no longer refers
     * to the coroutine.
      */
    future<T> start() {
        return [&](auto promise){
            auto &p = _h.promise();
            p.fut = promise.release();
            auto h = std::exchange(_h, {});
            h.resume();
        };
    }

    ///Starts the coroutine in lazy mode
    /**
     * Coroutine started in lazy mode is stays suspended, until its value is needed
     *
     * @return Returns future of this coroutine.
     * @note once the coroutine is started, this instance of Async<T> no longer refers
     * to the coroutine.
     */
    lazy_future<T> lazy_start() {
        promise_type &p = _h.promise();
        target_simple_activation(p._lazy_target, [h = _h](typename lazy_future<T>::promise &&prom) {
            auto &coro = h.promise();
            if (prom) {
                coro.fut = prom.release();
                return h;
            } else {
                h.destroy();
                return std::coroutine_handle<promise_type>();
            }
        });
        _h = {};
        return p._lazy_target;
    }

    auto join() {
        return start().get();
    }

    void start(typename future<T>::promise &&promise) {
        auto &p = _h.promise();
        p.fut = promise.release();
        auto h = std::exchange(_h, {});
        h.resume();
    }


    ///Starts coroutine detached
    /**
     * @note any returned value is thrown out
     */
    void detach() {
        auto h = std::exchange(_h, {});
        h.resume();
    }

    ///Allows to convert Async<T> to future<T>
    operator future<T>() {
        return start();
    }

    operator lazy_future<T>() {
        return lazy_start();
    }

    explicit operator bool() const {return _h != nullptr;}

    class StartAwaiter {
        std::coroutine_handle<promise_type> _h;
        future<T> _storage;
        typename future<T>::target_type _target;
    public:

        StartAwaiter(std::coroutine_handle<promise_type> h):_h(h) {}
        bool await_ready() const {
            return _h == nullptr;
        }
        std::coroutine_handle<> await_suspend(std::coroutine_handle<> h) {
            auto &p = _h.promise();
            p.fut = _storage.get_promise().release();
            target_coroutine(_target, h);
            _storage.register_target(_target);
            return std::exchange(_h, nullptr);
        }
        decltype(auto) await_resume() {
            return _storage.get();
        }
    };


    ///Allows directly co_await to Async object
    /**
     * this is equivalent to co_await coroutine().start()
     * @return awaiter
     */
    StartAwaiter operator co_await() {
        return StartAwaiter(std::exchange(_h, nullptr));
    }


protected:

    friend struct async_promise_type<T, Alloc>;
    async(promise_type *ptr):_h(std::coroutine_handle<promise_type>::from_promise(*ptr)) {}


    std::coroutine_handle<promise_type> _h;
};


template<>
struct async_promise_base<void> {
    future<void> *fut = nullptr;
    void return_void() {
        if (fut) fut->set_value();
    }
    static auto notify_targets(future<void> *f) {
        return f->notify_targets();
    }
    void unhandled_exception() {
        if (fut) fut->set_exception(std::current_exception());
    }
};

template<typename T, coro_optional_allocator Alloc>
struct async_promise_type: async_promise_type<T, void>
                         , promise_type_alloc_support<Alloc> {



    async<T, Alloc> get_return_object() {return {this};}

};




}

