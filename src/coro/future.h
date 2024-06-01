#pragma once
#include "common.h"
#include "function.h"
#include "exceptions.h"

#include "prepared_coro.h"
#include "allocator.h"
#include "construct.h"

#include <atomic>
#include <memory>
#include <optional>
#include <utility>
#include <bit>

namespace coro {

///Contains future value of T, can be co_awaited in coroutine
/**
 * @tparam T type which is subject of the future. T can be void. T can be also a reference
 * @ingroup Coroutines, awaitable
 */
template<typename T>
class future;
///Carries reference to future<T>, callable, sets value of an associated future<T>
/**
 * @tparam T contains type of associated future and the same type to be constructed.
 * @tparam atomic set true to make promise atomic - it can be accessed from multiple
 * threads at same time. Default value is false, so promise is not atomic (however it
 * is faster to handle). You can change this flag anytime,
 *
 * @see atomic_promise
 *
 */
template<typename T, bool atomic = false>
class promise;
///Contains future value of T, where evaluation is deferred until the value is needed
/** Deferred future is movable unless it is in progress (which results to exception).
 * This type of future is always returned from a function in deferred state, so
 * it can be moved and evaluated after the future is placed to correct place.
 *
 * Disadvantage of this future is requirement to store evaluation function
 * inside of the future (which must be movable) and guarantee to keep referneces
 * valid until deferred evaluation is started.
 * @ingroup Coroutines, awaitable
 */
template<typename T>
class deferred_future;

namespace _details {
template<typename T> class coro_promise_base;
};

template<typename T, typename ... Args>
concept avoid_same_kind = sizeof...(Args) != 1 || !std::is_same_v<T, std::decay_t<Args>...>;

template<typename Alloc>
concept is_allocator = requires(Alloc alloc, std::size_t n) {
    {alloc.allocate(n)} -> std::same_as<typename Alloc::value_type *>;
};

template<typename T, typename Ret, typename ... Args>
concept invocable_r_exact = std::is_same_v<std::invoke_result_t<T, Args...>, Ret>;

template<typename T, typename ... U>
concept future_constructible = ((sizeof...(U) == 1 && (invocable_r_exact<U, T> && ...)) || std::is_constructible_v<T, U...>);

template<typename T>
using atomic_promise = promise<T, true>;


template<typename T, bool atomic>
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

        ///deliver notification through the function
        /**
         * @param fn A function which receives another function which must be
         * executed. You can for example pass the function to a thread pool for
         * the execution. Return value of the function can be ignored. However
         * it can carry a coroutine handle, you can use this value to improve
         * scheduling. If the return value is ignored, the coroutine
         * is resumed.
         */
        template<std::invocable<function<prepared_coro()>  > Fn>
        void deliver(Fn &&fn) {
            auto ptr = _ptr.release();
            if (ptr) ptr->set_resolved(std::forward<Fn>(fn));
        }


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
            if (ptr) {
                prepared_coro retval;
                ptr->set_resolved([&](auto &&awt){
                    retval = awt();
                });
                return retval.symmetric_transfer();
            } else {
                return std::noop_coroutine();
            }

        }
        ///Determines, whether object carries deferred notification
        explicit operator bool() const {return static_cast<bool>(_ptr);}

        void operator()() {
            _ptr.reset();
        }

        ///combine notification into one object
        notify operator+(notify &other) {
            if (_ptr == nullptr) return std::move(other);
            _ptr->attach(other._ptr.release());
            return std::move(*this);
        }

        ///append a notification
        notify &operator+=(notify &other) {
            if (_ptr == nullptr) {
                _ptr = std::move(other._ptr);
            } else {
                _ptr->attach(other._ptr.release());
            }
            return *this;
        }

        ///append a notification
        notify &operator+=(notify &&other) {
            return operator+=(other);
        }

    protected:

        ///Construct deferred notify from future
        notify(FutureType *fut):_ptr(fut) {}

        struct NotifyAction {
            void operator()(FutureType *fut) {fut->set_resolved([](auto &c){c();});}
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

    ///Move change MT Safety
    template<bool x>
    promise(promise<T, x> &&other): _ptr(other.claim()) {}

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
                        || future_constructible<T, Args ...>, "Value is not constructible from arguments");
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

    ///Combine two promises into one
    /**
     * Two promises are combined into one and they can be fulfilled together by
     * single call operation. Requires T copy constructible. There is no reverse operation
     *
     * @param other other promise which is attached
     *
     * @note result is current promise controls also attached promise.
     * You can attach unlimited count of promises into one.
     *
     * @note order of processing of such promise is not defined - can be random.
     */

    template<bool f>
    promise &operator+=(promise<T, f> &other) {
        promise z =  other + (*this);
        _ptr = z.claim();
        return *this;
    }

    template<bool f>
    promise &operator+=(promise<T, f> &&other) {
        return this->operator+=(other);
    }

    ///Combine two promises into one
    /**
     * Two promises are combined into one and they can be fulfilled together by
     * single call operation. Requires T copy constructible. There is no reverse operation
     *
     * @param other other promise which is attached
     * @return new promise contains combined promises. Previous objects are left empty
     *
     * @note result is current promise controls also attached promise.
     * You can combine unlimited count of promises into one.
     *
     * @note order of processing of such promise is not defined - can be random.
     */
    template<bool f>
    promise operator+(promise<T, f> &other) {
        static_assert(std::is_void_v<T> || std::is_copy_constructible_v<T>, "Requires T copy constructible");
        auto a = claim();
        auto b = other.claim();
        if (!a) return promise(b);
        a->attach(b);
        return promise(a);
    }

    operator bool () const {
        return _ptr.load(std::memory_order_relaxed) != nullptr;
    }

protected:

    struct non_atomic {
        FutureType *_ptr = {};
        non_atomic() = default;
        non_atomic(FutureType *ptr):_ptr(ptr) {}
        FutureType *exchange(FutureType *x, std::memory_order) {
            return std::exchange(_ptr, x);
        }
        void store(FutureType *x, std::memory_order) {
            _ptr = x;
        }
        FutureType *load(std::memory_order) const {
            return _ptr;
        }
        bool compare_exchange_strong(FutureType *& old, FutureType *nw) {
            if (_ptr == old) {
                _ptr = nw;
                return true;
            } else {
                old = _ptr;
                return false;
            }
        }
    };

    using Holder = std::conditional_t<atomic, std::atomic<FutureType *>, non_atomic>;

    Holder _ptr = {};

    FutureType *claim() {return _ptr.exchange(nullptr, std::memory_order_relaxed);}
};

struct deferred_tag {};

inline constexpr deferred_tag deferred=  {};

namespace _details {

    template<typename T>
    class wait_awaiter {
    public:
        wait_awaiter(T *fut):self(fut) {}
        wait_awaiter(const wait_awaiter &other) = delete;
        wait_awaiter &operator=(const wait_awaiter &other) = delete;
        ~wait_awaiter() {wait_now();}
        bool await_ready() const noexcept {return self->await_ready();}
        auto await_suspend(std::coroutine_handle<> h) noexcept {return self->await_suspend(h);}
        void await_resume() const noexcept {}
    protected:
        T *self;
        void wait_now() {
            self->wait_internal();
        }

    };

    template<typename T, bool flag>
    class [[nodiscard]] has_value_awaiter: public wait_awaiter<T> {
    public:
        using wait_awaiter<T>::wait_awaiter;
        bool await_resume() const noexcept {
            return this->self->has_value() == flag;
        }
        has_value_awaiter<T, !flag> operator !() const {return this->self;}
        operator bool() {
            this->wait_now();
            return await_resume();
        }
    };

}


template<typename T>
class [[nodiscard]] future {
public:

    /*
     *   +-------------------+
     *   |   chain           |  8
     *   +-------------------+
     *   |   atomic(_state)  |  1
     *   +-------------------+
     *   |   enum(_result)   |  1
     *   +-------------------+
     *   |   await_cleanup   |  1
     *   +-------------------+ (padding 5)
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
    using value_store_type = std::conditional_t<std::is_reference_v<T>,
                                                        std::add_pointer_t<std::remove_reference_t<T> >,
                                                        std::conditional_t<std::is_void_v<T>, bool, T> >;
    ///type which is used for cast operator ()
    /**
     * This is always T && (if T is reference, then T & && -> T &)
     * The type is bool in case that T is void, because in this case, bool is actually used
     * as storage without defined value
     */
    using cast_ret_value = std::conditional_t<std::is_void_v<T>, bool, std::add_rvalue_reference_t<T> >;
    ///type which is used as return value of get() and await_resume()
    /**
     * Both functions returns rvalue reference for T, lvalue reference for T & and void for void
     * If you need to retrieve lvalue reference when T is specified, then you need to store rvalue
     * reference and convert it to lvalue, or use const T
     */
    using ret_value = std::conditional_t<std::is_void_v<T>, void, std::add_rvalue_reference_t<T> >;
    using awaiter_type = function<prepared_coro()>;
    using deferred_eval_fn_type = function<prepared_coro(promise_t)>;
    using wait_awaiter = _details::wait_awaiter<future>;
    using canceled_awaiter = _details::has_value_awaiter<future,false>;

    future() {/*can't be default*/}

    future(const future &) = delete;        //<can't copy nor move
    future &operator=(const future &) = delete; //<can't copy nor move

    ///Construct future already resolved with a value
    template<typename ... Args>
    requires (std::constructible_from<value_store_type, Args ...> && avoid_same_kind<future, Args...>)
    future(Args && ... args):_result(Result::value), _value(std::forward<Args>(args)...) {}


    ///Construct future already resolved with a value
    /**
     * @param args arguments
     */
    template<typename ... Args>
    future(std::in_place_t, Args &&... args): _result(Result::value), _value(std::forward<Args>(args)...) {}

    ////Construct future already resolved with an exception
    future(std::exception_ptr e):_result(Result::exception), _exception(std::move(e)) {}

    ///Construct future which is evaluated inside of lambda function.
    /**
     * @param fn a lambda function which receives promise object
     */
    template<std::invocable<promise_t> Fn>
    future(Fn &&fn):_state(State::pending) {
        std::forward<Fn>(fn)(promise_t(this));
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

    template<invocable_r_exact<future<T> > Fn>
    future(Fn &&fn) {
        new(this) future(std::forward<Fn>(fn)());
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
    template<invocable_r_exact<future> Fn>
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
        _chain = nullptr;
        return promise_t{this};
    }

    ///Start deferred execution
    /**
     * If the future is in deferred state, it initiates deferred evaluation.
     * When function returns, the future will be in progress. If the future is
     * not in deferred state, the function does nothing
     *
     * @return prepared coroutine, if there is any. You can schedule resumption. Note that future
     * cannot be finished until return value is destroyed
     */
    prepared_coro start() {
        prepared_coro out;
        if (is_deferred()) {
            startDeferredEvaluation([&](auto &&h){out = std::move(h);});
        }
        return out;
    }

    ///Sets callback which is called once future is resolved (in future)
    /**
     * The function is called only when future is unresolved in time of set.
     *
     * @param fn function. The function can return std::coroutine_handke<>, which
     * causes that returned coroutine will be resumed. Other return value is ignored.
     *
     * @retval true callback has been set, and will be called
     * @retval false callback was not set, because future is already resolved.
     *
     * @note There can be only one callback at time
     *
     * @note There can be only one callback at time. Attempt to call this function
     * twice can cause that previous function is destroyed without calling. However
     * this is way how to replace the callback function before resolution.
     *
     */
    template<std::invocable<> Fn>
    [[nodiscard]] bool set_callback(Fn &&fn) {
        return RegAwtRes::constructed_ready == register_awaiter(std::forward<Fn>(fn), [](auto &&){});
    }

    ///unset callback
    /**
     * Function is useful to disarm callback if there is possibility, that
     * callback can access objects going to be destroyed.
     * @retval true unset
     * @retval false future is already resolved
     */
    bool unset_callback() {
        return RegAwtRes::constructed_ready == register_awaiter([]{return prepared_coro{};}, [](auto &&){});
    }


    ///Sets function which is called once future is resolved (always)
    /**
     * The function is called always, regardless on whether the future is already resolved
     * @param fn function to call when future is resolved
     *
     * @retval true function will be called in future
     * @retval false function was called immediatelly, because the future was already resolved
     *
     * (you can ignore return value)
     *
     * @note There can be only one callback at time. Attempt to call this function
     * twice can cause that previous function is destroyed without calling
     */
    template<std::invocable<> Fn>
    bool then(Fn &&fn) {
        auto res = register_awaiter(std::forward<Fn>(fn), [](auto &&){});
        switch (res) {
            case RegAwtRes::resolved: {
                 fn();
            } return false;
            case RegAwtRes::constructed_resolved: {
                _awaiter();
            } return false;
            default:
                return true;
        }
    }

    ///Sets function which is called once future is resolved (always)
    /**
     * The function is called always, regardless on whether the future is already resolved
     * @param fn function to call when future is resolved
     *
     * @retval true function will be called in future
     * @retval false function was called immediatelly, because the future was already resolved
     *
     * (you can ignore return value)
     */
    template<std::invocable<> Fn>
    bool operator>>(Fn &&fn) {
        return then(std::forward<Fn>(fn));
    }



    ///Perform synchronous wait on resolution
    /**
     * Blocks execution until future is resolved. If the future is deferred, the
     * evaluation is started now.
     *
     * Result is awaitable. You can use @b co_await .wait() to await on resolution without
     * retrieving the value (or throw exceptionb)
     */
    wait_awaiter wait() noexcept {return this;}


    ///Retrieves value, performs synchronous wait
    ret_value get() {
        wait();
        return await_resume();
    }

    ///Retrieves value, performs synchronous wait
    operator cast_ret_value() {
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

    ///@b co_await support, returns true, if value is ready (resolved)
    bool await_ready() const noexcept {
        return _state.load(std::memory_order_relaxed) == State::resolved;
    }

    ///@b co_await support, called with suspended coroutine
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
    std::coroutine_handle<> await_suspend(std::coroutine_handle<> h) noexcept {
        prepared_coro retval = {};
        auto cb = [h]{return h;};
        auto resume_fn = [&retval](prepared_coro h){retval = std::move(h);};
        if (RegAwtRes::constructed_ready == register_awaiter(cb, resume_fn)) {
            return retval.symmetric_transfer();
        }
        return h;
    }

    ////@b co_await support, called by resumed coroutine to retrieve a value
    ret_value await_resume() {
        if constexpr(std::is_void_v<T>) {
            getInternal();
        } else {
            return getInternal();
        }
    }

    ///Determines, whether future has a value
    /**
     * @retval true future has value  or exception
     * @retval false future has no value nor exception
     *
     * @note future must be resolved otherwise undefined (use wait() )
     */
    bool has_value() const {
        return _result != Result::not_set;
    }

    ///Determines, whether future has exception
    /**
     * @retval true future has exception
     * @retval false future hasn't exception
     *
     * @note future must be resolved otherwise undefined (use wait() )
     */
    bool has_exception() const {
        return _result == Result::exception;
    }


    ///awaitable for canceled operation
    /** Result of this operator is awaitable. It returns whether future has been canceled
     *
     * @retval true canceled
     * @retval false not canceled
     *
     * You can invert meaning by applying ! on return value
     *
     * @code
     * if (co_await !!fut) {... fut wasn't canceled...}
     * @endcode
     */
    canceled_awaiter operator!() {return this;}

    ///Forward value, possibly convert it, to different promise
    /**
     * The source future must be resolved. The function forwards whole state without
     * throwing any exception. So the target promise recives possible exception or
     * cancel status
     *
     * @param prom target promise
     * @return notify of target promise. If the future is not resulved, return
     * is false
     *
     * @note uses copying. If you need move, you need to use std::move(x.forward_to(...))
     */
    template<typename X>
    typename promise<X>::notify forward_to(promise<X> &prom) & noexcept  {
        if (this->is_pending()) return {};
        switch (_result) {
            case Result::exception: return prom.reject(_exception);
            case Result::value:
                if constexpr(std::is_void_v<X>) {
                    return prom();
                } else {
                    if constexpr(std::is_reference_v<T>) {
                        return prom(*_value);
                    } else {
                        return prom(_value);
                    }
                }
            default:
            case Result::not_set:
                return prom.cancel();

        }

    }
    template<typename X>
    typename promise<X>::notify forward_to(promise<X> &&prom) & noexcept  {
        return forward_to(prom);
    }

    ///Forward value, possibly convert it, to different promise
    /**
     * The source future must be resolved. The function forwards whole state without
     * throwing any exception. So the target promise recives possible exception or
     * cancel status
     *
     * @param prom target promise
     * @return notify of target promise. If the future is not resulved, return
     * is false
     *
     * @note uses move
     */
    template<typename X>
    typename promise<X>::notify forward_to(promise<X> &prom) && noexcept  {
        if (this->is_pending()) return {};
        switch (_result) {
            case Result::exception: return prom.reject(_exception);
            case Result::value:
                if constexpr(std::is_void_v<X>) {
                    return prom();
                } else {
                    if constexpr(std::is_reference_v<T>) {
                        return prom(std::move(*_value));
                    } else {
                        return prom(std::move(_value));
                    }
                }
            default:
            case Result::not_set:
                return prom.cancel();

        }

    }

    template<typename X>
    typename promise<X>::notify forward_to(promise<X> &&prom) && noexcept  {
        return std::move(*this).forward_to(prom);
    }


    ///Forward value, possibly convert it, to different promise
    /**
     * The source future must be resolved. The function forwards whole state without
     * throwing any exception. So the target promise recives possible exception or
     * cancel status
     *
     * @param prom target promise
     * @param convert function which perform conversion from T to X. For T=void, the
     * passed value is bool.  If the function returns void, default constructor is used
     * to construct result
     *
     * @return notify of target promise. If the future is not resulved, return
     * is false
     */
    template<typename X, std::invocable<cast_ret_value> Fn>
    typename promise<X>::notify convert_to(promise<X> &prom, Fn &&convert) noexcept {
        if (this->is_pending()) return {};

        switch (_result) {
            case Result::exception: return prom.reject(_exception);
            case Result::value:
                if constexpr(std::is_void_v<std::invoke_result_t<Fn,cast_ret_value> >) {
                    if constexpr(std::is_reference_v<T>) {
                        std::forward<Fn>(convert)(_value);
                        return prom();
                    } else {
                        std::forward<Fn>(convert)(_value);
                        return prom();
                    }
                } else {
                    if constexpr(std::is_reference_v<T>) {
                        return prom(std::forward<Fn>(convert)(std::forward<cast_ret_value>(*_value)));
                    } else {
                        return prom(std::forward<Fn>(convert)(std::forward<cast_ret_value>(_value)));
                    }
                }
            default:
            case Result::not_set:
                return prom.cancel();

        }
    }
    template<typename X, std::invocable<cast_ret_value> Fn>
    typename promise<X>::notify convert_to(promise<X> &&prom, Fn &&convert) noexcept {
        return convert_to(prom, std::forward<Fn>(convert));
    }



protected:

    enum class State: char {
        //future is resolved
        resolved,
        //evaluation is deferred
        deferred,
        //future is pending  - no awaiter
        pending,
        //future is pending - awaiter is set
        awaited,
        //future is pending - deferred evaluation is current in progress
        evaluating,

    };

    enum class Result : char{
        //result is not set (yet?)
        not_set,
        //result contains value
        value,
        //result contains exception
        exception,
    };

    enum class RegAwtRes {
        //awaiter constructed, but future is resolved
        constructed_resolved,
        //awaiter constructed and ready
        constructed_ready,
        //future resolved, nothing happened
        resolved

    };

    using ChainPtr = future *;

    ChainPtr _chain = {};
    std::atomic<State> _state = {State::resolved}; //< by default, future is resolved in canceled state
    Result _result = Result::not_set; //< by default, future has no value
    bool _awaiter_cleanup = false;   //is set to true, if awaiter must be destroyed (in resolved state)


    //because deferred and awaited are mutually exclusive,
    //reuse single spcopy_ace for both functions
    union {
        awaiter_type _awaiter;
        deferred_eval_fn_type _deferred;
    };


    union {
        value_store_type _value;
        std::exception_ptr _exception;
        coro::function<T(), std::max(sizeof(value_store_type),sizeof(void *)*2)> _lambda;
    };



    void clearStorage() {
        switch (_result) {
            default: break;
            case Result::value: std::destroy_at(&_value);break;
            case Result::exception: std::destroy_at(&_exception);break;
        }
        _result = Result::not_set;
        check_awaiter_cleanup();
    }

    void wait_internal() {
        auto st = _state.load(std::memory_order_acquire);
        while (st != State::resolved) {
            if (st == State::deferred) {
                startDeferredEvaluation([](auto c){c();});
            } else {
                _state.wait(st);
            }
            st = _state.load(std::memory_order_acquire);
        }
    }

    cast_ret_value getInternal() {
        switch (_result) {
            case Result::value:
                if constexpr(std::is_reference_v<T>) {
                    return std::forward<cast_ret_value>(*_value);
                } else {
                    return std::forward<cast_ret_value>(_value);
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
        //lock awaiter state (should be deferred previously)
        State new_state = State::evaluating;
        _state.exchange(new_state, std::memory_order_acquire);
        //call the deferred function and pass promise, pass coroutine to resume_fn
        resume_fn(_deferred(promise_t(this)));
        //destroy deferred function to leave place for awaiter
        std::destroy_at(&_deferred);
        //try to set pending - however, it can be already resolved now
        //return false, if resolved
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
        //check return value - should return coroutine handle
        if constexpr(std::is_invocable_r_v<prepared_coro, Fn, promise_t> ) {
            //we expecting resolved state - other state is error
            State old = State::resolved;
            if (!_state.compare_exchange_strong(old,State::deferred, std::memory_order_relaxed)) {
                throw still_pending_exception();
            }
            //construct the function
            try {
                std::construct_at(&_deferred, std::forward<Fn>(fn));
                return;
            } catch (...) {
                //in case of exception, restore state
                _state.store(old, std::memory_order_relaxed);
                //and rethrow
                throw;
            }
        } else {
            //otherwise, wrap the function
            setDeferredEvaluation([xfn = std::move(fn)](promise_t prom) mutable->prepared_coro{
                xfn(std::move(prom));
                return {};
            });
        }
    }

    template<typename ... Args>
    void set_value(Args && ... args) {
        try {
            clearStorage();
            if constexpr(std::is_reference_v<T>) {
               std::construct_at(&_value, &args...);
            } else if constexpr(sizeof...(Args) == 1 && (invocable_r_exact<Args, value_store_type> && ...)) {
               new (&_value) value_store_type(args()...);
            } else {
               std::construct_at(&_value, std::forward<Args>(args)...);
            }
            _result = Result::value;

            if (_chain) {
                if constexpr(std::is_void_v<T>) {
                    _chain->set_value();
                } else if constexpr(std::is_reference_v<T>) {
                    _chain->set_value(*_value);
                } else if constexpr(std::is_copy_constructible_v<T>) {
                    _chain->set_value(_value);
                } /* else {
                    will cancel await (exception) as we cannot copy the valUE
                }*/
            }
        } catch (...) {
            set_exception(std::current_exception());
        }
    }

    void set_exception(std::exception_ptr e) {
        clearStorage();
        std::construct_at(&_exception, std::move(e));
        _result = Result::exception;
        if (_chain) _chain->set_exception(_exception);
    }

    template<typename SchedulerFn>
    void set_resolved(SchedulerFn &&schfn) {
        State st = _state.exchange(State::resolved);
        auto chain = _chain;
        _state.notify_all();
        if (st == State::awaited) {
            _awaiter_cleanup = true;
            schfn(_awaiter);
        }
        if (chain) chain->set_resolved(std::forward<SchedulerFn>(schfn));
    }


    void check_awaiter_cleanup() {
        if (_awaiter_cleanup) {
            std::destroy_at(&_awaiter);
            _awaiter_cleanup = false;
        }
    }


    template<std::invocable<> Awt, typename ResumeFn >
    RegAwtRes register_awaiter(Awt &&awt, ResumeFn &&resumeFn) {
        //lock awaiter slot
        State st = _state.exchange(State::evaluating);
        switch (st) {
            //already resolved?
            case State::resolved: _state.store(st); //restore state
                                  return RegAwtRes::resolved;

            case State::pending: //<still pending? move awaiter
                new(&_awaiter) auto(to_awaiter(awt));
                break;
            default:
            case State::evaluating:  //<locked - this is error
                throw still_pending_exception();
            case State::awaited:   //<already awaited? destroy previous awaiter and construct new one
                std::destroy_at(&_awaiter);
                new(&_awaiter) auto(to_awaiter(awt));
                break;
            case State::deferred: //<deferred ? perform evaluation and try again
                if (!startDeferredEvaluation(resumeFn)) return RegAwtRes::resolved;
                return register_awaiter(std::forward<Awt>(awt),std::forward<ResumeFn>(resumeFn));
        }
        st = State::evaluating; //replace evaluating with awaited
        if (!_state.compare_exchange_strong(st, State::awaited)) {
            //if failed, it should be resolved
            //mark that awaited must be clean up (always)
            _awaiter_cleanup = true;
            return RegAwtRes::constructed_resolved;
        }
        //awaiter has been set
        return RegAwtRes::constructed_ready;
    }

    template<std::invocable<> Fn>
    static awaiter_type to_awaiter(Fn &fn) {
        if constexpr (std::is_constructible_v<prepared_coro, std::invoke_result_t<Fn> >) {
            return std::move(fn);
        } else {
            return [fn = std::move(fn)]() mutable -> prepared_coro {
              fn();return {};
            };
        }
    }

    void checkInProgress() {
        if (is_in_progress()) throw still_pending_exception();
    }


    ///Attach future to internal linked list.
    /**Futures can be chained into linked list. This allows to control
     * this "internal" linked list. This function is added to support
     * combining of promises
     *
     * @param x pointer to a future object which will be attached to the
     * current future. If the current future already contains attached
     * future, a new future is added to the end.
     *
     * For better performance, always attach existing linked list to a new root
     * @code
     * newitem->attach(list);
     * list = newitem;
     * @endcode
     *
     * @note Function is not MT-Safe.
     */
    void attach(future *x) {
        future **ch = &_chain;
        while (*ch) ch = &((*ch)->_chain);
        *ch = x;
    }

    ///Detach linked list from the future
    /**
     * Detaches existing linked list from this future, if exists
     *
     * @return pointer to linked list, or nullptr;
     *
     * @note Function is not MT-Safe.
     */
    future *detach() {
        future *r = _chain;
        if (r) _chain = r->_chain;
        return r;
    }

    friend class _details::coro_promise_base<T>;
    friend class _details::wait_awaiter<future>;
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

    ///By calling deferred future with a promise, the future is started and result is trensfered to the promise
    /**This allows to convert deferred_future to a future. The state of current
     * object is change to resolved without a value.
     */
    void operator()(promise_t &&prom) & {
        State st = this->_state;
        if (st == State::deferred) {
            this->_deferred(std::move(prom));
            std::destroy_at(&this->_deferred);
            this->_state = State::resolved;
        } else if (st == State::resolved) {
            this->forward_to(prom);
        } else {
            throw still_pending_exception();
        }
    }

    ///By calling deferred future with a promise, the future is started and result is trensfered to the promise
    /**This allows to convert deferred_future to a future. The state of current
     * object is change to resolved without a value.
     */
    void operator()(promise_t &&prom) && {
        State st = this->_state;
        if (st == State::deferred) {
            this->_deferred(std::move(prom));
            std::destroy_at(&this->_deferred);
            this->_state = State::resolved;
        } else if (st == State::resolved) {
            std::move(*this).forward_to(prom);
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

    prepared_coro set_resolved()  {
        prepared_coro ret;
        auto tmp = std::exchange(fut, nullptr);
        tmp->set_resolved([&](auto &&fn){ret = fn();});
        return ret;
    }
    template<typename ... Args>
    void set_value(Args && ... args) const {
        if (fut) fut->set_value(std::forward<Args>(args)...);
    }

    coro_promise_base() = default;
    coro_promise_base(const coro_promise_base &x) = delete;
    coro_promise_base &operator=(const coro_promise_base &) = delete;
    ~coro_promise_base() {
        if (fut) fut->set_resolved([](auto &&){});
    }


    void set_exception(std::exception_ptr e) {
        if (fut) fut->set_exception(std::move(e));
    }


public:
    void unhandled_exception() {
        if (fut) fut->set_exception(std::current_exception());
    }
    LIBCORO_TRACE_AWAIT

};


template<typename T>
class coro_promise: public coro_promise_base<T> {
public:

    template<typename Arg>
    requires future_constructible<T, Arg>
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
 *
 * @ingroup awaitable
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
    using cast_ret_value = typename future<T>::cast_ret_value;
    using ret_value = typename future<T>::ret_value;
    using awaiter_cb = function<prepared_coro()>;
    using wait_awaiter = _details::wait_awaiter<shared_future>;
    using canceled_awaiter = _details::has_value_awaiter<shared_future,false>;

    ///Construct shared future uninitialized
    /** Uninitialized object cannot be used immediately, but assignment can be
     * used to initialize object later
     */
    shared_future() {}


    ///Construct shared future initialize it same way asi coro::future
    /**
     * @param arg0 first argument. If the first argument is standard allocator (std::allocator)
     * it uses allocator to allocate shared future instance. If the first argument is not
     * allocator, the argument is passed as first argument to shared future  instance directly
     * @param args other arguments
     *
     * @note you can use all constructor types in same way as coro::future. If you
     * need to convert coro::future to coro::shared_future, you need to pass a
     * lambda function which returns coro::future
     *
     * @code
     * coro::shared_future<int> f([&]()->coro::future<int>{return calc_async();});
     * @endcode
     */
    template<typename Arg0, typename ... Args>
    requires (!std::is_same_v<std::decay_t<Arg0>, shared_future>)
    shared_future(Arg0 &&arg0, Args && ...args) {
        if constexpr(is_allocator<std::decay_t<Arg0> >) {
            static_assert(std::is_constructible_v<Shared, Args ...>, "Invalid arguments");
            _shared_future = std::allocate_shared<Shared>(arg0, std::forward<Args>(args)...);
        } else {
            static_assert(std::is_constructible_v<Shared, Arg0, Args ...>, "Invalid arguments");
            _shared_future = std::make_shared<Shared>(arg0, std::forward<Args>(args)...);
        }
        _shared_future->init_callback(_shared_future);
    }

    ///shared future is copyable
    shared_future(shared_future &other):_shared_future(other._shared_future) {}
    ///shared future is copyable
    shared_future(const shared_future &other):_shared_future(other._shared_future) {}
    ///you can assign
    shared_future &operator=(const shared_future &other) {
        this->_shared_future = other._shared_future;
        _state.store(State::unused, std::memory_order_relaxed);
        return *this;
    }

    ///destructor
    ~shared_future() {
        check_in_progress();
    }

    ///Initialize future and get promise
    promise<T> get_promise() {
        _shared_future = std::make_shared<Shared>();
        promise<T> p = _shared_future->get_promise();
        init();
        return p;
    }

    ///Initialize future and get promise
    /**
     * Initializes future using standard stl allocator
     * @tparam stl_allocator
     * @param allocator instance of standard allocator
     * @return
     */
    template<typename stl_allocator>
    promise<T> get_promise(stl_allocator &&allocator) {
        _shared_future = std::allocate_shared<Shared>(std::forward<allocator>(allocator));
        promise<T> p = _shared_future->get_promise();
        init();
        return p;
    }


    ///Redirect return value of function returning coro::future to instance of shared_future
    template<std::invocable<> Fn>
    void operator<<(Fn &&fn) {
        static_assert(std::is_invocable_r_v<coro::future<T>, Fn>);
        _shared_future = std::make_shared<Shared>(std::forward<Fn>(fn));
        init();
    }

    ///Redirect return value of function returning coro::future to instance of shared_future
    /**
     * @param tpl contains tuple, which carries function and allocator
     */
    template<std::invocable<> Fn, typename std_allocator>
    void operator<<(std::tuple<Fn, std_allocator> && tpl) {
        static_assert(std::is_invocable_r_v<coro::future<T>, Fn>);
        _shared_future = std::allocate_shared<Shared>(
                std::forward<std_allocator>(std::get<std_allocator>(tpl)),
                std::forward<Fn>(std::get<Fn>(tpl)));
        init();
    }


    ///set callback which is invoked when future is resolved
    /**
     * @param fn function
     * @retval true callback set
     * @retval false callback was not set, already resolved
     */
    template<std::invocable<> Fn>
    bool set_callback(Fn &&fn) {
        auto st = _state.exchange(State::unused);
        if constexpr(std::is_constructible_v<prepared_coro, std::invoke_result_t<Fn> >) {
            _awaiter.emplace(std::forward<Fn>(fn));
        } else {
            _awaiter.emplace([fn = std::move(fn)]() mutable ->prepared_coro{
                fn(); return {};
            });
        }
        if (st == State::notified) return false;
        auto st2 = State::unused;
        if (_state.compare_exchange_strong(st2, State::awaited)) {
            if (st == State::unused) return _shared_future->register_target(this);
            return true;
        }
        return false;
    }

    ///execute callback when future is resolved
    /**
     * @param fn callback to executed. The callback is always executed.
     * @retval true, callback will be executed
     * @retval false callback has been executed during this function, because
     * the future is already resolved
     */
    template<std::invocable<> Fn>
    bool then(Fn &&fn) {
        if (!set_callback(std::forward<Fn>(fn))) {
            (*_awaiter)();
            return false;
        }
        return true;
    }


    ///alias to then()
    template<std::invocable<> Fn>
    bool operator>>(Fn &&fn) {
        return when_resolved(std::forward<Fn>(fn));
    }


    ///Perform synchronous wait on resolution
    /**
     * Blocks execution until future is resolved.
     */
    wait_awaiter wait() {return this;}


    ///Retrieves value, performs synchronous wait
    ret_value get() {
        wait();
        return _shared_future->get();
    }


    ///Retrieves value, performs synchronous wait
    operator cast_ret_value (){
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

    ///@b co_await support, returns true, if value is ready (resolved)
    bool await_ready() const {
        return !_shared_future || _shared_future->await_ready();
    }

    ///@b co_await support, called with suspended coroutine
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

    ////@b co_await support, called by resumed coroutine to retrieve a value
    ret_value await_resume() {
        return _shared_future->await_resume();
    }


    ///reset state
    /**
     * releases reference and stays uninitialized
     */
    void reset() {
        _shared_future.reset();
    }

    ///Determines, whether future has a value
    /**
     * @retval true future has value  or exception
     * @retval false future has no value nor exception
     *
     * @note future must be resolved otherwise undefined (use wait() )
     */
    bool has_value() const {
        return _shared_future->has_value();
    }

    ///Determines, whether future has exception
    /**
     * @retval true future has exception
     * @retval false future hasn't exception
     *
     * @note future must be resolved otherwise undefined (use wait() )
     */
    bool has_exception() const {
        return _shared_future->has_exception();
    }

    ///awaitable for canceled operation
    /** Result of this operator is awaitable. It returns whether future has been canceled
     *
     * @retval true canceled
     * @retval false not canceled
     *
     * You can invert meaning by applying ! on return value
     *
     * @code
     * if (co_await !!fut) {... fut wasn't canceled...}
     * @endcode
     */
    canceled_awaiter operator!()  {return this;}




protected:

    class Shared: public future<T> {
    public:

        using future<T>::future;

        static void init_callback(std::shared_ptr<Shared> self) {
            self->then([self = std::move(self)]() mutable -> prepared_coro{
                auto r = self->notify_targets();
                self.reset();
                return r;
            });
        }

        prepared_coro notify_targets() {
            auto n = _await_chain.exchange(disabled_slot());
            return n?notify_targets(n):prepared_coro{};
        }

        static prepared_coro notify_targets(shared_future *list) {
            if (list->_next) {
                auto c1 = notify_targets(list->_next);
                auto c2 = list->activate();
                if (c1) {
                    if (c2) {
                        c1();
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
                if (item->_next == disabled_slot()) return false;
            }
            return true;
        }


    protected:
        std::atomic<shared_future *> _await_chain = {};
    };



    enum class State: char {
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
    std::optional<awaiter_cb> _awaiter;

    prepared_coro activate() {
        auto st = _state.exchange(State::notified);
        if (st == State::awaited) {
            return (*_awaiter)();
        }
        return {};
    }

    void wait_internal() {
        _shared_future->wait();
    }


    void check_in_progress() {
        if (_state.load(std::memory_order_relaxed) == State::awaited) {
            throw still_pending_exception();
        }
    }


    void init() {
        _shared_future->init_callback(_shared_future);
        _state.store(State::unused, std::memory_order_relaxed);
    }


    friend class _details::wait_awaiter<shared_future>;


    static shared_future<T> * disabled_slot() {return reinterpret_cast<shared_future<T> *>(1);}


};


#if 0

class future_with_callback {
public:

    template<typename Fut>
    future_with_callback(Fut &&fut) {
        this->fut = &fut;
        set_cb = [](void *fut, function<prepared_coro()>fun){
            using FutT = std::decay_t<Fut>;
            FutT *f = reinterpret_cast<FutT *>(fut);
            f->then(std::move(fun));
        };
    }

    template<std::invocable<> Fn>
    void operator>>(Fn &&fn) {
        if constexpr(std::is_same_v<std::invoke_result_t<Fn>, prepared_coro>) {
            set_cb(fut, std::forward<Fn>(fn));
        } else{
            set_cb(fut, [fn = std::move(fn)]()mutable -> prepared_coro {
                fn();
                return {};
            });
        }
    }

protected:
    void *fut;
    void (*set_cb)(void *fut, function<prepared_coro()>);
};



///Awaitable (future) which is resolved once all of the futures are resolved
/**
 * This object doesn't return anything. You need to access original futures for results
 */
class all_of: public future<void> {
public:


    template<typename Iter>
    all_of(Iter from, Iter to) {
        result = get_promise();
        auto cb = [this]{if (--remain == 0) result();};
        while (from != to) {
            ++remain;
            static_cast<future_with_callback>(*from) >> cb;
            ++from;
        }
        cb();
    }

    all_of(std::initializer_list<future_with_callback> iter)
        :all_of(iter.begin(), iter.end()) {}

protected:
    promise<void> result = {};
    std::atomic<int> remain = {1};

};

#endif
#if 0
class any_of: public future<unsigned int> {
public:

    template<typename Iter>
    any_of(Iter from, Iter to) {
        cleanup = [=]{do_cleanup(from, to);};
        result = get_promise();
        unsigned int idx = 0;
        while (from != to) {
            static_cast<future_with_callback>(*from) >> [idx, this] {
                unsigned int r = notset;
                if (selected.compare_exchange_strong(r, idx)) {
                    finish();
                }
            };
            ++from;
            ++idx;
        }
        if (inited.fetch_add(2) > 0) {
            cleanup();
        }
    }


    any_of(std::initializer_list<future_with_callback> iter)
        :any_of(iter.begin(), iter.end()) {}


protected:

    void finish() {
        if (inited.fetch_add(1) > 1) {
            cleanup();
        }
        result(selected);
    }


    static constexpr unsigned int notset = std::bit_cast<unsigned int>(-1);
    promise<unsigned int> result = {};
    std::atomic<unsigned int> selected = {notset};
    std::atomic<int> inited = {0}; //0 - nothing, 1 - resolved before init, 2 - init waiting, 3 - resolved after init
    function<void(), 8*sizeof(void *)> cleanup;

    static void do_cleanup(auto from, auto to) {
        while (from != to) {
            static_cast<future_with_callback>(*from) >> []{return prepared_coro();};
            ++from;
        }
    }
};
#endif



}

