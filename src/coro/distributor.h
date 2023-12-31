#pragma once

#include "future.h"
#include "queue.h"

#include <vector>
#include <map>

namespace coro {

template<typename T, typename Lock, typename QueueImpl>
class distributor_queue;

///Distributes single event to multiple coroutines (subscribbers)
/**
 * To use this object, a consument must subscribe itself at this object by calling
 * subscribe() method. Return value is lazy_future. It must be awaited to finish
 * subscribtion. To distribute event, you need to publish the event. The method
 * resumes all registered coroutines.
 *
 * Every subscription is one-shot. If the coroutine need to continue in subscription,
 * it must re-call subscribe() right after resumption (in the same thread, without
 * calling another co_await). If the subscriber need to process result asynchronously,
 * it need to use distributor::queue to push received results to an queue, which
 * can be also co_awaited
 *
 *
 * @tparam T type which is payload of distribution. Can be reference
 * @tparam Lock locking class. Default value disables locking for better performace.
 * You need to set to std::mutex, if you need MT Safety
 */

template<typename T, typename Lock = nolock>
class distributor {
public:

    using value_type = T;
    using promise = typename future<T>::promise;
    using pending_notify = typename future<T>::pending_notify;
    using ID = const void *;

    ///default constructor
    distributor() = default;
    ///initialize lock instance
    distributor(Lock &&lk) :
            _mx(std::forward<Lock>(lk)) {
    }

    ///publish the value
    /**
     * @param args arguments to construct payload
     *
     * @note function is blocked until all subscribers are notified.
     *
     */
    template<typename ... Args>
    void publish(Args &&... args) {
        for_all([&](promise &p) {
            return p(args...);
        });
    }

    ///drops all subscribers
    /**
     * Subscribers are removed by receiving broken_promise. This allows to
     * broadcast "end of stream"
     */
    void drop_all() {
        for_all([&](promise &p) {
            return p.drop();
        });
    }

    ///subscribe
    /**
     * @param id identifier of subsciber (optional)
     * @return future which is resolved with distributed event
     */
    future<T> subscribe(ID id = { }) {
        return [&](auto promise) {
            subscribe(std::move(promise), id);
        };
    }

    void subscribe(promise &&prom, ID id = { }) {
        std::lock_guard _(_mx);
        _subscribers.push_back(std::pair<promise, ID>(std::move(prom), id));
    }

    ///drop single subscriber
    /**
     * @param id identifier of subscriber
     * @return pending_notify object, You can drop this object to finish operation, or
     * you can schedule its drop operation.
     */
    pending_notify drop(ID id) {
        pending_notify ntf;
        {
            std::lock_guard _(_mx);
            auto iter = std::find_if(_subscribers.begin(), _subscribers.end(),
                    [&](const auto &iter) {
                        return iter.second == id;
                    });
            if (iter == _subscribers.end())
                return {};
            ntf = iter->first.drop();
            if (&(iter->first) != &_subscribers.back().first) {
                std::swap(*iter, _subscribers.back());
                _subscribers.pop_back();
            }
        }
        return ntf;
    }

    ///Implements queue above distributor
    /**
     * @tparam Filter specifies filter function. Filter function is called synchronously
     * for the every item. It should return true to push item into queue, or false to
     * discard the item. This can help with slow subscribers to control the queue size.
     * For example, the function can skip unimportant events
     *
     * @tparam QueueImpl internal queue implementation
     *
     *
     * You need to connect the object to a distributor
     */

    template<typename QueueImpl = typename queue<T>::behavior>
    using queue = distributor_queue<T, Lock, QueueImpl>;

protected:
    [[no_unique_address]] Lock _mx;
    std::vector<std::pair<promise, ID> > _subscribers;

    template<typename Fn>
    void for_all(Fn &&fn) {
        pending_notify *buff;
        std::size_t sz;
        {
            std::lock_guard _(_mx);
            sz = _subscribers.size();
            buff = reinterpret_cast<pending_notify*>(alloca(
                    sizeof(pending_notify) * _subscribers.size()));
            for (std::size_t i = 0; i < sz; ++i) {
                try {
                    std::construct_at(buff + i, fn(_subscribers[i].first));
                } catch (...) {
                    std::construct_at(buff + 1, _subscribers[i].first.reject());
                }
            }
            _subscribers.clear();
        }
        for (std::size_t i = 0; i < sz; ++i) {
            std::destroy_at(buff + i);
        }
    }
};

template<typename T, typename Lock, typename QueueImpl>
class distributor_queue: public queue<T, QueueImpl> {
public:

    distributor_queue() = default;
    distributor_queue(QueueImpl qimpl) :
            queue<T, QueueImpl>(std::move(qimpl)) {
    }
    distributor_queue(distributor<T, Lock> &dist) {
        subscribe(dist);
    }
    distributor_queue(const distributor_queue&) = delete;
    distributor_queue& operator=(const distributor_queue&) = delete;

    ///Subscribes to distributor
    /**
     * @param dist reference to distributor, you need to ensure that
     * reference stays valid during connection
     *
     * Once distributor is connected, it starts to push distributed values to
     * the queue. The coroutine can co_await on the queue's pop() operation. It
     * can process results asynchronously without loosing any distributed event
     *
     */
    void subscribe(distributor<T, Lock> &dist) {
        _connection = &dist;

        target_simple_activation(_target, [&](auto) {
            bool ok = _dist_value.visit([&](auto &&val) {
                using ValType = decltype(val);
                using ValNormType = std::decay_t<ValType>;
                if constexpr (std::is_same_v<ValNormType, std::exception_ptr>) {
                    this->close(val);
                    return false;
                } else if constexpr (std::is_null_pointer_v<ValNormType>) {
                    this->close();
                    return false;
                } else {
                    try {
                        this->push(std::forward<ValType>(val));
                        return true;
                    } catch (...) {
                        this->close(std::current_exception());
                        return false;
                    }
                }
            });
            if (ok) {
                charge();
            }

        });
        charge();
    }

    ///Unsubscribes from distributor
    /** This causes closing the queue and resuming the awaiting coroutine with
     * broken promise.
     *
     * @note Destructor of the queue performs unsubscribe automatically
     */
    void unsubscribe() {
        if (_connection) {
            _connection->drop(this);
            _connection = nullptr;
        }

    }

    ~distributor_queue() {
        unsubscribe();
    }

protected:
    future<T> _dist_value;
    typename future<T>::target_type _target;
    distributor<T, Lock> *_connection = nullptr;
    void charge() {
        auto prom = _dist_value.get_promise();
        _dist_value.register_target(_target);
        _connection->subscribe(std::move(prom), this);
    }
};

///A special purpose queue which is intended to filter events sent to slow subscribers
/**
 * When subscriber is slow, you can filter messages depend on how important they are. You
 * need to define a sorting function
 * @tparam T type of item stored in the queue
 * @tparam FilterFn a function which is called with an item. It must return a priority of the
 * message as a number. Default implementation expects that return value 0 is important
 * message, which must not be filtered out. Other numbers defines priority, where highter
 * number is lower priority. The number also identifies a message kind. If there is already
 * message with the same priority, it is replaced (so only last update is stored in the queue).
 *
 * @tparam important_message specifies return value of the filter function which is
 * interpreted as important message, which must not be filtered out.
 *
 * The object acts as std::queue compatible with coro::queue
 *
 * @code
 * coro::distributor_queue<Item, std::mutex, filtered_upate_queue<Item, MyFilter> > dist_queu;
 * @endcode
 *
 */
template<typename T, std::invocable<T> FilterFn, std::invoke_result_t<FilterFn, T> important_message = 0>
class filtered_update_queue {
public:
    using FilterResult = std::invoke_result_t<FilterFn, T>;

    template<std::convertible_to<FilterFn> Fn>
    filtered_update_queue(Fn &&fn):_filter(std::forward<Fn>(fn)) {}

    filtered_update_queue() = default;

    bool empty() const {return _main_queue.empty() && _updates.empty();}

    ///Retrieves first item in queue
    /**
     * @return reference to item
     *
     * @note Doesn't check for empty, calling function on empty queue is UB
     */
    T &front() {
        if (_main_queue.empty()) return _updates.begin()->second;
        return _main_queue.front();
    }
    ///Retrieves first item in queue
    /**
     * @return reference to item
     *
     * @note Doesn't check for empty, calling function on empty queue is UB
     */
    const T &front() const {
        if (_main_queue.empty()) return _updates.begin()->second;
        return _main_queue.front();
    }

    ///Removes first item from the queue
    /**
     * @note Doesn't check for empty, calling function on empty queue is UB     *
     */
    void pop() {
        if (_main_queue.empty()) _updates.erase(_updates.begin());
        else _main_queue.pop_front();
    }

    void push(const T &x) {
        emplace(x);
    }

    void push(T &&x) {
        emplace(std::move(x));
    }

    template<typename ... Args>
    void emplace(Args && ... args) {
        //assume that message is important (we can't determine ID from args)
        //construct it in queue
        _main_queue.emplace_back(std::forward<Args>(args)...);
        //retrieve it constructed
        T &itm = _main_queue.back();
        //retrieve is ID
        FilterResult id = _filter(itm);

        //if it is important message, we are good
        if (id == important_message) return;

        //find whether there is message with same ID
        auto iter = _updates.find(id);
        //if not
        if (iter == _updates.end()) {
            //create it
            _updates.emplace(id, std::move(itm));
        } else {
            //otherwise replace it
            iter->second = std::move(itm);
        }
        //remove it from the queue )
        _main_queue.pop_back();
    }

protected:
    using MainQueue = std::deque<T>;
    using Updates = std::map<FilterResult, T>;

    FilterFn _filter;
    MainQueue _main_queue;
    Updates _updates;

};

}
