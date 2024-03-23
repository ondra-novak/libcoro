#pragma once
#include "future.h"

#include <mutex>
#include <queue>

namespace coro {

///Implements semaphore for coroutines
/**
 * @note use coro::mutex if you need mutex behaviour, as the coro::mutex has better
 * performance
 */
class semaphore {
public:

    ///construct semaphore with counter set to zero
    /**
     * The first awaiting coroutin
     */
    semaphore() = default;
    semaphore(unsigned long counter): _counter (counter) {}

    ///acquire the semaphore
    /**
     * Decrease the counter.
     * @retval resolved future is resolved, if counter was above zero
     * @retval pending future is pending, if counter was (and is) zero. The future
     * can be set to resolved by release()
     *
     */
    coro::future<void> acquire() {
        return [&](auto promise) {
            std::lock_guard _(_mx);
            if (_counter) {
                --_counter;
                promise();
            } else {
                _waiting.push(std::move(promise));
            }
        };
    }

    ///Returns awaiter, so coroutine can co_await on it
    /**
     * @copydoc acquire
     */
    coro::future<void> operator co_await() {
        return acquire();
    }

    ///Release the semaphore
    /**
     * Resumes first awaiting coroutine. If there is no coroutine, increases counter
     * @return notify object associated with the awaiting future. You can use it
     * to choose perfect place to perform resumption. If ignored, released coroutine
     * is resumed immediatelly
     */
    coro::promise<void>::notify release() {
        coro::promise<void>::notify r;
        std::lock_guard _(_mx);
        if (_waiting.empty()) {
            ++_counter;
        } else {
             r = _waiting.front()();
            _waiting.pop();
        }
        return r;
    }


    ///Retrieve counter
    /**
     * @retval >0 semaphore is opened
     * @retval =0 semaphore is closed, acquire will block
     * @retval <0 semaphore is closed, and there are awaiting coroutine, the
     * absolute value of the return value contains count of awaiting coroutine
     */
    long get() {
        std::lock_guard _(_mx);
        if (_counter) return static_cast<long>(_counter);
        return -static_cast<long>(_waiting.size());
    }


    ///Try acquire the semaphore
    /**
     * @retval true acquired
     * @retval false not aquired
     */
    bool try_acquire() {
        std::lock_guard _(_mx);
        if (!_counter) return false;
        --_counter;
        return true;
    }


protected:
    unsigned long _counter = 0;
    std::mutex _mx;
    std::queue<coro::promise<void> > _waiting;

};


}
