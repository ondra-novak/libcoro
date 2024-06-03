#pragma once

#include "future.h"

#include <mutex>
#include <optional>
#include <queue>

namespace coro {

///Implements asychronous queue with support for coroutines
/**
 * Asynchronous queue allows to @b co_await on new items. You can have multiple
 * coroutines reading the same queue (for push-pull)
 * @tparam T type of item
 * @tparam QueueImpl object which implements the queue, default is std::queue. You
 * can use for example some kind of priority queue or stack, however the
 * object must have the same interface as the std::queue
 * @ingroup awaitable
 */
template<typename T, typename QueueImpl = std::queue<T> >
class queue {
public:

    using value_type = T;
    using behavior = QueueImpl;

    ///Construct default queue
    queue() = default;
    ///Construct queue - initialize internal queue object
    template<std::convertible_to<QueueImpl> Q>
    explicit queue(Q &&qimpl):_item_queue(std::forward<Q>(qimpl)) {}
    ///The queue is not copyable
    queue(const queue &other) = delete;
    ///The queue is movable
    /**
     *  @param other source queue instance. The queue is moved
     *  with awaiting coroutines.
     *
     *  @note source queue becomes closed. You should avoid MT access during
     *  the moving.
     */
    queue(queue &&other):_closed(other._closed) {
        std::lock_guard lk(other._mx);
        _awaiters = std::move(other._awaiters);
        _item_queue = std::move(other._item_queue);
        other._closed = true;
    }
    queue &operator= (const queue &other) = delete;
    queue &operator= (queue &other) = delete;


    ///Push the item to the queue (emplace)
    /**
     * @param args arguments needed to construct the item.
     * @return pending notify of resolved promise (if applicable)
     * @note function can cause resumption of awaiting coroutine.
     *
     */
    template<typename ... Args>
    typename promise<T>::notify emplace(Args &&... args) {
        std::unique_lock lk(_mx);
        if (_awaiters.empty()) { //no awaiters? push to the queue
            _item_queue.emplace(std::forward<Args>(args)...);
            return {};
        }
        //pick first awaiter
        auto prom = std::move(_awaiters.front());
        _awaiters.pop();
        lk.unlock();
        //construct the item directly to the awaiter
        return prom(std::forward<Args>(args)...);
    }

    ///Push item to the queue
    /**
     * @param x item to push
     * @return pending notify of resolved promise (if applicable)
     * @note function can cause resumption of awaiting coroutine.
     */
    auto push(const T &x) {return emplace(x);}
    ///Push item to the queue
    /**
     * @param x item to push
     * @return pending notify of resolved promise (if applicable)
     * @note function can cause resumption of awaiting coroutine.
     */
    auto push(T &&x) {return emplace(std::move(x));}

    ///Pop the items
    /**
     * @return return Future wich is eventually resolved with an item.
     * @note The promise can be broken by calling the function close()
     */
    future<T> pop() {
        return [&](auto prom) {
            pop(prom);
        };
    }

    ///Pop item into a promise
    typename promise<T>::notify pop(promise<T> &prom) {
        std::unique_lock lk(_mx);
        if (_item_queue.empty()) {
            if (_closed) {
                if (_exception) return prom.reject(_exception);
                else return prom.cancel(); //breaks promise
            }
            LIBCORO_TRACE_LINK(this, prom.get_future());
            _awaiters.push(std::move(prom)); //remember promise
            return {};
        }
        auto ntf = prom(std::move(_item_queue.front())); //resolve promise
        _item_queue.pop();
        lk.unlock();
        return ntf;
    }

    ///Pops item non-blocking way
    /**
     * @return poped item if there is such item, or no-value if not
     */
    std::optional<T> try_pop() {
        std::unique_lock lk(_mx);
        if (_item_queue.empty()) {
            if (_exception) std::rethrow_exception(_exception);
            return {};
        }
        std::optional<T> out (std::move(_item_queue.front()));
        _item_queue.pop();
        return out;
    }

    ///determines whether queue is empty
    /**
     * @retval true queue is empty
     * @retval false queue is not empty
     * @note in MT environment, this value can be inaccurate. If you want
     * to check queue before pop, it is better to use try_pop()
     */
    bool empty() const {
        std::lock_guard _(_mx);
        return _item_queue.empty();
    }
    ///retrieve current size of the queue
    /**
     * @return current size of the queue
     */
    std::size_t size() const {
        std::lock_guard _(_mx);
        return _item_queue.size();
    }
    ///remove all items from the queue
    void clear() {
        std::lock_guard _(_mx);
        _item_queue = {};
    }
    ///close the queue
    /** closed queue means, that any awaiting coroutine
     * immediately receives BrokenPromise. Any attempt to
     * pop() also recives BrokenPromise when the queue is empty.
     * In thi state, it is still possible to push and pop items
     * but without blocking.
     *
     * @param e exception - you can set exception, which causes that
     * any attempt to pop value throws this exception. With default
     * value the queue only break promises
     */
    void close(std::exception_ptr e = nullptr) {
        std::unique_lock lk(_mx);
        if (_closed) return;
        _exception = e;
        auto z = std::move(_awaiters);
        _closed = true;
        lk.unlock();
        if (e) {
            while (!z.empty()) {
                z.front().reject(e);
                z.pop();
            }
        } else {
            while (!z.empty()) z.pop(); //break all promises
        }
    }

    ///Reactivates the queue
    void reopen() {
        std::lock_guard _(_mx);
        _closed = false;
        _exception = nullptr;
    }
protected:
    mutable std::mutex _mx;
    QueueImpl _item_queue;
    std::queue<promise<T> > _awaiters;
    std::exception_ptr _exception;
    bool _closed = false;
};

}
