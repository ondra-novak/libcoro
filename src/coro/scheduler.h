#pragma once

#include "future.h"

#include <condition_variable>
#include <mutex>
#include <queue>
#include <thread>

namespace coro {


///Implements basic pool of threads
class scheduler {
public:

    ///construct and start thread pool
    /**
     * @param count count of threads. If this argument is zero, no threads are created
     * however the object can be started as a dispatcher where the main thread
     * starts to dispatch events.
     *
     * @note default value is 1, which is most common for dispatcher-like thread pools
     */
    explicit scheduler(unsigned int count = 1) {
        _thread_list.resize(count);
        waitable_atomic<unsigned int> remain = count;

        for (auto &x: _thread_list) {
            x = std::thread([&]{
                current_instance = this;
                if (remain.fetch_sub(1, std::memory_order_relaxed) <= 1) remain.notify_all();
                worker([&]{return _running;});
            });
        }
        unsigned int r = remain.load(std::memory_order_relaxed);
        while (r > 0) {
            remain.wait(r);
            r = remain.load(std::memory_order_relaxed);
        }
    }


    ///Destructor
    /** It always stops all threads and all pending tasks are
     * canceled by breaking theirs promises.
     */
    ~scheduler() {
        auto id = std::this_thread::get_id();
        current_instance = nullptr;
        {
            std::unique_lock lk(_mx);
            _running = false;
            notify_all(lk);
        }
        for (auto &x: _thread_list) {
            if (x.get_id() == id) x.detach(); else x.join();
        }
        drop_all();
    }


    ///schedule a function call
    template<std::invocable<> Fn>
    void schedule(Fn &&fn) {
        push(make_target(std::forward<Fn>(fn)));
    }
    ///schedule a function call at given time
    /**
     * @param tp time point
     * @param fn function
     * @param id identifier, allows to cancel operation
     */
    template<std::invocable<> Fn>
    void schedule_at(std::chrono::system_clock::time_point tp, Fn &&fn, const void *id = nullptr) {
        push_at_time({make_target(std::forward<Fn>(fn)), tp, id});
    }

    template<std::invocable<> Fn, typename Dur>
    void schedule_after(Dur &&dur, Fn &&fn, const void *id = nullptr) {
        auto now = std::chrono::system_clock::now();
        push_at_time({make_target(std::forward<Fn>(fn)), now+dur, id});
    }

    bool cancel(const void *id) {
        return cancel_scheduled(id);
    }

    ///shortcut to schedule
    /**
     * @code
     * thread_pool tp;
     * tp >> []{....};
     * tp >> promise(value);
     * @endcode
     *
     */
    template<typename X>
    auto operator >> (X &&val) -> decltype(std::declval<scheduler &>().schedule(std::forward<X>(val))) {
        return schedule<X>(std::forward<X>(val));
    }


    ///Direct await on thread_pool
    struct awaiter { // @suppress("Miss copy constructor or assignment operator")
        scheduler *owner;
        std::chrono::system_clock::time_point tp;
        const void *ident;
        std::coroutine_handle<> h = {};
        bool err = false;
        constexpr bool await_ready() const {return false;}

        void await_suspend(std::coroutine_handle<> h) {
            this->h = h;
            scheduler::target_type target{[](bool ok, void *fptr)noexcept {
                auto me = reinterpret_cast<awaiter *>(fptr);
                me->err = !ok;
                me->h.resume();
            }, this};
            if (tp == std::chrono::system_clock::time_point()) {
                owner->push(target);
            } else{
                owner->push_at_time({target, tp, ident});
            }
        }
        constexpr void await_resume() const {
            if (err) throw broken_promise_exception();
        }
    };

    ///allows co_await on instance of thread_pool
    awaiter operator co_await()  {
        return awaiter{this,{},nullptr};
    }

    awaiter sleep_until(std::chrono::system_clock::time_point tp, const void *ident = nullptr) {
        return awaiter{this,tp,ident};
    }

    template<typename Dur>
    awaiter sleep_for(Dur &&dur, const void *ident = nullptr) {
        return awaiter{this,std::chrono::system_clock::now()+dur,ident};
    }

    template<typename X>
    future<X> execute(async<X> &&coro) {
        return [&](auto promise) {
            schedule([coro = std::move(coro), promise = std::move(promise)]() mutable {
                    coro.start(std::move(promise));
            });
        };
    }

    template<std::invocable<> Fn>
    auto run(Fn &&fn) {
        using ret_type = decltype(fn());
        return future<ret_type>([&](typename future<ret_type>::promise prom) {
            schedule([fn = std::move(fn), prom = std::move(prom)]() mutable {
                try {
                    if constexpr(std::is_void_v<ret_type>) {
                        fn();
                        prom();
                    } else {
                        prom(fn());
                    }
                } catch (...) {
                    prom.reject();
                }
            });
        });
    }

    ///retrieves reference to current thread pool
    static scheduler &current() {
        scheduler *x = current_instance;
        if (!x) throw no_active_scheduler();
        return *x;
    }

    static scheduler *current_ptr() {
        scheduler *x = current_instance;
        return x;
    }

    ///performs synchronous await while making thread free to process other operations
    /**
     * @param fut future which you want to await on.
     */
    template<typename X>
    void await_until_ready(future<X> &fut) {
        auto c = current_instance;
        std::atomic<bool> done = false;
        typename future<X>::target_type t;
        target_simple_activation(t, [&](future<X> *){
            done.store(true, std::memory_order_release);
            std::unique_lock lk(_mx);
            notify_all(lk);
        });
        if (!fut.register_target_async(t)) return;
        current_instance = this;
        worker([&]{return !done.load(std::memory_order_relaxed) && _running;});
        current_instance = c;;
    }

    ///performs synchronous await while making thread free to process other operations
    /**
     * @param fut future which you want to await on.
     * @return value of the future
     * @note while awaiting, the current thread is used to process any events
     * passed to the queue
     */
    template<typename X>
    auto await(future<X> &fut) {
        await_until_ready(fut);
        return fut.get();
    }

    ///performs synchronous await while making thread free to process other operations
    /**
     * @param fut future which you want to await on.
     * @return value of the future
     * @note while awaiting, the current thread is used to process any events
     * passed to the queue
     */
    template<typename X>
    auto await(future<X> &&fut) {
        await_until_ready(fut);
        return fut.get();
    }

    ///Determines, whether object is idle
    /**
     * @retval true, the object is idle, no enqueued events
     * @retval false, object is not idle, some events waiting
     * @note doesn't check scheduled events
     */
    bool is_idle() const {
        std::lock_guard _(_mx);
        return _queue.empty() || _idle_threads>0;
    }

    ///Retrives time of first scheduled event
    /**
     * This function can be important when the worker is being blocked.
     * Returned value contains time point when the blocking should end
     *
     * @return time point when the first event is scheduled. If the
     * object is not idle, returns now()
     *
     *
     */
    auto get_idle_interval() -> std::chrono::system_clock::time_point {
        std::lock_guard _(_mx);
        if (_idle_threads>0) {
            return std::chrono::system_clock::time_point::max();
        }
        if (_queue.empty()) {
            if (_scheduled.empty()) {
                return std::chrono::system_clock::time_point::max();
            } else{
                return _scheduled.front().tp;
            }
        } else {
            return std::chrono::system_clock::now();
        }
    }

    ///Definition of unblock target
    /**
     * This target is important if there is a function in a worker, which
     * can block the thread for unspecified time. It can receive a notification
     * that its thread should be unblocked. It is made by activating this target.
     * The target is activated just once.
     */
    using unblock_target = target<scheduler *>;

    ///Register unblock target
    /**
     * @param t target which is activated, when scheduler exhausted all threads and
     * need to unblock by user blocked thread. This target is activated only once, but
     * it can be reactivated in reaction to activation
     *
     * Note the reference must stay valid until the target is activated or until is
     * unregistered.
     *
     */
    auto register_unblock(unblock_target &t) {
        std::lock_guard _(_mx);
        _unblocks.push_back(&t);
    }

    ///Unregister unblock target
    /**
     * @param t target reference to target to unregister
     * @retval true success
     * @retval false no longer valid, probably already activated
     */
    bool unregister_unblock(unblock_target &t) {
        std::lock_guard _(_mx);
        auto iter = std::find(_unblocks.begin(), _unblocks.end(), &t);
        if (iter != _unblocks.end()) {
            _unblocks.erase(iter);
            return true;
        }
        return false;
    }

protected:

    struct target_type { // @suppress("Miss copy constructor or assignment operator")
        void (*fn)(bool ok, void *ptr) noexcept;
        void *user_ptr;
        void activate(bool ok) const {return fn(ok, user_ptr);}
    };

    struct scheduled_target { // @suppress("Miss copy constructor or assignment operator")
        target_type target;
        std::chrono::system_clock::time_point tp;
        const void *ident;
    };

    struct lazy_request {
        std::chrono::system_clock::time_point tp;
        const void *ident;
        lazy_request *next;
    };

    mutable std::mutex _mx;
    std::vector<std::thread> _thread_list;
    std::condition_variable _cond;
    std::queue<target_type> _queue;
    std::vector<scheduled_target> _scheduled;
    std::deque<unblock_target *> _unblocks;
    unsigned int _idle_threads = 0;
    bool _running = true;

    static thread_local scheduler *current_instance;

    template<std::invocable<> Fn>
    static target_type make_target(Fn &&fn) {
        if constexpr(pending_notify<Fn>) {
            using future_ptr = decltype(fn.release());
            auto f = fn.release();
            return target_type{[](bool ok, void *fptr) noexcept {
                Fn ntf(reinterpret_cast<future_ptr>(fptr));
                if (!ok) ntf.drop();
                //destructor handles execution
            }, f};
        } else if constexpr(sizeof(Fn) <= sizeof(void *)
                && std::is_trivially_copy_constructible_v<Fn>
                && std::is_trivially_destructible_v<Fn>) {
            target_type t;
            new(&t.user_ptr) Fn(std::forward<Fn>(fn));
            t.fn = [](bool ok, void *fptr) noexcept {
                Fn *fn = reinterpret_cast<Fn *>(&fptr);
                if (ok) {
                    (*fn)();
                }
            };
            return t;
        } else {
            auto fptr = new Fn(std::forward<Fn>(fn));
            return target_type{[](bool ok, void *fptr) noexcept {
                auto fnptr = reinterpret_cast<Fn *>(fptr);
                if (ok) (*fnptr)();
                delete fnptr;
            }, fptr};
        }
    }

    template<auto x, typename Obj = _details::extract_object_type_t<decltype(x)> >
    static target_type make_target(Obj *obj){
        return target_type{
            [](bool ok, void *ptr) noexcept {
                auto objptr = reinterpret_cast<Obj *>(ptr);
                if (ok) (objptr->*x)();
            }
        , obj};
    }


    void push(target_type t) {
        std::unique_lock lk(_mx);
        _queue.push(t);
        notify_one(lk);
    }

    static bool scheduled_cmp(const scheduled_target &a, const scheduled_target &b) {
        return a.tp > b.tp;
    }

    void push_at_time(scheduled_target t) {
        std::unique_lock lk(_mx);
        _scheduled.push_back(t);
        std::push_heap(_scheduled.begin(), _scheduled.end(), scheduled_cmp);
        notify_one(lk);
    }

    bool cancel_scheduled(const void *id) {
        std::unique_lock lk(_mx);
        auto iter = std::find_if(_scheduled.begin(), _scheduled.end(), [&](const scheduled_target &t){
            return t.ident == id;
        });
        if (iter != _scheduled.end()) {
            auto t = iter->target;
            if (iter == _scheduled.begin()) {
                std::pop_heap(_scheduled.begin(), _scheduled.end(), scheduled_cmp);
                _scheduled.pop_back();
            } else {
                std::swap(*iter, _scheduled.back());
                _scheduled.pop_back();
                std::make_heap(_scheduled.begin(), _scheduled.end(), scheduled_cmp);
            }
            lk.unlock();
            t.activate(false);
            return true;
        }
        return false;
    }

    void drop_all() {
        while (!_queue.empty()) {
            auto z = _queue.front();
            _queue.pop();
            z.activate(false);
        }
        for (auto &x: _scheduled) {
            x.target.activate(false);
        }
        _scheduled.clear();
    }

    template<typename Predicate>
    void worker(Predicate &&pred) {
        std::unique_lock lk(_mx);
        ++_idle_threads;
        while (pred()) {
            if (_queue.empty()) {
                wait(lk);
            } else {
                auto x = _queue.front();
                _queue.pop();
               --_idle_threads;
               lk.unlock();
               x.activate(true);
               if (current_instance != this) return;
               lk.lock();
               ++_idle_threads;
            }
            handle_expired();
        }
        --_idle_threads;
    }

    void handle_expired() {
        auto now = std::chrono::system_clock::now();
        while (!_scheduled.empty() && _scheduled.front().tp < now) {
            _queue.push(_scheduled.front().target);
            std::pop_heap(_scheduled.begin(),_scheduled.end(), scheduled_cmp);
            _scheduled.pop_back();
        }
    }

    void wait(std::unique_lock<std::mutex> &lk) {
        if (_scheduled.empty()) {
            _cond.wait(lk);
        } else {
            _cond.wait_until(lk, _scheduled.front().tp);
        }
    }

    void notify_one(std::unique_lock<std::mutex> &lk) {
        if (_idle_threads) {
            lk.unlock();
            _cond.notify_one();
        }
        else if (!_unblocks.empty() && current_instance != this) {
            auto t = _unblocks.front();
            _unblocks.pop_front();
            lk.unlock();
            t->activate(this);
        } else {
            lk.unlock();
        }
    }
    void notify_all(std::unique_lock<std::mutex> &lk) {
        auto q = std::move(_unblocks);
        lk.unlock();
        _cond.notify_all();
        for (const auto &t: q) {
            t->activate(this);
        }
    }

};


inline thread_local scheduler *scheduler::current_instance = nullptr;


}
