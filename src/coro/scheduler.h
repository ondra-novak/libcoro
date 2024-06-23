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
 * @tparam CondVar custom condition variable implementation - compatible API with
 *  std::condition_variable
 *
 */
template<typename CondVar>
class scheduler_t {
public:

    using notify_t = promise<void>::notify;

    ///A function responsible to resume scheduled coroutine
    using resume_cb = function<void(notify_t)>;

    using promise_t = promise<void>;
    using ident_t = const void *;

    ///initialize the scheduler. It is not started, you must start it manually
    /**
     * The scheduler in this state can be used for scheduling, however no awaiting
     * coroutine is awaken until the scheduler is started
     *
     * @see start, run
     */
    scheduler_t() = default;

    ///initialize the scheduler. It is not started, you must start it manually
    /**
     * The scheduler in this state can be used for scheduling, however no awaiting
     * coroutine is awaken until the scheduler is started
     *
     * @param cfg CondVar configuration
     *
     * @see start, run
     */
    template<std::convertible_to<CondVar> CondVarCfg>
    explicit scheduler_t(CondVarCfg &&cfg):_cond(std::forward<CondVarCfg>(cfg)) {}

    ///stop and destroy scheduler
    ~scheduler_t(){
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
            if (std::find(_blk.begin(), _blk.end(), ident) != _blk.end()) return ;
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




    ///run the scheduler in signle thread mode, stop when future is ready
    /**
     * @param fut reference to future, which is awaited
     * @param cb callback function which is responsible to resume expired items.
     * This argument is optional. You can specify own function, or you can use
     * coro::scheduler::thread_pool() function to connect a thread pool for
     * this feature. If not specified, expired items are resumed in the
     * current thread
     *
     * @return return value of the future
     */
    template<typename T, std::invocable<notify_t> ResumeCB>
    decltype(auto) run(future<T> &fut, ResumeCB &&cb) {
        bool stopflag = false;
        if (fut.await_ready() || !fut.set_callback([this,&stopflag]{
            std::lock_guard _(_mx);
            stopflag = true;
            notify();
        })) {
            return fut.await_resume();
        }
        auto cur = _current;
        trace::add_link(&fut, nullptr, sizeof(fut));
        worker(std::forward<ResumeCB>(cb), stopflag);
        _current = cur;
        return fut.await_resume();
    }

    ///run the scheduler in signle thread mode, stop when future is ready
    /**
     * @param fut reference to future, which is awaited
     * @param cb callback function which is responsible to resume expired items.
     * This argument is optional. You can specify own function, or you can use
     * coro::scheduler::thread_pool() function to connect a thread pool for
     * this feature. If not specified, expired items are resumed in the
     * current thread
     *
     * @return return value of the future
     */
    template<typename T, std::invocable<notify_t> ResumeCB>
    decltype(auto) run(future<T> &&fut, ResumeCB &&cb) {
        return run(fut, std::forward<ResumeCB>(cb));
    }

    ///run the scheduler in signle thread mode, stop when future is ready
    /**
     * @param fut reference to future, which is awaited
     *
     * @return return value of the future
     */
    template<typename T>
    decltype(auto) run(future<T> &fut) {
        return run(fut,[](notify_t){});
    }

    ///run the scheduler in signle thread mode, stop when future is ready
    /**
     * @param fut reference to future, which is awaited
     *
     * @return return value of the future
     */
    template<typename T>
    decltype(auto) run(future<T> &&fut) {
        return run(fut);
    }

    using thread_pool_t = coro::thread_pool;

    ///Create a thread pool for the scheduler
    /**
     * @param threads count of threads
     * @return function object, which can be pased to run() or start()
     * @see run, start
     */
    static auto thread_pool(unsigned int threads) {
        return [thr = std::make_shared<thread_pool_t>(threads)](notify_t ntf){
            thr->enqueue(std::move(ntf));
        };
    }
    ///Attach a thread pool to the scheduler
    /**
     * @param pool shared pointer to an existing thread pool
     * @return function object, which can be pased to run() or start()
     * @see run, start
     */
    static auto thread_pool(std::shared_ptr<coro::thread_pool> pool) {
        return [pool](notify_t ntf){
            pool->enqueue(std::move(ntf));
        };
    }

    ///Start scheduler at background
    /**
     * The scheduler starts a thread, which performs scheduling. You can
     * start only one thread per scheduler's instance
     * @param resumecb callback function which is responsible to resume expired items.
     * This argument is optional. You can specify own function, or you can use
     * coro::scheduler::thread_pool() function to connect a thread pool for
     * this feature. If not specified, expired items are resumed in the
     * scheduler's thread
     * @retval true started
     * @retval false already runnning
     */
    template<std::invocable<notify_t> ResumeCB>
    bool start(ResumeCB &&resumecb) {
        if (_thr.joinable()) return false;
        _stop = false;
        _thr = std::thread([this, rcb = std::move(resumecb)]() mutable {
            worker(std::move(rcb), _stop);
        });
        return true;
    }

    ///Start scheduler at background
    /**
     * The scheduler starts a thread, which performs scheduling. You can
     * start only one thread per scheduler's instance
     * @retval true started
     * @retval false already runnning
     */
    bool start() {
        return start([](notify_t){});
    }

    ///stop the running scheduler
    void stop() {
        std::unique_lock lk(_mx);
        _stop = true;
        lk.unlock();
        if (scheduler_t::_current != this) {
            notify();
            if (_thr.joinable()) _thr.join();
        } else {
            scheduler_t::_current = nullptr;
            if (_thr.joinable()) _thr.detach();
        }
    }


    ///returns current scheduler (for the current thread)
    /**
     * Note this is not useful, if the scheduled coroutine is resumed in
     * a different thread.
     * @return
     */
    static scheduler_t * current() {return _current;}


    ///While this object is held, cancel is in effect
    class pending_cancel {
    private:
        struct deleter {
            ident_t _ident;
            void operator()(scheduler_t *sch) {
                sch->_blk.erase(std::remove(sch->_blk.begin(),sch->_blk.end(), _ident), sch->_blk.end());
            }
        };

        std::unique_ptr<scheduler_t, deleter> _ptr;

        pending_cancel(scheduler_t &sch, ident_t ident)
            :_ptr(&sch, {ident}) {};

        friend class scheduler_t;
    };

    ///Cancel scheduled operation
    /**
     * @param ident identity of the scheduled operation
     * @return an object, which must be held in order to prevent futher attempts to
     * schedule an operation under the same identity. This helps to synchronize with
     * loops.
     *
     */
    pending_cancel cancel(ident_t ident) {
        promise<void>::notify d;
        std::lock_guard lk(_mx);
        d = cancel_lk(ident);
        _blk.push_back(ident);
        return pending_cancel(*this, ident);
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
    CondVar _cond;
    std::vector<item_t> _items;
    std::vector<ident_t> _blk;
    bool _stop = false;
    static thread_local scheduler_t *_current;
    std::thread _thr;

    void notify() {
        _cond.notify_all();
    }

    template<typename ResumeCB>
    void worker(ResumeCB &&rcb, bool &stop_flag) noexcept {
        std::unique_lock lk(_mx);

        _current = this;

        while (!stop_flag) {
            auto now = std::chrono::system_clock::now();
            if (!_items.empty() && _items.front()._tp < now) {
                auto d = _items.front()._prom();
                std::pop_heap(_items.begin(),_items.end(), compare_items);
                _items.pop_back();
                lk.unlock();
                rcb(std::move(d));
                if (_current != this) return;
                lk.lock();
            } else {
                wait_next(lk);
            }
        }

        _current = nullptr;


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
///scheduler for coroutines
/**
 * Implements co_awaitable sleep_for and sleep_until
 *
 * @ingroup awaitable
 */
using scheduler = scheduler_t<std::condition_variable>;

template<typename CondVar>
inline thread_local scheduler_t<CondVar> *scheduler_t<CondVar>::_current = nullptr;

}

