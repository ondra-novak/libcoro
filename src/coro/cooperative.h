#pragma once
#include "prepared_coro.h"

#include <optional>
#include <queue>

namespace coro {

/**
 * @defgroup cooperative Cooperative
 * Set of functions for cooperative multitasking
 */

///Suspend current coroutine and switch to another coroutine ready to run
/**
 * This allows you to run cooperative task switching in the current thread.
 * In order for this to work, additional corutines need to be scheduled to run
 * within this thread. Each time a corutine is suspended on suspend(),
 * execution is switched to the next scheduled corutine.
 * The current corutine is scheduled to run at the end of the queue.
 *
 * This mode must be activated. Activation must be done by the main coroutine,
 * which first executes `@b co_await suspend()` to activate the cooperative run.
 * This first `@b co_await` is not about waiting, as there is no corutine ready
 * in the queue yet. After that, this main corutine can start other corutines
 * that use `@b co_await suspend()` when they run.
 * The main corutine itself can also use `@b co_await suspend()`.
 *
 * The main coroutine can finish anytime as it is no longer considered as main.
 * The current thread stays in this mode while there are any suspended coroutine
 * in the queue.
 *
 * Usage:
 *
 * @code
 * co_await coro::suspend();
 * @endcode
 *
 * @note this queue is thread local. If you need to share a queue between threads, use
 * thread_pool
 * @ingroup cooperative, awaitable
 *
 */
class suspend : public std::suspend_always{
public:

    ///this function is static.
    /**
     * You can call this function directly if you need to enqueue coroutine handle.
     * @param h coroutine handle to enqueue
     */
    static void await_suspend(std::coroutine_handle<> h) {
        if (local_queue) {
            local_queue->push(h);
        } else {
            std::queue<prepared_coro> q;
            local_queue = &q;
            h.resume();
            while (!q.empty()) {
                auto n = std::move(q.front());
                q.pop();
                n();
            }
            local_queue = nullptr;
        }
    }

    ///Determines, whether current thread is in cooperative mode
    /**
     * @retval true thread is in cooperative mode
     * @retval false thread is not in cooperative mode
     * @ingroup cooperative
     */
    friend bool in_cooperative_mode() {
        return local_queue != nullptr;
    }

    ///Enqueue coroutine to the queue in the cooperative mode
    /**
     * @param c coroutine to enqueue
     *
     * @note if the thread is not in cooperative mode, the coroutine
     * is just resumed
     * @ingroup cooperative
     *
     */
    friend void enqueue(prepared_coro c) {
        await_suspend(c.symmetric_transfer());
    }



protected:
    static thread_local std::queue<prepared_coro> *local_queue;
};


inline thread_local std::queue<prepared_coro> *suspend::local_queue = nullptr;



}
