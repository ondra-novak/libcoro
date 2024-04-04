#pragma once
#include "function.h"
#include "prepared_coro.h"
#include "exceptions.h"
#include "future.h"
#include <condition_variable>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace coro {

///thread pool implementation
/**
 *
 * This is generic thread pool with custom condition variable
 *
 * @tparam CondVar implementation of condition variable. It must be compatible
 * with std::condition_variable. Custom condition variable allows implement more
 * featureful managment of idle threads
 *
 *
 * @see thread_pool
 */
template<typename CondVar>
class thread_pool_t {
public:


    ///construct thread pool
    /**
     * @param threads maximum thread count
     *
     * @note treads are not created immediately. They are started with requests
     */
    thread_pool_t(unsigned int threads):_to_start(threads) {
        _thr.reserve(threads);
    }

    template<std::convertible_to<CondVar> CondVarInit>
    thread_pool_t(unsigned int threads, CondVarInit &&cinit)
        :_to_start(threads),_cond(std::forward<CondVarInit>(cinit)) {

    }

    ///enqueue function
    /**
     * @param fn function to enqueue
     * @retval true success
     * @retval false called when thread pool is stopped
     */
    template<std::invocable<> Fn>
    bool enqueue(Fn &&fn) {
        std::unique_lock lk(_mx);
        if (_stop) return false;
        _que.emplace(std::forward<Fn>(fn));
        ++_enqueued;
        notify(lk);
        return true;
    }

    ///stop thread pool
    /**
     * Can be called even if the thread_pool is already stopped
     */
    void stop() {
        std::vector<std::thread> tmp;
        {
            std::lock_guard lk(_mx);
            if (_stop) return;
            _stop = true;
            _to_start = 0;
            std::swap(_thr, tmp);
        }
        _cond.notify_all();
        auto this_thr = std::this_thread::get_id();
        for (auto &t: tmp) {
            if (this_thr == t.get_id()) {
                _current = nullptr;
                t.detach();
            } else {
                t.join();
            }
        }
        _que = {};

    }

    ///dtor
    ~thread_pool_t() {
        stop();
    }

    ///@b co_await support (never ready)
    static constexpr bool await_ready() noexcept {return false;}
    ///@b co_await support (nothing returned)
    /**
     * @exception await_canceled_exception - thread pool has been stopped
     */
    void await_resume()  {
        if (is_stopped()) throw await_canceled_exception();
    }
    ///@b co_await support - resumes the coroutine inside of thread_pool
    /**
     * @param h handle of suspended coroutine
     *
     * @exception await_canceled_exception - thread pool has been stopped
     */
    void await_suspend(std::coroutine_handle<> h) {
        prepared_coro prep(h);
        if (!enqueue(std::move(prep))) {
            prep.release();
            throw await_canceled_exception();
        }
    }

    ///wait to process all enqueued tasks
    /**
     * @return future is resolved, once all tasks enqueued previously are finished
     *
     * @note tasks enqueued after this call are not counted
     */
    future<void> join() {
        return [&](auto promise) {
            std::lock_guard lk(_mx);
            if (_enqueued == _finished) {
                promise();
            } else {
                _joins.push_back({_enqueued, std::move(promise)});
                std::push_heap(_joins.begin(), _joins.end());
            }
        };
    }

    ///test whether is stopped
    bool is_stopped() const {
        std::lock_guard lk(_mx);
        return _stop;
    }
    ///returns current thread pool (in context of managed thread)
    static thread_pool_t *current() {return _current;}

protected:


    std::vector<std::thread> _thr;
    mutable std::mutex _mx;
    CondVar _cond;
    std::queue<function<void()> > _que;
    long _finished = 0;
    long _enqueued = 0;
    unsigned int _to_start = 0;
    bool _stop = false;

    struct join_info {
        long _target;
        promise<void> _prom;
        int operator<=>(const join_info &other) const {
            return other._target - _target;
        }
    };

    std::vector<join_info> _joins;

    static thread_local thread_pool_t *_current;

    void check_join(std::unique_lock<std::mutex> &lk) {
        promise<void> joined;
        lk.lock();
        ++_finished;
        if (_joins.empty() || _joins.front()._target > _finished) return;
        do {
            joined += _joins.front()._prom;
            std::pop_heap(_joins.begin(), _joins.end());
            _joins.pop_back();
        } while (!_joins.empty() && _joins.front()._target > _finished);
        lk.unlock();
        joined();
        lk.lock();
    }

    void notify(std::unique_lock<std::mutex> &mx) {
        if (_to_start) {
            _thr.emplace_back([](thread_pool_t *me){me->worker();}, this);
            --_to_start;
            mx.unlock();
        } else {
            mx.unlock();
            _cond.notify_one();
        }
    }

    void worker() {
        _current = this;
        std::unique_lock lk(_mx);
        while (!_stop) {
            if (_que.empty()) {
                _cond.wait(lk);
            } else {
                {
                    auto fn = std::move(_que.front());
                    _que.pop();
                    lk.unlock();
                    fn();
                    if (_current != this) return;
                }
                check_join(lk);

            }
        }
    }

};

template<typename CondVar>
inline thread_local thread_pool_t<CondVar> *thread_pool_t<CondVar>::_current = nullptr;


///Thread pool implementation
/**
 * @ingroup awaitable
 *
 * This is alias to thread_pool_t with std::condition_variable as condition
 * variable.
 *
 */
using thread_pool = thread_pool_t<std::condition_variable>;

}




