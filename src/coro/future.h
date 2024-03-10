#pragma once
#ifndef SRC_CORO_FUTURE_H_
#define SRC_CORO_FUTURE_H_
#include "common.h"
#include "function.h"
#include "exceptions.h"

#include <atomic>
#include <memory>

namespace coro {

///Contains future value of T, can be co_awaited in coroutine
/**
 * @tparam T type which is subject of the future. T can be void. T can be also a reference
 */
template<typename T>
class future;
///Carries reference to future<T>, callable, sets value of an associated future<T>
/** promise object is movable and MT Safe */
template<typename T>
class promise;
///Contains future value of T, where evaluation is deferred until the value is needed
/** Deferred future is movable unless it is in progress (which results to exception).
 * This type of future is always returned from a function in deferred state, so
 * it can be moved and evaluated after the future is placed to correct place.
 *
 * Disadvantage of this future is requirement to store evaluation function
 * inside of the future (which must be movable) and guarantee to keep referneces
 * valid until deferred evaluation is started.
 *
 */
template<typename T>
class deferred_future;

namespace _details {
template<typename T> class coro_promise_base;
};


template<typename T>
class promise {
public:

    ///Associated future
    using FutureType = future<T>;


    ///contain notification to be delivered to the asociated future
    /**
     * By setting a value to a promise, it can cause that associated coroutine
     * will be resumed. The place, where this happens can be unsuitable for such
     * operation. This object can be used to schedule the notification about
     * resolution of the future. By default, the notification is delivered in the
     * destructor, however the object can be moved, or the notification can be delivered
     * manually. This object can be also used in situation special for coroutines,
     * such a function await_suspend() to support the symmetric transfer
     *
     */
    class notify {
    public:
        using FutureType = future<T>;

        notify() = default;

        ///cancel the future resolution (notify still needs to be delivered)
        void cancel() {_ptr->clearStorage();}
        ///deliver the notification now
        void deliver() {_ptr.reset();}
        ///deliver the notification with ability to switch to the coroutine
        /**
         * support for symmetric transfer
         *
         * @return return value must be used in await_suspend() - (otherwise the
         * notification can be lost). This allows to transfer from current coroutine
         * to resuming coroutine. If the awaiter is not coroutine, the function
         * simply delivers to notification and return noop_coroutine()
         */
        std::coroutine_handle<> symmetric_transfer() {
            auto ptr = _ptr.release();
            if (ptr) return ptr->resolve_symmetric_transfer();
            else return std::noop_coroutine();
        }
        ///Determines, whether object carries deferred notification
        explicit operator bool() const {return static_cast<bool>(_ptr);}

        void operator()() {
            _ptr.reset();
        }

    protected:

        ///Construct deferred notify from future
        notify(FutureType *fut):_ptr(fut) {}

        struct NotifyAction {
            void operator()(FutureType *fut) {fut->set_resolved();}
        };

        std::unique_ptr<FutureType, NotifyAction> _ptr;

        friend class promise<T>;
    };




    ///construct unbound promise
    promise() = default;

    ///Bound promise to future
    /**
     * @param ptr pointer to promise
     *
     * @note requires pending future, (not checked)
     */
    explicit promise(FutureType *ptr):_ptr(ptr) {}

    ///Move
    promise(promise &&other):_ptr(other.claim()) {}


    ///Assign by move
    promise &operator=(promise &&other) {
        if (this != &other) {
            cancel();
            _ptr.store(other.claim(), std::memory_order_relaxed);
        }
        return *this;
    }

    ///Dtor - if future is pending, cancels it
    ~promise() {
        cancel();
    }

    ///cancel the future (resolve without value)
    /**
     * @return deferred notification
     */
    notify cancel() {
        return claim();
    }

    ///set value
    /**
     * @param args arguments to construct value
     * @return deferred notification
     */
    template<typename ... Args>
    notify operator()(Args && ... args) {
        static_assert((std::is_void_v<T> && sizeof...(Args) == 0)
                        || std::is_constructible_v<T, Args ...>, "Value is not constructible from arguments");
        auto fut = claim();
        if (fut) fut->set_value(std::forward<Args>(args)...);
        return fut;
    }

    ///reject the future with exception
    /**
     * @param e exception
     * @return deferred notification
     */
    notify reject(std::exception_ptr e) {
        auto fut = claim();
        if (fut) fut->set_exception(std::move(e));
        return fut;
    }

    ///reject or cancel the future with exception
    /**
     * If called inside of cach block, it rejects with current notification, otherwise
     * it cancelles
     * @return deferred notification
     */
    notify reject() {
        auto e = std::current_exception();
        return e?reject(std::move(e)):cancel();
    }

    ///Reject with exception
    template<typename E>
    notify reject(E && exception) {
        return reject(std::make_exception_ptr<std::decay_t<E> >(std::forward<E>(exception)));
    }

    ///Release the future pointer from the promise object
    /** This can be useful,when you need to manage the pointer by yourself. To
     * resolve the future, you need to construct promise again with this pointer.
     * @return
     */
    FutureType *release() {
        return claim();
    }

protected:

    std::atomic<FutureType *> _ptr = {};

    FutureType *claim() {return _ptr.exchange(nullptr, std::memory_order_relaxed);}
};

struct deferred_tag {};

inline constexpr deferred_tag deferred=  {};

template<typename T>
class [[nodiscard]] future {
public:

    /*
     *   +-------------------+
     *   |   atomic(_state)  |  8
     *   +-------------------+
     *   |   enum(_result)   |  8
     *   +-------------------+
     *   |                   |
     *   |  * awaiter        |
     *   |  * deferred       |  32
     *   |                   |
     *   +-------------------+
     *   |                   |
     *   |  * exception      |
     *   |  * value          |  8+
     *   |                   |
     *   +-------------------+
     *
     *                          56+
     */


    using value_type = T;
    using promise_t = promise<T>;
    using value_store_type = std::conditional_t<std::is_reference_v<T>,std::add_pointer_t<std::decay_t<T> >,std::conditional_t<std::is_void_v<T>, bool, T> >;
    using cast_return = std::conditional_t<std::is_reference_v<T>, T, value_store_type>;
    using coro_handle = std::coroutine_handle<>;
    using awaiter_type = function<coro_handle(future &&)>;
    using deferred_eval_fn_type = function<coro_handle(promise_t)>;


    future() {/*can't be default*/}

    future(const future &) = delete;        //<can't copy nor move
    future &operator=(const future &) = delete; //<can't copy nor move

    ///Construct future already resolved with a value
    template<typename ... Args>
    requires std::constructible_from<value_store_type, Args ...>
    future(Args && ... args):_result(Result::value), _value(std::forward<Args>(args)...) {}


    ////Construct future already resolved with an exception
    future(std::exception_ptr e):_result(Result::exception), _exception(std::move(e)) {}

    ///Construct future which is evaluated inside of lambda function.
    /**
     * @param fn a lambda function which receives promise object
     */
    template<std::invocable<promise_t> Fn>
    future(Fn &&fn):_state(State::pending) {
        fn(promise_t(this));
    }

    ///Construct future with deferred evaluation
    /**
     * @param deferred tags this as deferred evaluatio
     * @param fn function called when evaluation is required. Note that function
     * is called outside of current context (don't use & in lambda)
     *
     * Evaluation can still be asynchronous
     */
    template<std::invocable<promise_t> Fn>
    future(deferred_tag, Fn &&fn) {
        setDeferredEvaluation(std::forward<Fn>(fn));
    }

    template<typename Fn>
    requires std::is_invocable_r_v<future, Fn>
    future(Fn &&fn) {
        new(this) future(fn());
    }

    ///dtor
    /**
     * @note the dtor calls terminate if future is destroyed while is in progress
     */
    ~future() {
        checkInProgress();
        if (_state.load(std::memory_order_relaxed) == State::deferred) {
            std::destroy_at(&_deferred);
        }
        clearStorage();
    }

    ///Store result future in already declared value
    /**
     * As the future cannot be copied or moved, only way to "store" result of
     * function to existing future variable is through this operator. Note
     * that operator accepts a function, which returns future of the exact same type
     *
     * @code
     * future<int> doSomething(args) {...}
     *
     *
     * future<int> fut;
     * //... other code ...
     * fut << [&]{return doSomething(args);};
     * int res = co_await fut;
     * @endcode
     *
     */
    template<typename Fn>
    requires std::is_invocable_r_v<future, Fn>
    future &operator << (Fn &&fn) {
        std::destroy_at(this);
        try {
            std::construct_at(this, std::forward<Fn>(fn));
        } catch (...) {
            std::construct_at(this);
        }
        return *this;
    }

    ///Retrieve promise and begin evaluation
    /**
     * The future must be either in resolved state or in deferred state. If the
     * future is in deferred state, deferred evaluation is canceled.
     *
     * Previusly stored value is cleared. The future is set to pending state
     *
     * @return promise
     */
    promise_t get_promise() {
        auto old = State::resolved;
        if (!_state.compare_exchange_strong(old, State::pending)) {
            if (old == State::deferred) {
                std::destroy_at(&_deferred);
                _state.store(State::pending);
            } else {
                throw still_pending_exception();
            }
        }
        clearStorage();
        return promise_t{this};
    }

    ///Sets callback which is called once future is resolved (future &&)
    /**
     * @param fn function to be called, the function must accept (future &&)
     * and can  optionally return std::coroutine_handle<>. In this case, the
     * returned handle can be used to switch execution to the coroutine. Another
     * result is ignored
     *
     * @retval true callback has been set
     * @retval false impossible, the future is already resolved. In this case, the
     * callback CAN BE DESTROYED if was passed as r-value. This can happen,
     * when future is resolved during copying the callback into the future object
     *
     * @note deferred future is evaluated before the callback is set. If the deferred
     * future is evaluated synchronously, the function returns false as the
     * future is already resolved
     *
     * @note there can be only one awaiter at time (callback or coroutine)
     *
     */
    template<std::invocable<future &&> Fn>
    bool set_callback(Fn &&fn) {
        using DFn = std::decay_t<Fn>;
        if constexpr(std::is_same_v<std::invoke_result_t<Fn, future &&>, coro_handle>) {
            return register_awaiter(std::forward<Fn>(fn), [](auto h){h.resume();});
        } else {
            return set_callback([fn = DFn(std::forward<Fn>(fn))](future &&f) -> coro_handle{
                fn(std::move(f));
                return {};
            });
        }
    }


    ///Sets callback which is called once future is resolved ()
    /**
     * @param fn function to be called, the function doesn't accept any arguments
     * and can optionally return std::coroutine_handle<>. In this case, the
     * returned handle can be used to switch execution to the coroutine. Another
     * result is ignored
     *
     * @retval true callback has been set
     * @retval false impossible, the future is already resolved. In this case, the
     * callback CAN BE DESTROYED if was passed as r-value. This can happen,
     * when future is resolved during copying the callback into the future object
     *
     * @note deferred future is evaluated before the callback is set. If the deferred
     * future is evaluated synchronously, the function returns false as the
     * future is already resolved
     *
     * @note there can be only one awaiter at time (callback or coroutine)
     */
    template<std::invocable<> Fn>
    bool set_callback(Fn &&fn) {
        using DFn = std::decay_t<Fn>;
        if constexpr(std::is_same_v<std::invoke_result_t<Fn>, coro_handle>) {
            return set_callback([fn = DFn(std::forward<Fn>(fn))](future &&) -> coro_handle{
                return fn();
            });
        } else {
            return set_callback([fn = DFn(std::forward<Fn>(fn))](future &&) -> coro_handle{
                fn();
                return {};
            });
        }
    }


    ///Perform synchronous wait on resolution
    /**
     * Blocks execution until future is resolved. If the future is deferred, the
     * evaluation is started now
     */
    void wait() {
        auto st = _state.load(std::memory_order_acquire);
        while (st != State::resolved) {
            if (st == State::deferred) {
                startDeferredEvaluation([](auto c){c.resume();});
            } else {
                _state.wait(st);
            }
            st = _state.load(std::memory_order_acquire);
        }
    }

    ///Retrieves value, performs synchronous wait
    T get() && {
        wait();
        if constexpr(std::is_void_v<T>) {
            getInternal();
        } else  {
            return std::forward<T>(getInternal());
        }
    }

    ///Retrieves value, performs synchronous wait
    T get() & {
        wait();
        if constexpr(std::is_void_v<T>) {
            getInternal();
        } else {
            return getInternal();
        }
    }

    ///Retrieves value, performs synchronous wait
    operator cast_return() && {
        wait();
        return std::forward<cast_return>(getInternal());
    }

    ///Retrieves value, performs synchronous wait
    operator cast_return() & {
        wait();
        return getInternal();
    }

    ///Determines pending status
    /**
     * @retval true future is pending, including deferred state
     * @retval false future is resolved
     */
    bool is_pending() const {
        return _state.load(std::memory_order_relaxed) != State::resolved;
    }

    ///Determine in progress status
    /**
     * @retval true future is being evaluated (evaluation is performed asynchronously)
     * @retval false future is dormant, it is either resolved or deferred
     */
    bool is_in_progress() const {
        auto st = _state.load(std::memory_order_relaxed);
        return st != State::resolved && st != State::deferred;
    }

    ///Determine deferred status
    /**
     * @retval true future is deferred
     * @retval false future is in other state
     */
    bool is_deferred() const {
        return _state.load(std::memory_order_relaxed) == State::deferred;
    }

    ///Determine whether an awaiter is set
    /** @retval true future has awaiter set
     *  @retval false there is no awaiter
     */
    bool is_awaited() const {
        return _state.load(std::memory_order_relaxed) == State::awaited;
    }

    ///co_await support, returns true, if value is ready (resolved)
    bool await_ready() const {
        return _state.load(std::memory_order_relaxed) == State::resolved;
    }

    ///co_await support, called with suspended coroutine
    /**
     * the function registers coroutine to be awaken once the future is resolved.
     * It is registered as an awaiter
     *
     * @param h handle of suspended coroutine waiting for result
     * @return handle of coroutine to wakeup. This can be for example deferred
     * evaluation if it is registered as a coroutine. The called must resume
     * the coroutine. function returns std::noop_coroutine if there is no
     * such coroutine
     */
    coro_handle await_suspend(coro_handle h) {
        coro_handle retval = {};
        bool st = register_awaiter([h](future &&){return h;},
                         [&retval](coro_handle h){retval = h;});
        if (st) return retval?retval:std::noop_coroutine();
        return h;
    }

    ////co_await support, called by resumed coroutine to retrieve a value
    T await_resume() && {
        if constexpr(std::is_void_v<T>) {
            getInternal();
        } else {
            return std::forward<T>(getInternal());
        }
    }

    ////co_await support, called by resumed coroutine to retrieve a value
    T await_resume() & {
        if constexpr(std::is_void_v<T>) {
            getInternal();
        } else {
            return getInternal();
        }
    }

    template<bool flag = true>
    class has_value_awaiter {
    public:
        has_value_awaiter(future &owner):_owner(owner) {}
        has_value_awaiter(has_value_awaiter &&) = default;
        has_value_awaiter &operator=(has_value_awaiter &&) = delete;

        bool await_ready() const {return _owner.await_ready(); }
        coro_handle await_suspend(coro_handle h) {return _owner.await_suspend(h);}
        bool await_resume() const {return (_owner._result != Result::not_set) == flag;}

        has_value_awaiter<!flag> operator!() const {return _owner;}
        operator bool() {
            _owner.wait();
            return await_resume();
        }

    protected:
        future &_owner;
    };

    ///Determines whether future has a value (was not canceled)
    /**
     * @return awaitable object. A coroutine must co_await on result to
     * determine the state. Non-coroutine can convert this value to boolean to
     * retrieve the state. In this case, synchronous waiting is performed.
     *  The return value also support operator ! to retrieve inverted value.
     *
     *  The function is intended to co_await on a future and detect canceled
     *  state without throwing an exception. To retrieve value later, the
     *  coroutine can safely use get() or await_resume()
     *
     *  @code
     *  future<int> fut = doSomething();
     *  if (co_await !fut.has_value()) {
     *      std::cerr << "Canceled" << std::endl;
     *  } else {
     *      int val = fut.get();
     *      std::cout << "Result:" << val << std::endl;
     *  }
     *
     *
     *  @retval true future has value
     *  @retval false future has been canceled
     *
     */
    has_value_awaiter<> has_value() {
        return *this;
    }




protected:

    enum class State {
        //future is resolved
        resolved,
        //evaluation is deferred
        deferred,
        //future is pending  - no awaiter
        pending,
        //future is pending - awaiter is set
        awaited,
        //future is pending - deferred evaluation is current in progress
        evaluating

    };

    enum class Result {
        //result is not set (yet?)
        not_set,
        //result contains value
        value,
        //result contains exception
        exception,
    };

    std::atomic<State> _state = {State::resolved}; //< by default, future is resolved in canceled state
    Result _result = Result::not_set; //< by default, future has no value

    //because deferred and awaited are mutually exclusive,
    //reuse single space for both functions
    union {
        awaiter_type _awaiter;
        deferred_eval_fn_type _deferred;
    };

    union {
        value_store_type _value;
        std::exception_ptr _exception;
    };

    void clearStorage() {
        switch (_result) {
            default: break;
            case Result::value: std::destroy_at(&_value);break;
            case Result::exception: std::destroy_at(&_exception);break;
        }
        _result = Result::not_set;
    }


    auto &getInternal() {
        switch (_result) {
            case Result::value:
                if constexpr(std::is_reference_v<T>) {
                    return *_value;
                } else {
                    return _value;
                }
            case Result::exception: std::rethrow_exception(_exception); break;
            default: break;
        }
        throw await_canceled_exception();
    }


    ///Starts evaluation of deferred future
    /**
     * @note DOESN'T CHECK REQUIRED STATE
     *
     * @param resume_fn function called to handle resumption of coroutine
     * @retval true evaluation continues asynchronously
     * @retval false evaulation is done, future is resolved
     */
    template<typename ResumeFn>
    bool startDeferredEvaluation(ResumeFn &&resume_fn) noexcept {
        State new_state = State::evaluating;
        _state.store(new_state, std::memory_order_acquire);
        auto coro = _deferred(promise_t(this));
        resume_fn(coro);
        std::destroy_at(&_deferred);
        return _state.compare_exchange_strong(new_state, State::pending, std::memory_order_release);

    }

    ///Initializes deferred evaluation
    /**
     * @param fn function which is called for deferred evaluation
     *
     * @note future must be in resolved state.
     */
    template<std::invocable<promise_t> Fn>
    void setDeferredEvaluation(Fn &&fn) {
        if constexpr(std::is_convertible_v<std::invoke_result_t<Fn, promise_t>, coro_handle>) {
            State old = State::resolved;
            if (!_state.compare_exchange_strong(old,State::deferred, std::memory_order_relaxed)) {
                throw still_pending_exception();
            }
            try {
                std::construct_at(&_deferred, std::forward<Fn>(fn));
                return;
            } catch (...) {
                _state.store(old, std::memory_order_relaxed);
                throw;
            }
        } else {
            setDeferredEvaluation([xfn = std::move(fn)](promise_t prom) mutable->coro_handle{
                xfn(std::move(prom));
                return {};
            });
        }
    }

    template<typename ... Args>
    void set_value(Args && ... args) {
        clearStorage();
        std::construct_at(&_value, std::forward<Args>(args)...);
        _result = Result::value;
    }

    void set_exception(std::exception_ptr e) {
        clearStorage();
        std::construct_at(&_exception, std::move(e));
        _result = Result::exception;
    }

    void set_resolved() {
        auto coro = resolve();
        if (coro) coro.resume();
    }

    coro_handle resolve_symmetric_transfer() {
        auto coro = resolve();
        if (coro) return coro;
        else return std::noop_coroutine();
    }

    coro_handle resolve() noexcept {
        State st = _state.exchange(State::resolved);
        _state.notify_all();
        if (st == State::awaited) {
            auto coro = _awaiter(std::move(*this));
            std::destroy_at(&_awaiter);
            return coro;
        }
        return {};
    }

    template<typename Awt, typename ResumeFn >
    bool register_awaiter(Awt &&awt, ResumeFn &&resumeFn) {
        State st = _state.exchange(State::evaluating);
        switch (st) {
            case State::resolved: _state.store(st);
                                  return false;
            case State::pending:
                std::construct_at(&_awaiter, std::forward<Awt>(awt));
                break;
            default:
            case State::evaluating:
                throw still_pending_exception();
            case State::awaited:
                std::destroy_at(&_awaiter);
                std::construct_at(&_awaiter, std::forward<Awt>(awt));
                break;
            case State::deferred:
                return startDeferredEvaluation(resumeFn)
                        && register_awaiter(std::forward<Awt>(awt),
                                            std::forward<ResumeFn>(resumeFn));
        }
        st = State::evaluating;
        if (!_state.compare_exchange_strong(st, State::awaited)) {
            std::destroy_at(&_awaiter);
            return false;
        }
        return true;
    }

    void checkInProgress() {
        if (is_in_progress()) throw still_pending_exception();
    }
    friend class _details::coro_promise_base<T>;
    friend class promise<T>;
};


template<typename T>
class deferred_future: public future<T> {
public:

    using promise_t = typename future<T>::promise_t;
    using State = typename future<T>::State;
    using Result = typename future<T>::Result;
    using future<T>::future;

    template<std::invocable<promise_t> Fn>
    deferred_future(Fn &&fn) {
        this->setDeferredEvaluation(std::forward<Fn>(fn));
    }
    deferred_future(deferred_future &&other) {
        other.checkInProgress();
        if (other._state == State::deferred) {
            std::construct_at(&this->_deferred, std::move(other._deferred));
            this->_state = State::deferred;
        }
        switch (other._result) {
            case Result::value: std::construct_at(&this->_value, std::move(other._value)); break;
            case Result::exception: std::construct_at(&this->_exception, std::move(other._exception)); break;
            default: break;
        }
        this->_result = other._result;
    }

    deferred_future &operator=(deferred_future &&other) {
        if (this != &other){
            this->checkInProgress();
            other.checkInProgress();
            std::destroy_at(this);
            std::construct_at(this, std::move(other));
        }
        return *this;
    }

    ///convert deferred future to future
    operator future<T>() {
        if (this->_state == State::deferred) {
            return [&](auto promise) {
                auto coro = this->_deferred(std::move(promise));
                if (coro) coro.resume();
                std::destroy_at(&this->_deferred);
                this->_state = State::resolved;
            };
        } else if (this->_state == State::resolved) {
            switch (this->_result) {
                case Result::value:return std::move(this->_value);
                case Result::exception: return std::move(this->_exception);
                default: return {};
            }
        } else {
            throw still_pending_exception();
        }
    }


};

namespace _details {

template<typename T>
class coro_promise_base {
protected:
    future<T> *fut = nullptr;

    std::coroutine_handle<> set_resolved() const {
        return fut->resolve_symmetric_transfer();
    }
    template<typename ... Args>
    void set_value(Args && ... args) const {
        if (fut) fut->set_value(std::forward<Args>(args)...);
    }
public:
    void unhandled_exception() {
        if (fut) fut->set_exception(std::current_exception());
    }

};


template<typename T>
class coro_promise: public coro_promise_base<T> {
public:
    template<std::convertible_to<T> Arg>
    void return_value(Arg &&arg) {
        this->set_value(std::forward<Arg>(arg));
    }

};

template<>
class coro_promise<void>: public coro_promise_base<void> {
public:
    void return_void() {
        this->set_value();
    }

};

}

///Future which can be shared (by copying - like shared_ptr)
/**
 * Each instance of the shared future can be awaited or can install a callback function.
 * So this allows to have multiple awaiters on signle future. The shared future can
 * be constructed by similar wait as future, you can retrieve promise<T> or you can
 * convert future<T> to shared_future<T> by passing a function call returning future<T>
 * to a constructor of shared_future<T>
 *
 * @code
 * future<int> foo(...);
 *
 * shared_future<int> fut( [&]{return foo(...);} );
 * @endcode
 *
 * @tparam T
 */
template<typename T>
class shared_future {
public:

    /*
     * +----------------+
     * |  shared_ptr    |---------------------> +----------------+
     * +----------------+                       |                |
     * | _next - l.list |                       |                |
     * +----------------+                       |    future<T>   |
     * | _state         |                       |                |
     * +----------------+                       |                |
     * |                |                       +----------------+
     * | _awaiter/cb    |                       | chain - l.list |
     * |                |                       +----------------+
     * +----------------+
     *
     *
     *
     */


    using value_type = typename future<T>::value_type;
    using value_store_type = typename future<T>::value_store_type;
    using awaiter_cb = function<std::coroutine_handle<>(future<T> &)>;

    shared_future() = default;
    template<typename ... Args>
    requires std::is_constructible_v<future<T>, Args...>
    shared_future(Args && ...args) {
        _shared_future = std::make_shared<Shared>(std::forward<Args>(args)...);
        if (_shared_future->is_pending()) _shared_future->init_callback(_shared_future);
    }

    shared_future(const shared_future &other):_shared_future(other._shared_future) {}
    shared_future &operator=(const shared_future &other) {
        this->_shared_future = other._shared_future;
        return *this;
    }

    ~shared_future() {
        check_in_progress();
    }

    promise<T> get_promise() {
        _shared_future = std::make_shared<Shared>();
        promise<T> p = _shared_future->get_promise();
        _shared_future->init_callback(_shared_future);
        return p;
    }

    template<std::invocable<future<T> &> Fn>
    bool set_callback(Fn &&fn) {
        using DFn = std::decay_t<Fn>;
        if constexpr(std::is_same_v<std::invoke_result_t<Fn, future<T> &>, std::coroutine_handle<> >) {
            auto st = _state.exchange(State::unused);
            if (st == State::notified) return false;
            if (st == State::awaited) {
                std::destroy_at(&_awaiter);
            }
            std::construct_at(&_awaiter, std::forward<Fn>(fn));
            auto st2 = State::unused;
            if (_state.compare_exchange_strong(st2, State::awaited)) {
                if (st == State::unused) return _shared_future->register_target(this);
                return true;
            }
            std::destroy_at(&_awaiter);
            return false;
        } else {
            return set_callback([fn = DFn(std::forward<Fn>(fn))](future<T> &f) -> std::coroutine_handle<> {
                fn(f);
                return {};
            });
        }
    }

    template<std::invocable<> Fn>
    bool set_callback(Fn &&fn) {
        using DFn = std::decay_t<Fn>;
        if constexpr(std::is_same_v<std::invoke_result_t<Fn>, std::coroutine_handle<> >) {
            return set_callback([fn = DFn(std::forward<Fn>(fn))](future<T> &) -> std::coroutine_handle<> {
                return fn();
            });
        } else {
            return set_callback([fn = DFn(std::forward<Fn>(fn))](future<T> &) -> std::coroutine_handle<> {
                fn();
                return {};
            });
        }
    }

    ///Perform synchronous wait on resolution
    /**
     * Blocks execution until future is resolved. If the future is deferred, the
     * evaluation is started now
     */
    void wait() {
        _shared_future->wait();
    }


    ///Retrieves value, performs synchronous wait
    T get() {
        wait();
        return _shared_future->get();
    }


    ///Retrieves value, performs synchronous wait
    operator value_store_type(){
        wait();
        return *_shared_future;
    }

    ///Determines pending status
    /**
     * @retval true future is pending, including deferred state
     * @retval false future is resolved
     */
    bool is_pending() const {
        return _shared_future && _shared_future->is_pending();
    }

    ///Determine in progress status
    /**
     * @retval true future is being evaluated (evaluation is performed asynchronously)
     * @retval false future is dormant, it is either resolved or deferred
     */
    bool is_in_progress() const {
        return _shared_future && _shared_future->is_in_progress();
    }


    ///Determine whether an awaiter is set
    /** @retval true future has awaiter set
     *  @retval false there is no awaiter
     */
    bool is_awaited() const {
        return _state.load(std::memory_order_relaxed) == State::awaited;
    }

    ///co_await support, returns true, if value is ready (resolved)
    bool await_ready() const {
        return !_shared_future || _shared_future->await_ready();
    }

    ///co_await support, called with suspended coroutine
    /**
     * the function registers coroutine to be awaken once the future is resolved.
     * It is registered as an awaiter
     *
     * @param h handle of suspended coroutine waiting for result
     * @return handle of coroutine to wakeup. This can be for example deferred
     * evaluation if it is registered as a coroutine. The called must resume
     * the coroutine. function returns std::noop_coroutine if there is no
     * such coroutine
     */
    bool await_suspend(std::coroutine_handle<> h) {
        return set_callback([h]{return h;});
    }

    ////co_await support, called by resumed coroutine to retrieve a value
    T await_resume() {
        return _shared_future->await_resume();
    }

    template<bool flag = true>
    class has_value_awaiter {
    public:
        has_value_awaiter(shared_future &owner):_owner(owner) {}
        has_value_awaiter(has_value_awaiter &&) = default;
        has_value_awaiter &operator=(has_value_awaiter &&) = delete;

        bool await_ready() const {return _owner.await_ready(); }
        bool await_suspend(std::coroutine_handle<> h) {return _owner.await_suspend(h);}
        bool await_resume() const {return _owner._shared_future->has_value();}

        has_value_awaiter<!flag> operator!() const {return _owner;}
        operator bool() {
            _owner.wait();
            return await_resume();
        }

    protected:
        shared_future &_owner;
    };

    ///Determines whether future has a value (was not canceled)
    /**
     * @return awaitable object. A coroutine must co_await on result to
     * determine the state. Non-coroutine can convert this value to boolean to
     * retrieve the state. In this case, synchronous waiting is performed.
     *  The return value also support operator ! to retrieve inverted value.
     *
     *  The function is intended to co_await on a future and detect canceled
     *  state without throwing an exception. To retrieve value later, the
     *  coroutine can safely use get() or await_resume()
     *
     *  @code
     *  future<int> fut = doSomething();
     *  if (co_await !fut.hasValue()) {
     *      std::cerr << "Canceled" << std::endl;
     *  } else {
     *      int val = fut.get();
     *      std::cout << "Result:" << val << std::endl;
     *  }
     *
     *
     *  @retval true future has value
     *  @retval false future has been canceled
     *
     */
    has_value_awaiter<> has_value() {
        return *this;
    }




protected:

    class Shared: public future<T> {
    public:

        using future<T>::future;

        static void init_callback(std::shared_ptr<Shared> self) {
            if (!self->set_callback([self]() -> std::coroutine_handle<> {
                return self->notify_targets();
            })) {
                self->_await_chain = disabled_slot;
            }
        }

        std::coroutine_handle<> notify_targets() {
            auto n = _await_chain.exchange(disabled_slot);
            return n?notify_targets(n):nullptr;
        }

        static std::coroutine_handle<> notify_targets(shared_future *list) {
            if (list->_next) {
                auto c1 = notify_targets(list->_next);
                auto c2 = list->activate();
                if (c1) {
                    if (c2) {
                        c1.resume();
                        return c2;
                    }
                    return c1;
                }
                return c2;
            } else {
                return list->activate();
            }
        }

        bool register_target(shared_future *item) {
            while (!_await_chain.compare_exchange_strong(item->_next, item)) {
                if (item->_next == disabled_slot) return false;
            }
            return true;
        }


    protected:
        static shared_future * disabled_slot;
        std::atomic<shared_future *> _await_chain;
    };

    enum class State {
        ///awaiter is not set
        unused,
        ///awaiter is set
        awaited,
        ///notification has been sent, awaiter is not set
        notified
    };


    std::shared_ptr<Shared> _shared_future;
    shared_future *_next = nullptr;
    std::atomic<State> _state = {State::unused};

    union {
        awaiter_cb _awaiter;
    };

    std::coroutine_handle<> activate() {
        auto st = _state.exchange(State::notified);
        if (st == State::awaited) {
            auto coro = _awaiter(*_shared_future);
            std::destroy_at(&_awaiter);
            return coro;
        }
        return {};
    }

    void check_in_progress() {
        if (_state.load(std::memory_order_relaxed) == State::awaited) {
            throw still_pending_exception();
        }
    }

};

template<typename T>
inline shared_future<T> * shared_future<T>::Shared::disabled_slot = reinterpret_cast<shared_future<T> *>(1);

}
#endif /* SRC_CORO_FUTURE_H_ */
