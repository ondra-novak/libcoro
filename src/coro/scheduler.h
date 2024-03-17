#pragma once

#include "function.h"
#include "future.h"
#include "thread_pool.h"

#include <mutex>
#include <chrono>
namespace coro {


///scheduler for coroutines
/**
 * Implements co_awaitable sleep_for and sleep_until
 *
 * @ingroup awaitable
 */
class scheduler {
public:

    using promise_t = promise<void>;
    using ident_t = const void *;

    ///initialize the scheduler. It is not started, you must start it manually
    /**
     * The scheduler in this state can be used for scheduling, however no awaiting
     * coroutine is awaken until the scheduler is started
     */
    scheduler() = default;

    ///initialize the scheduler to run in specified thread pool
    scheduler(thread_pool &pool) {
        start(pool);
    }
    ///stop and destroy scheduler
    ~scheduler(){
        stop();
    }


    ///sleep until specified time point
    /**
     * @param tp time point
     * @param ident optional identification. You can use any pointer as identification. The
     * pointer is dereferrenced
     * @return future which becomes resolved at specified time point. You just need to @b co_await on it
     */
    future<void> sleep_until(std::chrono::system_clock::time_point tp, ident_t ident = nullptr) {
        return [&](auto promise) {
            std::unique_lock lk(_mx);
            if (_stop|| std::find(_blk.begin(), _blk.end(), ident) != _blk.end()) return ;
            _items.push_back({std::move(promise), tp, ident});
            std::push_heap(_items.begin(), _items.end(), compare_items);
            notify();
        };
    }

    ///sleep for specified duration
    /**
     * @param dur duration
     * @param ident optional identification. You can use any pointer as identification. The
     * pointer is dereferrenced
     * @return future which becomes resolved after specified duration. You just need to @b co_await on it
     */
    template<typename A, typename B>
    future<void> sleep_for(std::chrono::duration<A,B> dur, ident_t ident = nullptr) {
        return sleep_until(std::chrono::system_clock::now()+dur, ident);
    }


    ///cancel sleep operation prematurally
    /**
     * @param ident identification of sleep operation.
     * @return deferred_notify instance allows to schedule resumption of awaiting coroutine.
     * If ignored, the coroutine is resumed at the end of the expression. The sleeping
     * coroutine receives the exception await_canceled_exception
     *
     */
    promise<void>::notify cancel(ident_t ident) {
        std::unique_lock lk(_mx);
        return cancel_lk(ident);
    }

    ///start the scheduler in single thread mode
    /** the funcion blocks until the scheduler is stopped
     *
     */
    void start() {
        if (_worker_active.exchange(true)) return;
        _stop = false;
        worker();
    }

    ///start the scheduler inside thread pool
    /**
     * @param pool thread pool
     */
    void start(thread_pool &pool) {
        if (_worker_active.exchange(true)) return;
        _stop = false;
        pool.enqueue([this]{worker();});
    }


    ///stop the running scheduler
    void stop() {
        std::unique_lock lk(_mx);
        _stop = true;
        lk.unlock();
        if (scheduler::_current != this) {
            _cond.notify_one();
            _worker_active.wait(true);
        }
    }


    static scheduler * current() {return _current;}


    ///an object which holds blocking status for specified identity
    /**
     * @see block()
     */
    class blocked {
    public:
        ~blocked() {
            _sch._blk.erase(std::remove(_sch._blk.begin(), _sch._blk.end(), _ident), _sch._blk.end());
        }
        blocked(const blocked &) = delete;
        blocked &operator=(const blocked &) = delete;
    private:
        blocked(scheduler &sch, ident_t ident):_sch(sch),_ident(ident) {
            _sch._blk.push_back(_ident);
        }
        scheduler &_sch;
        ident_t _ident;
        friend class scheduler;
    };

    ///block specified identity to be scheduled
    /**
     * @param ident identity to block.
     * @return object which must be held to block identity
     *
     * The function cancels awaiting sleep operation if any, and blocks all futher
     * sleep operations for given identity. This block is held during lifetime of
     * returned object
     *
     * Blocked identity cannot be used for scheduling and attempt to sleep with it
     * ends with the exception await_canceled_exception()
     */
    blocked block(ident_t ident) {
        promise<void>::notify d;
        std::lock_guard lk(_mx);
        d = cancel(ident);
        return blocked(*this, ident);
    }



protected:

    struct item_t {
        promise_t _prom;
        std::chrono::system_clock::time_point _tp;
        ident_t _ident;
    };

    static bool compare_items(const item_t &a, const item_t &b) {
        return a._tp > b._tp;
    }

    std::mutex _mx;
    std::condition_variable _cond;
    std::vector<item_t> _items;
    std::vector<ident_t> _blk;
    std::atomic<bool> _worker_active = {false};
    bool _stop = false;
    static thread_local scheduler *_current;

    void notify() {
        _cond.notify_one();
    }

    void worker() {
        std::unique_lock lk(_mx);

        auto tpool = thread_pool::current();
        scheduler::_current = this;

        while (!_stop) {
            auto now = std::chrono::system_clock::now();
            if (!_items.empty() && _items.front()._tp < now) {
                auto d = _items.front()._prom();
                std::pop_heap(_items.begin(),_items.end(), compare_items);
                _items.pop_back();
                if (tpool) {
                    tpool->enqueue(std::move(d));
                } else {
                    lk.unlock();
                    d.deliver();
                    lk.lock();
                }
            } else if (tpool) {
                bool unblk = false;
                {
                    lk.unlock();
                    auto blk = tpool->set_unblock_callback([&](bool running){
                        {
                            std::lock_guard _(_mx);
                            unblk = true;
                            _stop = !running;
                        }
                        _cond.notify_one();
                    });
                    lk.lock();
                    if (!unblk) wait_next(lk);
                }
                if (unblk) {
                    tpool->enqueue([this]{worker();});
                    return;
                }
            } else {
                wait_next(lk);
            }
        }

        _worker_active.store(false, std::memory_order_relaxed);
        _worker_active.notify_all();
        scheduler::_current = nullptr;
        _stop = false;

    }

    void wait_next(std::unique_lock<std::mutex> &lk) {
        if (!_items.empty()) {
            _cond.wait_until(lk, _items.front()._tp);
        } else {
            _cond.wait(lk);
        }
    }

    promise<void>::notify cancel_lk(ident_t ident) {
        auto iter = std::find_if(_items.begin(), _items.end(), [&](const item_t &item){
            return item._ident == ident;
        });
        if (iter == _items.end()) return {};
        if (iter == _items.begin()) {
            std::pop_heap(_items.begin(), _items.end(), compare_items);
            auto d = _items.back()._prom.cancel();
            _items.pop_back();
            return d;
        } else {
            auto d = iter->_prom.cancel();
            item_t &x = *iter;
            if (&x != &_items.back()) {
                std::swap(x, _items.back());
                std::make_heap(_items.begin(), _items.end(),  compare_items);
            }
            _items.pop_back();
            return d;
        }
    }


};

inline thread_local scheduler *scheduler::_current = nullptr;

}

