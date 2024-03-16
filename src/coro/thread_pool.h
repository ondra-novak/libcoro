#pragma once
#include "function.h"
#include "prepared_coro.h"

#include <condition_variable>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace coro {

///thread pool implementation
class thread_pool {

    class abstract_unblocker {
    public:
        abstract_unblocker() = default;
        abstract_unblocker(const abstract_unblocker &) = delete;
        abstract_unblocker &operator=(const abstract_unblocker &) = delete;

        virtual ~abstract_unblocker() = default;
        virtual void unblock(bool running) = 0;
    };

    template<typename Fn>
    class unblocker: public abstract_unblocker {
    public:
        unblocker(Fn &&fn, thread_pool &owner):_owner(owner), _fn(std::forward<Fn>(fn)) {
            _owner.set_unblock(this);
        }
        virtual void unblock(bool running) {_fn(running);}
        virtual ~unblocker() {
            _owner.unset_unblock(this);
        }
    protected:
        thread_pool &_owner;
        Fn _fn;
    };


public:


    ///construct thread pool
    /**
     * @param threads maximum thread count
     *
     * @note treads are not created immediately. They are started with requests
     */
    thread_pool(unsigned int threads):_to_start(threads) {
        _thr.reserve(threads);
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
            for (auto &blk: _unblk) blk->unblock(false);
            _unblk.clear();
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
    ~thread_pool() {
        stop();
    }

    ///co_await support (never ready)
    static constexpr bool await_ready() noexcept {return false;}
    ///co_await support (nothing returned)
    /**
     * @exception await_canceled_exception - thread pool has been stopped
     */
    void await_resume()  {
        if (is_stopped()) throw await_canceled_exception();
    }
    ///co_await support - resumes the coroutine inside of thread_pool
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

    ///sync.point - blocks thread until all enqueued functions are finished
    void join() {
        long cnt;
        {
            std::lock_guard lk(_mx);
            cnt = _enqueued;
        }
        long finished = _finished.load(std::memory_order_acquire);
        while (finished-cnt < 0) {
            _finished.wait(finished);
            finished = _finished.load(std::memory_order_acquire);
        }
    }

    ///test whether is stopped
    bool is_stopped() const {
        std::lock_guard lk(_mx);
        return _stop;
    }

    ///install unblock function
    /**
     * Unblock function is a custom function which is called, when thread pool needs
     * to unblock a managed thread, which is waiting on blocking operation (for example
     * a scheduler, which sleeps until time point is reached).
     *
     * @param fn function which is called to unblock current thread.
     * @return an object (no-copy, no-move) which must be held while unblock function
     * is active. To uninstall the funciton, destroy returned object
     */
    template<std::invocable<bool> Fn>
    [[nodiscard]] auto set_unblock_callback(Fn &&fn) {
        return unblocker<Fn>(std::forward<Fn>(fn), *this);
    }


    static thread_pool *current() {return _current;}

protected:


    std::vector<std::thread> _thr;
    mutable std::mutex _mx;
    std::condition_variable _cond;
    std::queue<function<void()> > _que;
    std::vector<abstract_unblocker *> _unblk;
    std::atomic<long> _finished = {0};
    long _enqueued = 0;
    unsigned int _to_start = 0;
    unsigned int _waiting = 0;
    bool _stop = false;

    static thread_local thread_pool *_current;


    void notify(std::unique_lock<std::mutex> &mx) {
        if (_to_start) {
            _thr.emplace_back([](thread_pool *me){me->worker();}, this);
            --_to_start;
            mx.unlock();
        } else if (_waiting){
            mx.unlock();
            _cond.notify_one();
        } else if (!_unblk.empty()) {
            auto x = _unblk.back();
            _unblk.pop_back();
            mx.unlock();
            x->unblock(true);
        }
    }

    void worker() {
        _current = this;
        std::unique_lock lk(_mx);
        while (!_stop) {
            if (_que.empty()) {
                ++_waiting;
                _cond.wait(lk);
                --_waiting;
            } else {
                {
                    auto fn = std::move(_que.front());
                    _que.pop();
                    lk.unlock();
                    fn();
                    if (_current != this) return;
                }
                _finished.fetch_add(1, std::memory_order_release);
                _finished.notify_all();
                lk.lock();
            }
        }
    }

    void set_unblock(abstract_unblocker *x) {
        std::lock_guard lk(_mx);
        if (_stop) {
            x->unblock(false);
            return;
        }
        if (_waiting == 0 && !_que.empty()) {
            x->unblock(true);
            return;
        }
        _unblk.push_back(x);
    }
    void unset_unblock(abstract_unblocker *x) {
        std::lock_guard lk(_mx);
        auto iter = std::remove(_unblk.begin(), _unblk.end(), x);
        _unblk.erase(iter, _unblk.end());
    }
};

inline thread_local thread_pool *thread_pool::_current = nullptr;


}




