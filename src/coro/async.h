#pragma once
#ifndef SRC_CORO_ASYNC_H_
#define SRC_CORO_ASYNC_H_

#include "common.h"
#include "allocator.h"
#include "future.h"

namespace coro {

template<typename T, CoroAllocator Alloc = StdAllocator>
class async {
public:

    class promise_type: public _details::coro_promise<T>, public coro_allocator_helper<Alloc> {
    public:

        struct final_awaiter {
            bool detached;
            bool await_ready() const noexcept {return detached;}
            std::coroutine_handle<>  await_suspend(std::coroutine_handle<promise_type> h) const noexcept {
                promise_type &self = h.promise();
                std::coroutine_handle<> retval = self.set_resolved();
                h.destroy();
                return retval;
            }

            void await_resume() const noexcept {}
        };

        std::suspend_always initial_suspend() const noexcept {return {};}
        final_awaiter final_suspend() const noexcept {return {this->fut == nullptr};}
        async get_return_object() {return {this};}

        void attach(promise<T> &prom) {this->fut = prom.release();}

    };

    async() = default;

    template<typename A>
    async(async<T, A> &&other):_promise_ptr(cast_promise(other._promise_ptr.release())) {}

    void detach() {
        auto p = _promise_ptr.release();
        auto h = std::coroutine_handle<promise_type>::from_promise(*p);
        h.resume();
    }

    future<T> start() {
        return [&](auto promise) {
            _promise_ptr->attach(promise);
            detach();
        };
    }

    void start(promise<T> prom) {
        _promise_ptr->attach(prom);
        detach();
    }

    deferred_future<T> defer_start() {
        return [me = std::move(*this)](auto promise) mutable {
            me._promise_ptr->attach(promise);
            me.detach();
        };
    }

    shared_future<T> shared_start() {
        return [me = std::move(*this)](auto promise) mutable {
            me._promise_ptr->attach(promise);
            me.detach();
        };
    }

    deferred_future<T> operator co_await() {
        return defer_start();
    }

    operator future<T>() {
        return start();
    }

    operator deferred_future<T>() {
        return defer_start();
    }

    operator shared_future<T>() {
        return shared_start();
    }

    operator auto() {
        return start().get();
    }

    auto run() {
        return start().get();
    }

protected:

    struct Deleter {
        void operator()(promise_type *p) {
            auto h = std::coroutine_handle<promise_type>::from_promise(*p);
            h.destroy();
        }
    };

    async(promise_type *p): _promise_ptr(p) {}

    std::unique_ptr<promise_type, Deleter> _promise_ptr;

    template<typename X>
    static promise_type *cast_promise(X *other) {
        return static_cast<promise_type *>(static_cast<_details::coro_promise<T> *>(other));
    }


};


}


template<typename T, typename ... Args>
struct std::coroutine_traits<coro::future<T>, Args...> {
    using promise_type = typename coro::async<T>::promise_type;

};

template<typename T, typename ... Args>
struct std::coroutine_traits<coro::deferred_future<T>, Args...> {
    using promise_type = typename coro::async<T>::promise_type;

};

template<typename T, typename ... Args>
struct std::coroutine_traits<coro::shared_future<T>, Args...> {
    using promise_type = typename coro::async<T>::promise_type;

};



#endif /* SRC_CORO_ASYNC_H_ */
