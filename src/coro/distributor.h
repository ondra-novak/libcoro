#pragma once

#include "future.h"
#include "queue.h"

#include <vector>

namespace coro {

template<typename T, typename Filter, typename QueueImpl>
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
    distributor(Lock &&lk):_mx(std::forward<Lock>(lk)) {}


    ///publish the value
    /**
     * @param args arguments to construct payload
     *
     * @note function is blocked until all subscribers are notified.
     *
     */
    template<typename ... Args>
    void publish(Args && ... args) {
        for_all([&](promise &p){
            return p(std::forward<Args>(args)...);
        });
    }

    ///drops all subscribers
    /**
     * Subscribers are removed by receiving broken_promise. This allows to
     * broadcast "end of stream"
     */
    void drop_all() {
        for_all([&](promise &p){return p.drop();});
    }

    ///subscribe
    /**
     * @param id identifier of subsciber (optional)
     * @return future which is resolved with distributed event
     */
    future<T> subscribe(ID id = {}) {
        return [&](auto promise) {
            subscribe(std::move(promise), id);
        };
    }

    void subscribe(promise &&prom, ID id = {}) {
        std::lock_guard _(_mx);
        _subscribers.push_back(std::pair<promise,ID>(std::move(prom), id));
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
            auto iter = std::find_if(_subscribers.begin(), _subscribers.end(), [&](const auto &iter){
                return iter.second == id;
            });
            if (iter == _subscribers.end()) return {};
            ntf = iter->first.drop();
            if (&(iter->first) != &_subscribers.back().first) {
                std::swap(*iter, _subscribers.back());
                _subscribers.pop_back();
            }
        }
        return ntf;
    }

    struct EmptyFilter {
        constexpr bool operator()(const T &) {return true;}
    };

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


    template<typename Filter = EmptyFilter, typename QueueImpl = typename queue<T>::behavior>
    using queue = distributor_queue<T, Filter, QueueImpl>;

protected:
    [[no_unique_address]] Lock _mx;
    std::vector<std::pair<promise, ID> > _subscribers;

    template<typename Fn>
    void for_all(Fn && fn) {
        pending_notify *buff;
        std::size_t sz;
        {
            std::lock_guard _(_mx);
            sz = _subscribers.size();
            buff = reinterpret_cast<pending_notify *>(alloca(sizeof(pending_notify)*_subscribers.size()));
            for (std::size_t i = 0; i < sz; ++i) {
                try {
                    std::construct_at(buff+i, fn(_subscribers[i].first));
                } catch (...) {
                    std::construct_at(buff+1, _subscribers[i].first.reject());
                }
            }
            _subscribers.clear();
        }
        for (std::size_t i = 0; i < sz; ++i) {
            std::destroy_at(buff+i);
        }
    }
};

template<typename T, typename Filter, typename QueueImpl>
class distributor_queue: public queue<T, QueueImpl> {
    public:

    distributor_queue() = default;
    distributor_queue(distributor<T> &dist) {subscribe(dist);}
    distributor_queue(Filter flt):_filter(std::move(flt)) {}
    distributor_queue(Filter flt, distributor<T> &dist):_filter(std::move(flt)) {subscribe(dist);}
    distributor_queue(const distributor_queue &) = delete;
    distributor_queue &operator=(const distributor_queue &) = delete;

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
        void subscribe(distributor<T> &dist) {
            _connection = &dist;

            target_simple_activation(_target, [&](auto) {
                bool ok = _dist_value.visit([&](auto &&val){
                    using ValType = decltype(val);
                    using ValNormType = std::decay_t<ValType>;
                    if constexpr(std::is_same_v<ValNormType, std::exception_ptr>) {
                        this->close(val);
                        return false;
                    } else if constexpr(std::is_null_pointer_v<ValNormType>) {
                        this->close();
                        return false;
                    } else {
                        try {
                            if (_filter(val)) {
                                this->push(std::forward<ValType>(val));
                            }
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
        Filter _filter;
        typename future<T>::target_type _target;
        distributor<T> *_connection = nullptr;
        void charge() {
            auto prom = _dist_value.get_promise();
            _dist_value.register_target(_target);
            _connection->subscribe(std::move(prom), this);
        }
    };


}
