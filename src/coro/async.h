#pragma once

#include "common.h"
#include "allocator.h"
#include "future.h"

#include <cstdint>
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

        /**
         * The standard does not guarantee the call order of some operations.
         * For example, the conversion from async to future can happen
         * before initial_suspend (clang).This could cause a coroutine that
         * is not yet fully initialized to run early (because assigning a coroutine to a future calls resume() )
         *
         * To address this problem, a pointer to a bound future is used as a flag
         * for incomplete initialization.
         * If this flag does not have nullptr during the first run,
         * it means that the coroutine is not yet fully initialized.
         *
         * If the flag does not have this special value in initial_suspend,
         * then there is no initial suspend andthe coroutine is
         * started immediately
         *
         */
        static future<T> *invalid_value() {
            return reinterpret_cast<future<T> *>(std::uintptr_t(-1));
        }

        promise_type() {
            this->fut = invalid_value();
        }

        struct initial_awaiter {
            promise_type *me;
            bool await_ready() const noexcept {return me->fut != invalid_value();}
            void await_suspend(std::coroutine_handle<> h)  noexcept {
                //initialization is finished, reset the pointer
                me->fut = nullptr;
                LIBCORO_TRACE_ON_SWITCH(h, std::coroutine_handle<>());
            }
            static constexpr void await_resume() noexcept {};
        };

        struct final_awaiter {
            bool detached;
            bool await_ready() const noexcept {return detached;}
            #ifdef LIBCORO_MSC_FAILED_SYMMETRIC_TRANSFER
            void await_suspend(std::coroutine_handle<promise_type> h) const noexcept {
                promise_type &self = h.promise();
                std::coroutine_handle<> retval = self.set_resolved().symmetric_transfer();
                h.destroy();
                LIBCORO_TRACE_ON_RESUME(h);
                return retval.resume();
            }
            #else
            std::coroutine_handle<>  await_suspend(std::coroutine_handle<promise_type> h) const noexcept {
                promise_type &self = h.promise();
                std::coroutine_handle<> retval = self.set_resolved().symmetric_transfer();
                LIBCORO_TRACE_ON_SWITCH(h,retval);
                h.destroy();
                return retval;          //MSC RELEASE BUILD: Handle is passed by a register
            }
            #endif

            void await_resume() const noexcept {}
        };

        initial_awaiter initial_suspend() noexcept {return {this};}
        final_awaiter final_suspend() const noexcept {return {this->fut == nullptr};}
        async get_return_object() {return {this};}

        prepared_coro attach(promise<T> &prom) {
            if (std::exchange(this->fut, prom.release()) == nullptr) {
                return std::coroutine_handle<promise_type>::from_promise(*this);
            }
            return {};
        }
        prepared_coro detach() {
            if (std::exchange(this->fut, nullptr) == nullptr) {
                return std::coroutine_handle<promise_type>::from_promise(*this);
            }
            return {};
        }
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
        _promise_ptr.release()->detach();
    }

    ///detach coroutine using symmetric transfer
    /**
     * @return coroutine handle of coroutine to be resumed. Use
     * return value as result of await_suspend();
     */
    std::coroutine_handle<> detach_on_await_suspend() {
        return _promise_ptr.release()->detach().symmetric_transfer();
    }

    ///Start coroutine and return future
    future<T> start() {
        return future<T>([me = std::move(*this)](auto promise) mutable {
            me._promise_ptr.release()->attach(promise);
        });
    }

    ///Start coroutine and pass return value to promise
    void start(promise<T> prom) {
        _promise_ptr.release()->attach(prom);
    }

    ///Defer start of coroutine
    deferred_future<T> defer_start() {
        return [me = std::move(*this)](auto promise) mutable {
            return me._promise_ptr.release()->attach(promise);
        };
    }

    ///Start coroutine and return shared future
    shared_future<T> shared_start() {
        return [me = std::move(*this)](auto promise) mutable {
            me._promise_ptr.release()->attach(promise);
        };
    }

    ///direct @b co_await
    future<T> operator co_await() {
        return *this;
    }

    ///convert coroutine to future, start immediatelly
    operator future<T>() {
        return future<T>([me = std::move(*this)](auto promise) mutable {
            me._promise_ptr.release()->attach(promise);
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



