#pragma once

#include "common.h"
#include "allocator.h"
#include "future.h"

namespace coro {

#ifdef _MSC_VER
#ifdef _DEBUG
///defined in MSC DEBUG configuration as the symmetric transfer doesn't work here (compiler bug)
#define LIBCORO_MSC_FAILED_SYMMETRIC_TRANSFER
#endif
#endif

///COROUTINE: Coroutine for asynchronous operation.
/**
 * This represents a coroutine which can return a value. The object
 * returned by the coroutine can be converted to future<T> or deferred_future<T>,
 * or can be directly co_awaited. You can also convert this object to T to
 * retrieve value synchronously
 *
 * @code
 * async<int> my_coro(...) {
 *  //...
 *  co_return 42;
 * }
 * @endcode
 *
 * Synchronous access
 * @code
 * int val = my_coro(...)
 * @endcode
 *
 * Retrieve future
 * @code
 * future<int> f = my_coro(...);
 * int val = co_await f;
 * @endcode
 *
 * Direct @b co_await
 * @code
 * int val = co_await my_coro(...);
 * @endcode
 *
 *
 * @tparam T type of returned value
 * @tparam Alloc optional allocator
 *
 * @note object is movable only
 * @ingroup Coroutines, awaitable
 */
template<typename T, coro_allocator Alloc = std_allocator>
class async {
public:


    class promise_type: public _details::coro_promise<T>, public coro_allocator_helper<Alloc> {
    public:

        struct final_awaiter {
            bool detached;
            bool await_ready() const noexcept {return detached;}
            #ifdef LIBCORO_MSC_FAILED_SYMMETRIC_TRANSFER
            void await_suspend(std::coroutine_handle<promise_type> h) const noexcept {
                promise_type &self = h.promise();
                std::coroutine_handle<> retval = self.set_resolved().symmetric_transfer();
                h.destroy();
                return retval.resume();
            }
            #else
            std::coroutine_handle<>  await_suspend(std::coroutine_handle<promise_type> h) const noexcept {
                promise_type &self = h.promise();
                std::coroutine_handle<> retval = self.set_resolved().symmetric_transfer();
                h.destroy();
                return retval;          //MSC RELEASE BUILD: Handle is passed by a register
            }
            #endif

            void await_resume() const noexcept {}
        };

        std::suspend_always initial_suspend() const noexcept {return {};}
        final_awaiter final_suspend() const noexcept {return {this->fut == nullptr};}
        async get_return_object() {return {this};}

        void attach(promise<T> &prom) {this->fut = prom.release();}

    };

    ///construct uninitialized object
    async() = default;

    ///convert from different allocator (because the allocator is only used during creation)
    template<typename A>
    async(async<T, A> &&other):_promise_ptr(cast_promise(other._promise_ptr.release())) {}


    ///Run coroutine detached
    /**
     * This runs coroutine without ability to retrieve return value. The
     * object is cleared after return (no longer controls the coroutine)
     */
    void detach() {
        auto p = _promise_ptr.release();
        auto h = std::coroutine_handle<promise_type>::from_promise(*p);
        h.resume();
    }

    ///detach coroutine using symmetric transfer
    /**
     * @return coroutine handle of coroutine to be resumed. Use
     * return value as result of await_suspend();
     */
    std::coroutine_handle<> detach_on_await_suspend() {
        auto p = _promise_ptr.release();
        return std::coroutine_handle<promise_type>::from_promise(*p);
    }

    ///Start coroutine and return future
    future<T> start() {
        return future<T>([me = std::move(*this)](auto promise) mutable {
            me._promise_ptr->attach(promise);
            me.detach();
        });
    }

    ///Start coroutine and pass return value to promise
    void start(promise<T> prom) {
        _promise_ptr->attach(prom);
        detach();
    }

    ///Defer start of coroutine
    deferred_future<T> defer_start() {
        return [me = std::move(*this)](auto promise) mutable {
            me._promise_ptr->attach(promise);
            return me.detach_on_await_suspend();
        };
    }

    ///Start coroutine and return shared future
    shared_future<T> shared_start() {
        return [me = std::move(*this)](auto promise) mutable {
            me._promise_ptr->attach(promise);
            me.detach();
        };
    }

    ///direct @b co_await
    future<T> operator co_await() {
        return *this;
    }

    ///convert coroutine to future, start immediatelly
    operator future<T>() {
        return future<T>([me = std::move(*this)](auto promise) mutable {
            me._promise_ptr->attach(promise);
            me.detach();
        });
    }

    ///convert to deferred_future
    operator deferred_future<T>() {
        return defer_start();
    }

    ///convert to shared_future
    operator shared_future<T>() {
        return shared_start();
    }


    ///synchronous wait for value
    template<std::constructible_from<T> U>
    operator U () {
        return U(start().get());
    }

    ///run synchronously
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



