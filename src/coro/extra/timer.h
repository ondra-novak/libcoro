#pragma once
#include "../future.h"


#include <chrono>
#include <vector>
#include <condition_variable>

namespace coro {

template<typename TimeT = std::chrono::system_clock , typename ID = const void *>
class timer_base {
public:

    using promise = future<void>::promise;

    using time_point = typename TimeT::time_point;

    ///generate sleeping future which is resolved at given time
    /**
     * @param tp time point
     * @param id optional identification
     * @return future
     */
    future<void> sleep_until(time_point tp, ID id = {}) {
        return [&](auto p) {
            sleep_until(p,tp,id);
        };
    }

    void sleep_until(promise &prom, time_point tp, ID id = {}) {
        _slots.push_back(Slot{tp,std::move(prom),id});
        std::push_heap(_slots.begin(), _slots.end(), slot_cmp);

    }

    ///cancels sleep for specified future
    /**
     * @param id of item to remove
     * @return promise which is canceled. If the return value is
     * ignored, the future is resolved as broken_promise_exception
     */
    promise cancel_sleep(ID id) {
        auto iter = std::find_if(_slots.begin(), _slots.end(),[&](const Slot &s){return s.id = id;});
        if (iter == _slots.end()) return {};
        iter->t = time_point::max();
        std::make_heap(_slots.begin(), _slots.end(), slot_cmp);
        auto p = std::move(_slots.back().id);
        _slots.pop_back();
        return p;
    }


protected:

    struct Slot {
        time_point t;
        promise p;
        ID id;
    };

    static bool slot_cmp(const Slot &a, const Slot &b) {
        return a.t > b.t;
        //true = a moved to the end
        //false = b is moved to the end
    }

    static auto slot_cmp_remove(ID id) {
        return [&](const Slot &a, const Slot &b) {
            bool is_a = a.id == id;
            bool is_b = b.id == id;
            return (!is_a) & ((is_b) | (a.t > b.t));
        };
    }

    std::vector<Slot> _slots;

    auto set_time(time_point tp) {
        if (_slots.empty() || _slots.front().t > tp) return std::pair(promise(), tp);
        Slot &x = _slots.front();
        auto ret = pair(std::move(x.p), std::move(x.t));
        std::pop_heap(_slots.begin(), _slots.end(), slot_cmp);
        _slots.pop_back();
        return ret;
    }
};

template<typename TimeT = std::chrono::system_clock, typename ID = const void *>
class manual_timer: public timer_base<TimeT, ID> {
public:

    using time_point = typename timer_base<TimeT, ID>::time_point;
    using promise = typename timer_base<TimeT, ID>::promise;

    template<typename Dur>
    future<void> sleep_for(Dur dur, ID id = {}) {
        return this->sleep_until(now()+dur, id);
    }
    future<void> sleep_until(time_point tm, ID id = {}) {
        return timer_base<TimeT, ID>::sleep_until(std::max(tm, _cur_time), id);
    }

    time_point now() const {
        return _cur_time;
    }

    promise set_time(time_point tp) {
        auto slt = timer_base<TimeT, ID>::set_time(tp);
        if (!slt.first) return {};
        _cur_time = slt.second;
        return slt.first;
    }

protected:
    time_point _cur_time;
};

template<typename ID = const void *>
class realtime_timer: public timer_base<std::chrono::system_clock,ID> {
public:


    using TimeT = std::chrono::system_clock;
    using time_point = typename timer_base<TimeT, ID>::time_point;

    template<typename Dur>
    future<void> sleep_for(Dur dur, ID id = {}) {
        return sleep_until(now()+dur, id);
    }

    future<void> sleep_until(TimeT tm, ID id = {}) {
        return [&](auto promise) {
            {
                std::lock_guard lk(_mx);
                timer_base<TimeT, ID>::sleep_until(promise,tm, id);
            }
            _cond.notify_one();
        };
    }

    time_point now() const {
        return TimeT::now();
    }

    void run() {
        std::lock_guard _(_mx);
        _exit_signal = false;
        worker();
    }

    void stop() {
        std::lock_guard _(_mx);
        _exit_signal = true;
    }

    auto run_thread() {
        std::lock_guard _(_mx);
        _exit_signal = false;
        return std::thread([&]{
            worker();
        });
    }

    template<typename X>
    bool run_until(future<X> &fut) {
        _exit_signal = false;
        auto &t = _target.template as<future<X>::target_type>();
        target_member_fn<&realtime_timer::stop_target>(t, this);
        if (!fut.register_target_async(t)) return false;
        worker();
        return true;
    }

protected:
    std::mutex _mx;
    std::condition_variable _cond;
    any_target _target;

    bool _exit_signal = false;
    void worker() {
        std::unique_lock lk(_mx);
        while (!_exit_signal) {
            if (this->_slots.empty()) {
                _cond.wait(lk);
            } else {
                auto tp = this->_slots.front().t;
                _cond.wait_until(lk,tp);
                auto prom = this->set_time(now());
                if (prom) {
                    lk.unlock();
                    prom();
                    lk.lock();
                }
            }
        }
    }
};

}
