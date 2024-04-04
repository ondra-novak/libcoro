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
#ifdef LIBCORO_MSC_FAILED_SYMMETRIC_TRANSFER 

namespace _msc_details {

    //Helps to handle symmetric transfer while coroutine is already destroyed
    /* 
     * HACK
     *   
     * Reason: MSC has a bug, which prevents to use symmetric transfer when coroutine frame is destroyed inside of await_suspend of the final
     * suspend operation. The function must be finished while frame is still valid. However we need to destroy the frame before transfering to
     * next coroutine. So this class implements transfering coroutine, which handles this situation
     * 
     *
     * it is inifnite and empty generator. It is thread local allocated. You just need to call prepare_jump to set jump handle, and destroy handle.
     * Return value is handle to jump;
     * 
     * The generator's promise_type handles destruction of previous coroutine frame and jump to next coroutine. 
     *
     * There is actually double jump
     * 
     * (finished_coro) -> SymmTransGen -> (coroutine to resume)
     * 
     * while frame of finished coroutine is destroyed in process
     * 
     * NOTE: There is no promise that this is effective fast solution. It only garantee that
     * this solution performs true symmetric transfer (with no extra frame allocation).
     * Please ask Microsoft to fix this bug
     */
    class SymmTransGen {
    public:

        struct promise_type {
            std::suspend_always initial_suspend() {return {};}
            std::suspend_never final_suspend() noexcept {return {};}
            void return_void() {}
            void unhandled_exception() {}
            SymmTransGen get_return_object() {return this;}          

            std::coroutine_handle<> _next_handle = {};
            std::coroutine_handle<> _destroy_handle = {};

            struct yield_suspend: std::suspend_always {
                std::coroutine_handle<> await_suspend(std::coroutine_handle<promise_type> me) {
                    promise_type &self = me.promise();
                    self._destroy_handle.destroy();
                    return self._next_handle;
                }
            };

            yield_suspend yield_value(std::nullptr_t) {return {};}
        };

        /// @brief prepares symmetric transfer jump while destroying the frame of finished coroutine
        /// @param destroy_handle handle of coroutine to destroy
        /// @param jump_handle handle of coroutine to resume
        /// @return handle of coroutine to transfer from final_awaiter of finished coroutine
        std::coroutine_handle<> prepare_jump(std::coroutine_handle<> destroy_handle, std::coroutine_handle<> jump_handle) {
            _prom->_next_handle = jump_handle;
            _prom->_destroy_handle = destroy_handle;
            return std::coroutine_handle<promise_type>::from_promise(*_prom);
        }

    /// @brief every thread as own generator
    static thread_local SymmTransGen instance;

    protected:
        SymmTransGen(promise_type *prom):_prom(prom) {}

        struct Deleter {
            void operator()(promise_type *x) {
                return std::coroutine_handle<promise_type>::from_promise(*x).destroy();
            }
        };
        std::unique_ptr<promise_type,Deleter> _prom;

    };

    /// @brief only valid function for this generator
    /// @return instance of SymmTransGen
    inline SymmTransGen symmTransGen() {while (true) {co_yield nullptr;}}

    /// @brief initializes SymmTransGen instance
    inline  thread_local SymmTransGen SymmTransGen::instance = symmTransGen();
}

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
            std::coroutine_handle<>  await_suspend(std::coroutine_handle<promise_type> h) const noexcept {                
                promise_type &self = h.promise();
                std::coroutine_handle<> retval = self.set_resolved();                
                #ifdef LIBCORO_MSC_FAILED_SYMMETRIC_TRANSFER
                retval = _msc_details::SymmTransGen::instance.prepare_jump(h, retval);
                #else
                h.destroy();
                #endif
                return retval;
            }

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

    ///convert coroutine to future, prepare to start
    /**
     * This function directly converts to future, but doesn't starts it,
     * it actually returns future in deferred state. You need to co_await (or
     * wait on) future to start the coroutine. If you need to start coroutine
     * immediatelly, use start() function
     *
     */
    operator future<T>() {
        return future<T>(deferred, [me = std::move(*this)](auto promise) mutable {
            me._promise_ptr->attach(promise);
            return me.detach_on_await_suspend();
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



