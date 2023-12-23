#pragma once

#include "target.h"
#include "exceptions.h"

#include <thread>
#include <variant>

namespace coro {

template<typename T, coro_optional_allocator Alloc = void> class async;
template<typename T> struct async_promise_base;
template<typename T, coro_optional_allocator Alloc = void> struct async_promise_type;
template<typename T> class lazy_future;

struct emplace_tag {};
constexpr emplace_tag emplace;

template<typename T> class future;

///Promise type
/**Promise resolves pending Future.
 * Promise is movable object. There can be only one Promise per Future.
 * Promise acts as callback, which can accepts multiple argumens depend on
 * constructor of the T
 * Promise is one shot. Once it is fullfilled, it is detached from the Future
 * Multiple threads can try to resolve promise simultaneously. this operation is
 * MT safe. Only one thread can success, other are ignored
 * Promise can rejected with exception. You can use reject() without arguments
 * inside of catch block
 * Destroying not-filled promse causes BrokenPromise
 * You can break promise by function drop()
 *
 */
template<typename T>
class promise {
public:

    using future = ::coro::future<T>;



    struct pending_notify_deleter {void operator()(future *ptr)noexcept{ptr->notify_resume();}};

    ///Intermediate state of the future
    /**
     * This stores state of the future between setting the value and sending notification.
     * In normal processing, by setting value through the promise causes immediately
     * resumption. However, as result of setting a value you can retrieve pending_notify
     * object, which allows to schedule notification.
     *
     * In this state, the future has already value, but it is not considered as
     * resolved. You can still break the promise especially when you fail to
     * deliver the notification.
     *
     * Note this object is not MT Safe
     *
     * If the object is ignored, standard destructor delivers the notification in
     * current thread
     */



    class pending_notify: protected std::unique_ptr<future, pending_notify_deleter>  {
    public:
        using std::unique_ptr<future, pending_notify_deleter>::unique_ptr;
        using std::unique_ptr<future, pending_notify_deleter>::release;
        using std::unique_ptr<future, pending_notify_deleter>::operator bool;

        ///causes that already resolved promise is dropped
        /** This can be useful when resumption is scheduled, but scheduler
         * failed to perform resumtion. The stored value is destroyed. Resumption
         * is not performed now.
         */
        void drop() {
            if (*this) {
                (*this)->drop();

            }
        }
        ///returns value which can be used as return value on await_suspend()
        /**
         * @return a valid coroutine handle. It assumes, that notification is
         * finished by this operation.
         *
         * @note if the associated future is awaited by callback or synchronous wait,
         * the notification is perfomed now and returned handle is noop_coroutine
         */
        std::coroutine_handle<> on_await_suspend() noexcept {
            auto ptr = this->release();
            if (ptr) {
                auto r = ptr->notify_targets();
                if (r) return r;
            }
            return std::noop_coroutine();
        }

        void operator()() {
            this->reset();
        }

    };



    ///Construct unbound promise
    promise() = default;
    ///Construct bound promise to a future
    promise(future *f) noexcept :_ptr(f) {}
    ///Move promise
    promise(promise &&other) noexcept:_ptr(other._ptr.exchange(nullptr, std::memory_order_relaxed)) {}
    ///Copy is disabled
    promise(const promise &other) = delete;
    ///Destructor drops promise
    ~promise() {
        drop();
    }

    ///Move promise by assignment. It can drop original promise
    promise &operator=(promise &&other) noexcept {
        if (this != &other) {
            auto x = other._ptr.exchange(nullptr, std::memory_order_relaxed);
            if (*this) reject();
            _ptr.store(x, std::memory_order_relaxed);
        }
        return *this;
    }
    promise &operator=(const promise &other) = delete;


    ///Drop promise - so it becomes broken
    /**
     * @retval true  success
     * @retval false promise is unbound
     */
    pending_notify drop() noexcept {
        auto x = _ptr.exchange(nullptr, std::memory_order_relaxed);
        if (x) x->drop();
        return pending_notify(x);
    }


    ///resolve promise
    /** Imitates a callback call
     *
     * @param args arguments to construct T
     * @retval true success- resolved
     * @retval false promise is unbound, object was not constructed
     *
     * */
    template<typename ... Args>
    pending_notify operator()(Args && ... args)  {
        auto x = _ptr.exchange(nullptr, std::memory_order_relaxed);
        if (x) {
            x->set_value(std::forward<Args>(args)...);
            return pending_notify(x);
        }
        return {};
    }



    ///reject promise with exception
    /**
     * @param except exception to construct
     * @retval true success - resolved
     * @retval false promise is unbound
     */
    template<typename Exception>
    pending_notify reject(Exception && except) {
        auto x = _ptr.exchange(nullptr, std::memory_order_relaxed);
        if (x) {
            x->set_exception(std::make_exception_ptr<Exception>(std::forward<Exception>(except)));
            return pending_notify(x);
        }
        return {};
    }

    pending_notify reject(std::exception_ptr e) {
        auto x = _ptr.exchange(nullptr, std::memory_order_relaxed);
        if (x) {
            x->set_exception(std::move(e));
            return pending_notify(x);
        }
        return {};
    }

    ///Determines state of the promise
    /**
     * @retval true promise is bound to pending future
     * @retval false promise is unbound
     */
    explicit operator bool() const {
        return _ptr.load(std::memory_order_relaxed) != nullptr;
    }

    ///Reject under catch handler
    /**
     * If called outside of catch-handler it is equivalent to drop()
     * @retval true success
     * @retval false promise is unbound
     */
    auto reject() {
        auto exp = std::current_exception();
        if (exp) {
            return reject(std::move(exp));
        } else {
            return drop();
        }
    }



    ///Release internal pointer to the future
    /** It can be useful to transfer the pointer through non-pointer type variable,
     * for example if you use std::uintptr_t. You need construct Promise from this
     * pointer to use it as promise
     *
     * @return pointer to pending future. The future is still pending.
     *
     * @note this instance looses the ability to resolve the future
     *
     */
    future *release() {
        return _ptr.exchange(nullptr, std::memory_order_relaxed);
    }


protected:
    std::atomic<future *> _ptr = {nullptr};
};


template<typename T>
class future: public future_tag {

    ///is true when T is void
    static constexpr bool is_void = std::is_void_v<T>;
    static constexpr bool is_rvalue_ref = !std::is_void_v<T> && std::is_rvalue_reference_v<T>;
    static constexpr bool is_lvalue_ref = !std::is_void_v<T> && std::is_lvalue_reference_v<T>;

public:

    ///contains type
    using value_type = std::decay_t<T>;

    ///declaration of type which cannot be void, so void is replaced by bool
    using voidless_type = std::conditional_t<is_void, bool, value_type >;
    ///declaration of return value - which is reference to type or void
    using ret_type = std::conditional_t<is_rvalue_ref, T, std::add_lvalue_reference_t<T> >;

    using StorageType = std::conditional_t<is_lvalue_ref, voidless_type *, voidless_type>;
    using construct_type = std::conditional_t<is_lvalue_ref, voidless_type &, voidless_type>;

    using cast_ret_type = std::conditional_t<is_void, bool, ret_type>;

    ///Tags future<T> as valid return value for coroutines
    using promise_type = async_promise_type<T>;


    ///Specifies target in code where execution continues after future is resolved
    /** This is maleable object which can be used to wakeup coroutines or
     * call function when asynchronous operation is complete. It was intentionaly
     * designed as non-function object to reduce heap allocations. Target can be
     * declared statically, or can be instantiated as a member variable without
     * need to allocate anything. If used along with coroutines, it is always
     * created in coroutine frame which is already preallocated.
     *
     * Target is designed as POD, it has no constructor, no destructor and it
     * can be put inside to union with other Targets if only one target
     * can be charged
     */
    using target_type = target<future<T> *>;
    using unique_target_type = unique_target<target_type>;
    using promise = ::coro::promise<T>;
    using pending_notify = typename promise::pending_notify;

    future():_state(nothing) {}

    future(construct_type v):_state(value),_value(from_construct_type(std::forward<construct_type>(v))) {}

    template<typename ... Args>
    future(emplace_tag, Args && ... args):_state(value), _value(std::forward<Args>(args)...) {}

    future(std::exception_ptr e):_state(exception), _exception(std::move(e)) {}

    template<invocable_with_result<void, promise> Fn>
    future(Fn &&fn):_state(nothing),_targets(nullptr) {
        fn(promise(this));
    }

    template<invocable_with_result<future> Fn>
    future(Fn &&fn) {
        new(this) future(fn());
    }

    future(const future &) = delete;
    future &operator=(const future &) = delete;

    ~future() {
        cleanup();
    }


    bool is_pending() const {
        return _targets.load(std::memory_order_relaxed) != &disabled_target<target_type>;
    }

    promise get_promise() {
        const target_type *need = &disabled_target<target_type>;
        if (!_targets.compare_exchange_strong(need, nullptr, std::memory_order_relaxed)) {
            throw already_pending_exception();
        }
        drop();
        return promise(this);
    }

    ///synchronous wait
    /**
     * @note to perform asynchonous wait, use co_await on has_value()
     */
    void wait() noexcept {
        sync_target<target_type> t;
        if (register_target_async(t)) {
            t.wait();
        }
    }

    ret_type get() {
        wait();
        return get_internal();
    }

    operator cast_ret_type() {
        if constexpr(is_void) {
            wait();
            return _value;

        } else {
            return get();
        }
    }

    bool register_target_async(target_type &t) {
        return t.push_to(_targets);
    }
    bool register_target(target_type &t) {
        if (register_target_async(t)) return true;
        t.activate_resume(this);
        return false;
    }
    bool register_target_async(unique_target_type t) {
        auto ptr = t.release();
        if (register_target_async(ptr)) return true;
        delete ptr;
        return false;
    }
    bool register_target(unique_target_type t) {
        return register_target(t.release());
    }

    ///Replace target(s)
    /**
     * Purpose of this function is to nulify targets by replacing them with a safe target.
     * You need to somehow process old targets.
     * @param t pointer which holds new target. It receives old target(s) when function
     * successes.
     *
     * @retval true success
     * @retval false future is already resolved, no targets can be replaced
     *
     * @note function is MT Safe
     */
    bool replace_target(const target_type  *  &t) {
        const target_type *need = _targets.load(std::memory_order_relaxed);
        while (need != t && need != &disabled_target<target_type>) {
            if (_targets.compare_exchange_weak(need, t)) {
                t = need;
                return true;
            }
        }
        return false;
    }

    template<typename Fn>
    bool operator >> (Fn &&fn) {
        return register_target(target_callback<target_type>(std::forward<Fn>(fn)));
    }


    ///Initialize the future object from return value of a function
    /**
     * @param fn function which's return value is used to initialize the future.
     * The future must be either dormant or resolved
     *
     * @code
     * Future<T> f;
     * f << [&]{return a_function_returning_future(arg1,arg2,arg3);};
     * @endcode
     */

    template<invocable_with_result<future<T> > Fn>
    void operator << (Fn &&fn) {
        std::destroy_at(this);
        try {
            new(this) future(fn());
        } catch (...) {
            new(this) future(std::current_exception());
        }
    }

    ///Retrieve exception pointer if the future has exception
    /**
     * @return exception pointer od nullptr if there is no exception recorded
     */
    std::exception_ptr get_exception_ptr() const noexcept {
        return _state != exception?_exception:nullptr;
    }

    ///helps with awaiting on resolve()
    template<bool expected>
    class ready_state_awaiter {
    public:
        ready_state_awaiter(future &owner):_owner(owner) {};
        ready_state_awaiter(const ready_state_awaiter &) = default;
        ready_state_awaiter &operator=(const ready_state_awaiter &) = delete;

        bool await_ready() const {
            return !_owner.is_pending();
        }
        bool await_suspend(std::coroutine_handle<> h)  {
            target_coroutine(_target, h);
            return _owner.register_target_async(_target);
        }
        bool await_resume() const {
            return _owner.has_value() == expected;
        }
        ready_state_awaiter<!expected> operator !() const {
            return ready_state_awaiter<!expected>(_owner);
        }
        operator bool() const {
            _owner.wait();
            return (_owner._state != nothing) == expected;
        }

    protected:
        future &_owner;
        target_type _target;

    };

    ///Generic awaiter
    class value_awaiter: public ready_state_awaiter<true> {
    public:
        using ready_state_awaiter<true>::ready_state_awaiter;
        ret_type await_resume() const {
            return this->_owner.get_internal();
        }
    };

    ///Retrieves awaitable object which resumes a coroutine once the future is resolved
    /**
     * The awaitable object can't throw exception. It eventually returns has_value() state
     *
     * @code
     * Future<T> f = [&](auto promise) {...}
     * bool is_broken_promise = !co_await f.resolve();
     * @endcode
     *
     * @retval true future is resolved by value or exception
     * @retval false broken promise
     */
    auto has_value() {return ready_state_awaiter<true>(*this);}

    auto operator!() {return ready_state_awaiter<false>(*this);}


    ///Retrieves awaitable object to await and retrieve a value (or exception)
    /**
     *
     * @code
     * Future<T> f = [&](auto promise) {...}
     * T result = co_await f;
     * @endcode
     *
     */
    value_awaiter operator co_await() {
        return value_awaiter(*this);
    }


    /// visit function
    /**
     * @param fn lambda function (template). The type depends on state: T for value, std::exception_ptr for exception, and nullptr_t for no-value
    */
    template<typename Fn>
    auto visit(Fn fn) {
        switch (_state) {
            default: return fn(nullptr);break;
            case value:
                if constexpr(is_rvalue_ref) {
                    return fn(std::move(_value));
                } else if constexpr(is_lvalue_ref) {
                    return fn(*_value);
                } else {
                    return fn(_value);
                }
                break;
            case exception: return fn(_exception);break;
        }
    }
    ///Forward value of the future to next promise
    /**
     * @param p promise to forward
     * @return result of promise set operation
     * @note doesn't throw exception. Possible exception is forwarded to the promise
     */
    template<std::convertible_to<T> X>
    auto forward(::coro::promise<X> &&p) noexcept {
        return visit([&](auto x) {
            if constexpr(std::is_null_pointer_v<decltype(x)>) {
                return p.drop();
            } else if constexpr(std::is_same_v<std::decay_t<decltype(x)>, std::exception_ptr>) {
                return p.reject(x);
            } else {
                return p(std::forward<X>(x));
            }
        });
    }


protected:

    enum StorageState {
        nothing,
        value,
        exception
    };

    StorageState _state = nothing;
    union {
        StorageType _value;
        std::exception_ptr _exception;
    };
    std::atomic<const target_type *> _targets = {&disabled_target<target_type>};

    std::conditional_t<is_lvalue_ref, value_type *, StorageType &&> from_construct_type(construct_type &&t) {
        if constexpr(is_lvalue_ref) {
            return &t;
        } else {
            return std::move(t);
        }
    }

    ///clean future state, do not reset (ideal for destructor)
    void cleanup() {
        if (is_pending()) {
            throw std::runtime_error("Destructor or cleanup() called on pending Future");
        }
        clear_storage();
    }

    ///reset state into initial state
    void reset() {
        cleanup();
        _state = nothing;
    }

    ///clear stored value, but do not resolve the future
    void drop() {
        clear_storage();
        _state = nothing;
    }

    template<typename ... Args>
    void set_value(Args && ... args) {
        _state = value;
        if constexpr(is_lvalue_ref) {
            static_assert(sizeof...(args) == 1, "Only one argument is allowed in reference mode");
            static constexpr auto get_ptr  = [](auto &x) {
                return &x;
            };
            std::construct_at(&_value, get_ptr(args...));
        } else {
            std::construct_at(&_value, std::forward<Args>(args)...);
        }
    }

    void set_exception(std::exception_ptr e) {
        _state = exception;
        std::construct_at(&_exception, std::move(e));
    }

    std::coroutine_handle<> notify_targets() {
        auto n = _targets.exchange(&disabled_target<target_type>);
        return n?notify_targets(n, this):nullptr;
    }

    static std::coroutine_handle<> notify_targets(const target_type *list, future *f) {
        if (list->next) {
            auto c1 = notify_targets(list->next, f);
            auto c2 = list->activate(f);
            if (c1) {
                if (c2) {
                    c1.resume();
                    return c2;
                }
                return c1;
            }
            return c2;
        } else {
            return list->activate(f);
        }
    }

    void notify_resume() {
        auto x = notify_targets();
        if (x) x.resume();
    }

    ret_type get_internal() {
        switch (_state) {
            default: throw broken_promise_exception();
            case exception: std::rethrow_exception(_exception);
            case value:
                if constexpr(is_void) {
                    return;
                } else if constexpr(is_rvalue_ref) {
                    return std::move(_value);
                } else if constexpr(is_lvalue_ref) {
                    return *_value;
                } else {
                    return _value;
                }
        }
    }


    void clear_storage() {
        switch (_state) {
            default:break;
            case value: std::destroy_at(&_value);break;
            case exception: std::destroy_at(&_exception);break;
        }
    }

    friend class ::coro::promise<T>;
    friend struct async_promise_base<T>;
    friend class lazy_future<T>;


};

template<typename T>
class lazy_future: public future_tag {
public:

    using promise =typename future<T>::promise;
    using target_type = typename future<T>::target_type;
    using unique_target_type = typename future<T>::unique_target_type;
    using voidless_type = typename future<T>::voidless_type;
    using construct_type = typename future<T>::construct_type;

    using promise_target_type = target<promise &&>;
    using unique_promise_target_type = unique_target<promise_target_type>;
    using ret_type = typename future<T>::ret_type;
    using cast_ret_type = typename future<T>::cast_ret_type;

    using promise_type = async_promise_type<T>;

    lazy_future() = default;
    lazy_future(promise_target_type &t):_lazy_target(&t) {};
    lazy_future(unique_promise_target_type t):_lazy_target(t.release()) {}

    template<invocable_with_result<void, promise> Fn>
    lazy_future(Fn &&fn):_lazy_target(target_callback<promise_target_type>(std::forward<Fn>(fn))) {}
    template<invocable_with_result<lazy_future> Fn>
    lazy_future(Fn &&fn):lazy_future(fn()) {}

    lazy_future(voidless_type val):_base(std::move(val)) {}
    lazy_future(std::exception e):_base(std::move(e)) {}

    lazy_future(lazy_future &&other):_base([&]{return move_if_not_pending(other);})
                                , _lazy_target(other._lazy_target.exchange(nullptr, std::memory_order_relaxed)) {}
    lazy_future &operator=(lazy_future &&other) {
        if (this != &other) {
            _base<<([&]{return move_if_not_pending(other);});
            _lazy_target.store(other._lazy_target.exchange(nullptr, std::memory_order_relaxed));
        }
        return *this;
    }

    ~lazy_future() {
        const promise_target_type *v = _lazy_target.exchange(nullptr, std::memory_order_relaxed);
        if (v) v->activate_resume(promise());
    }
    bool is_deferred() const {return _lazy_target.load(std::memory_order_relaxed) != nullptr;}
    bool is_pending() const {return is_deferred() || _base.is_pending();}

    promise get_promise() {return _base.get_promise();}

    bool evaluate() {
        const promise_target_type *v = _lazy_target.exchange(nullptr, std::memory_order_relaxed);
        if (v) {
            v->activate_resume(get_promise());
            return true;
        }
        return false;

    }
    void wait() noexcept {
        evaluate();
        _base.wait();
    }
    ret_type get() {
        evaluate();
        return _base.get();
    }

    operator cast_ret_type() {
        evaluate();
        return _base.operator cast_ret_type();
    }
    operator future<T>() {
        const promise_target_type *v = _lazy_target.exchange(nullptr, std::memory_order_relaxed);
        if (v) {
            return [&](auto promise) {
                v->activate_resume(std::move(promise));
            };
        }
        return future<T>();
    }
    operator future<T> & () & {
        const promise_target_type *v = _lazy_target.exchange(nullptr, std::memory_order_relaxed);
        if (v) {
            v->activate_resume(get_promise());
        }
        return _base;
    }

    bool register_target_async(target_type &t) {
        const promise_target_type *pt = _lazy_target.exchange(nullptr);
        if (pt) {
            auto prom = _base.get_promise();
            _base.register_target_async(t);
            pt->activate_resume(std::move(prom));
            return true;
        } else {
            return _base.register_target_async(t);
        }

    }
    bool register_target(target_type &t) {
        if (register_target_async(t)) return true;
        t.activate_resume(&_base);
        return false;

    }
    bool register_target_async(unique_target_type t) {
        auto ptr = t.release();
        if (register_target_async(ptr)) return true;
        delete ptr;
        return false;
    }
    bool register_target(unique_target_type t) {
        return register_target(t.release());
    }

    template<typename Fn>
    bool operator >> (Fn &&fn) {
        return register_target(target_callback<target_type>(std::forward<Fn>(fn)));
    }

    template<bool expected>
    class ready_state_awaiter {
    public:
        ready_state_awaiter(lazy_future &owner):_owner(owner) {};
        ready_state_awaiter(const ready_state_awaiter &) = default;
        ready_state_awaiter &operator=(const ready_state_awaiter &) = delete;

        bool await_ready() const {
            return !_owner.is_pending();
        }
        std::coroutine_handle<> await_suspend(std::coroutine_handle<> h)  {
            auto x = _owner.start_evaluation_coro();
            target_coroutine(_target, h);
            if (!_owner._base.register_target_async(_target)) return h;
            return x;
        }
        bool await_resume() const {
            return _owner._base.has_value() == expected;
        }

        auto operator!() const {return ready_state_awaiter<!expected>(_owner);}

        operator bool() const {
            _owner.wait();
            return _owner._base.has_value() == expected;
        }

    protected:
        lazy_future &_owner;
        target_type _target;
    };


    class value_awaiter: public ready_state_awaiter<true> {
    public:
        using ready_state_awaiter<true>::ready_state_awaiter;
        ret_type await_resume() const {
            return this->_owner._base.get_internal();
        }
    };

    ///Retrieves awaitable object which resumes a coroutine once the future is resolved
    /**
     * The awaitable object can't throw exception. It eventually returns has_value() state
     *
     * @code
     * Future<T> f = [&](auto promise) {...}
     * bool is_broken_promise = !co_await f.resolve();
     * @endcode
     *
     * @retval true future is resolved by value or exception
     * @retval false broken promise
     */
    auto has_value() {return ready_state_awaiter<true>(*this);}


    ///Retrieves awaitable object to await and retrieve a value (or exception)
    /**
     *
     * @code
     * Future<T> f = [&](auto promise) {...}
     * T result = co_await f;
     * @endcode
     *
     */
    value_awaiter operator co_await() {
        return value_awaiter(*this);
    }

    auto operator!(){return ready_state_awaiter<false>(*this);}

    /// visit function
    /**
     * @param fn lambda function (template). The type depends on state: T for value, std::exception_ptr for exception, and nullptr_t for no-value
    */
    template<typename Fn>
    auto visit(Fn &&fn) {
        return _base.visit(std::forward<Fn>(fn));
    }


    template<std::convertible_to<T> X>
    auto forward(typename future<X>::promise &&p) noexcept {
        return _base.forward(p);
    }

    ///Converts pointer on future to pointer to lazy_future
    /**
     * Only way how to get pointer to lazy_future from future (as there is no
     * direct inheritance). Targets used by lazy_future are receiving future, so
     * you need to call this function to retrieve pointer to lazy_future
     *
     * @param fut pointer to future<> object which is result of resolution of lazy_future<>
     * @return pointer to associated lazy_future<>
     */
    static lazy_future *from_future(future<T> *fut) {
        return reinterpret_cast<lazy_future *>(
                reinterpret_cast<char *>(fut) - offsetof(lazy_future, _base));
    }

protected:

    future<T> _base;
    std::atomic<const promise_target_type *> _lazy_target = {};

    static future<T> move_if_not_pending(lazy_future<T> &other) {
        if (other._base.is_pending()) throw already_pending_exception();
        else return other.visit([](auto &&val){
           using Val = decltype(val);
           using DecayVal = std::decay_t<Val>;
           if constexpr(std::is_null_pointer_v<DecayVal>) {
               return future<T>();
           } else {
               return future<T>(std::forward<Val>(val));
           }
        });
    }


    std::coroutine_handle<> start_evaluation_coro() {
        const promise_target_type *t = _lazy_target.exchange(nullptr);
        if (t) {
            auto r = t->activate(_base.get_promise());
            if (!r) r = std::noop_coroutine();
            return r;
        } else {
            return std::noop_coroutine();
        }
    }


};

template<typename X>
concept pending_notify = requires(X x) {
    {x.drop()};
    {X(x.release())};
    {*x.release()};
};

template<auto defval>
class promise_with_default: public promise<std::decay_t<decltype(defval) > > {
public:
    using super = promise<std::decay_t<decltype(defval) > >;
    using promise<std::decay_t<decltype(defval) > >::promise;

    ~promise_with_default() {
        if (*this) {
            (*this)(defval);
        }
    }
};

///construct awaiter as combination future and its awaiter
/**
 * @tparam Future source future
 *
 * It is useful to return this as result of operator co_await
 */
template<typename Future>
class awaiter: public Future, public Future::value_awaiter {
public:

    template<typename ... Args>
    requires(std::constructible_from<Future, Args...>)
    awaiter(Args && ... args)
        :Future(std::forward<Args>(args)...)
        ,Future::value_awaiter(*static_cast<Future *>(this)) {}

};

class any_promise {
public:
    any_promise() = default;
    any_promise(const any_promise &) = delete;
    any_promise &operator=(const any_promise &) = delete;

    template<typename T>
    static void deleter_fn(void *ptr) {
        auto *p = reinterpret_cast<promise<T> *>(ptr);
        std::destroy_at(p);
    }

    template<typename T>
    any_promise &operator=(promise<T> &&prom) {
        if (deleter) deleter(_space);
        static_assert(sizeof(promise<T>) <= sizeof(_space));
        std::construct_at(reinterpret_cast<promise<T> *>(_space), std::move(prom));
        deleter = &deleter_fn<T>;
        return *this;
    }

    template<typename T>
    promise<T> &as() & {
        if (&deleter_fn<T> != deleter) throw std::logic_error("coro::any_promise - type missmatch");
        auto *p = reinterpret_cast<promise<T> *>(_space);
        return *p;
    }

    template<typename T>
    promise<T> as() && {
        if (&deleter_fn<T> != deleter) return promise<T>();
        auto *p = reinterpret_cast<promise<T> *>(_space);
        return std::move(*p);
    }


protected:
    char _space[sizeof(coro::promise<int>)];
    void (*deleter)(void *ptr) = nullptr;
};

template<typename ... Args>
class variant_future: public std::variant<future<Args>...> {
public:

    template<typename T>
    future<T> &as() {
        if (!std::holds_alternative<future<T> >(*this)) {
            this->template emplace<future<T> >();
        }
        return std::get<future<T> >(*this);
    }
};
template<typename ... Args>
class variant_lazy_future: public std::variant<lazy_future<Args>...> {
public:

    template<typename T>
    future<T> &as() {
        if (!std::holds_alternative<lazy_future<T> >(*this)) {
            this->template emplace<lazy_future<T> >();
        }
        return std::get<lazy_future<T> >(*this);
    }
    template<typename T>
    operator future<T> &() {
        return as<T>();
    }
};



}
