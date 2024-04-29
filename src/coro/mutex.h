#pragma once
#include "prepared_coro.h"
#include "future.h"

#include <atomic>
#include <memory>
#include <variant>


namespace coro {

///Mutex which allows locking across @b co_await and @b co_yield suspend points.
/**
 * This object can be used to hold exclusive access to a resource while the
 * coroutine is suspended on @b co_await on @b co_yield. Standard mutex can't support
 * such feature. This mutex is also co_awaitable.
 *
 * The object suports lock and try_lock. However the lock protocol is different. When
 * lock success, you receive an ownership object, which must be held to
 * keep exclusive access. Once the ownership is released, the mutex is unlocked.
 * The ownership object uses RAII to track to mutex ownership. So there is no
 * explicit unlock() function.
 *
 * The mutex object support @b co_await, lock_sync()
 *
 * Attempt to lock the mutex always return a future with ownership.
 *
 * @ingroup awaitable
 */
class mutex {
public:

    ///tracks ownership
    class ownership {
    public:
        ///ownership can be default constructed
        ownership() = default;
        ///ownership can be moved
        ownership(ownership &&x):_inst(x._inst) {x._inst = nullptr;}
        ///ownership can be assigned by move
        ownership &operator=(ownership &&x) {
            if (this != &x) {
                release();
                _inst = x._inst;
                x._inst = nullptr;
            }
            return *this;
        }
        ///dtor releases ownership
        ~ownership() {release();}
        ///releases ownership explicitly (unlock)
        void release() {
            if (_inst) {
                auto tmp = _inst;
                _inst = nullptr;
                tmp->unlock();
            }
        }
        ///test whether ownership is held
        /**
         * The existence of the object doesn't always means, that ownership is held.
         * For example if ownership has been released, or when try_lock() was not
         * successed. You can use this operator to test, whether ownership is held
         */
        operator bool() const {return _inst != nullptr;}


    protected:
        mutex *_inst = nullptr;

        friend class mutex;

        ownership(mutex *inst):_inst(inst) {}
    };

    ///try to lock
    /**
     * @return ownership. If the function fails, the ownership is not given. You need
     * to convert ownership to bool and test it.
     */
    ownership try_lock() {
        return ownership(try_acquire()?this:nullptr);
    }


    ///lock the mutex, retrieve future ownership
    future<ownership> lock() {
        return [&](auto promise) {
            do_lock(promise.release());
        };
    }

    ///lock the mutex co_awaitable
    future<ownership> operator co_await() {
        return lock();
    }

    ///lock synchronously
    ownership lock_sync() {
        return std::move(lock().get());
    }

protected:
    class awaiter : public future<ownership> {
    public:
        using future<ownership>::_chain;
        static awaiter *from_future(future<ownership> *x) {
            return static_cast<awaiter *>(x);
        }
    };

    ///generates special pointer, which is used as locked flag (value 0x00000001)
    static awaiter *locked() {return reinterpret_cast<awaiter *>(0x1);}

    /**
     * contains stack of requests, or nullptr when lock is unlocked,
     * or 0x00000001 when lock is locked with no requests
     */
    std::atomic<awaiter *> _req = {nullptr};
    ///contains queue of requests already registered by the lock
    /** the queue is always managed by an owner  */
    awaiter * _que = nullptr;


    ///tries to acquire
    /**
     * attempts to set _req to locked() if it has nullptr
     * @retval true success, acquired
     * @retval false failed, lock is owned
     */
    bool try_acquire() {
        awaiter *need = nullptr;
        return _req.compare_exchange_strong(need,locked(), std::memory_order_acquire);
    }

    ///initiate lock operation
    /**
     * registers the awaiter as a new request. If it is first request, the
     * lock has been acquired, builds queue of requests and returns false. Otherwise
     * keeps awaiter registered and returns true
     *
     * @param awt awaiter
     * @retval false can't registrer, we acquired ownership, so continue
     * @retval true registered, we can wait
     */
    promise<ownership>::notify do_lock(future<ownership> *fut_awt) {
        awaiter *awt = awaiter::from_future(fut_awt);
        awaiter *nx = nullptr;
        while (!_req.compare_exchange_weak(nx, awt, std::memory_order_relaxed)) {
            awt->_chain = nx;
        }

        if (nx == nullptr) {
            build_queue(_req.exchange(locked(), std::memory_order_acquire), awt);
            return promise<ownership>(fut_awt)(make_ownership());
        }
        return {};
    }

    ///builds internal queue
    /**
     * Picks are requests and reorder them in reverse order so stack is converted to
     * queue. This is done by the owner.
     *
     * @param r pointer request stack (linked list)
     * @param stop pointer to item, which is used as stop item (which is often first
     * awaiter or locked flag)
     */
    void build_queue(awaiter *r, awaiter *stop) {
        while (r != stop) {
            auto n = r->_chain;
            r->_chain = _que;
            _que = r;
            r = awaiter::from_future(n);
        }
    }

    ///unlock the lock
    /**
     * if queue is empty, tries to swap locked() to nullptr, however if this fails,
     * it builds queue and continues with the queue
     *
     * if queue is not empty, retrieves the first awaiter, removes it from the queue
     * and resumes it, which transfers ownership to the new owner.
     */
    promise<ownership>::notify unlock() {
        if (!_que) {
            auto lk = locked();
            auto need = lk;
            if (_req.compare_exchange_strong(need, nullptr, std::memory_order_release)) return {};
            build_queue(_req.exchange(lk, std::memory_order_relaxed), lk);
        }
        promise<ownership> ret(_que);
        _que = static_cast<awaiter *>(_que->_chain);
        return ret(make_ownership());
    }


    ///creates ownership object
    ownership make_ownership() {return this;}

};

namespace _details {
    template<typename T, typename Tuple> struct tuple_add;
    template<typename T, typename ... Us> struct tuple_add<T, std::tuple<Us...> > {
        using type = std::tuple<T, Us...>;
    };

    template <typename Tuple> struct make_unique_types;
    template <typename T, typename... Us> struct make_unique_types<std::tuple<T, Us...> > {
        using type = std::conditional_t<
                (std::is_same_v<T, Us> || ...),
                typename make_unique_types<std::tuple<Us ...> >::type,
                typename tuple_add<T, typename make_unique_types<std::tuple<Us...> >::type>::type>;
    };

    template <> struct make_unique_types< std::tuple<> > {
        using type = std::tuple<>;
    };

    template<typename Tuple> struct make_variant_from_tuple;
    template<typename ... Ts> struct make_variant_from_tuple<std::tuple<Ts...> > {
        using type = std::variant<Ts...>;
    };
    template<typename ... Ts> using variant_of_t = typename make_variant_from_tuple<
            typename make_unique_types<std::tuple<Ts...> >::type>::type;
}


template<typename ... Mutexes>
class lock: public future<std::tuple<decltype(std::declval<Mutexes>().try_lock())...> > {
public:

    using ownership = std::tuple<decltype(std::declval<Mutexes>().try_lock())...>;
    using future_variant = _details::variant_of_t<std::monostate,
            decltype(std::declval<Mutexes>().lock())...>;

    lock(Mutexes & ... lst):_mxlist(lst...),_prom(this->get_promise()) {
        ownership ownlist;
        if (finish_lock(-1, ownlist)) {
            _prom(std::move(ownlist));
        } //if failed, ownership is released and so all held locks
    }


protected:
    std::tuple<Mutexes &...> _mxlist;
    promise<ownership> _prom;
    future_variant _fut;

    ///called when lock on particular mutex is complete
    /**
     * @tparam idx index of mutex
     */
    template<int idx>
    void on_lock() {
        auto &mx = std::get<idx>(_mxlist);
        using LkType = decltype(mx.lock());
        ownership ownlist;
        std::get<idx>(ownlist) = std::get<LkType>(_fut).await_resume();
        if (finish_lock(idx,ownlist)) {
            _prom(std::move(ownlist));
        }
    }

    ///finishes locking of all locks
    /**
     * @tparam idx index of starting lock (default 0)
     * @param skip index of lock which is already locked (to skip)
     * @param ownlist result ownership variable
     * @retval true all locks are locked
     * @retval false failure some lock was not locked, waiting callback installed
     */
    template<int idx = 0>
    bool finish_lock(int skip, ownership &ownlist) {
        //idx out of range? success
        if constexpr(idx >= std::tuple_size_v<ownership>) {
            return true;
        } else {
            //idx == skip - skip it
            if (idx == skip) {
                return finish_lock<idx+1>(skip,ownlist);
            } else {
                //retrieve ownership
                auto &own = std::get<idx>(ownlist);
                //retrieve lock
                auto &mx = std::get<idx>(_mxlist);
                //try to lock
                own = std::get<idx>(_mxlist).try_lock();
                //we success
                if (own) {
                    //continue by next lock
                    return finish_lock<idx+1>(skip,ownlist);
                } else {
                    //failure
                    using LkType = decltype(mx.lock());
                    //construct future object
                    _fut.template emplace<LkType>();
                    LkType &fut = std::get<LkType>(_fut);
                    //acquire lock
                    fut << [&]{return mx.lock();};
                    //install callback, if failed
                    if (fut.set_callback([this]{ on_lock<idx>();}) == false) {
                        //we got ownership
                        own = fut.get();
                        //continue locking
                        return finish_lock<idx+1>(skip,ownlist);
                    }
                    //return failure
                    return false;
                }
            }
        }
    }
};


}




