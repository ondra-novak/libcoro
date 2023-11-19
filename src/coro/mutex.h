#pragma once

#include "target.h"

namespace coro {

///Mutex handle locking inside of coroutine
class mutex {
public:

    ///Ownership is implemented as std::unique_ptr with custom deleter. This is the deleter
    struct ownership_deleter {
        void operator()(mutex *m) noexcept {
            auto x =m->unlock();
            if (x) x.resume();
        }
    };

    ///Ownership declaration
    using ownership = std::unique_ptr<mutex, ownership_deleter>;
    ///Target declaration
    using target_type = target<ownership &&>;

    ///Tries to lock the mutex without waiting
    /**
     *
     * @return ownership, which is nullptr if lock failed.
     */
    ownership try_lock() {
        return try_lock_internal()?ownership(this):ownership(nullptr);
    }

    ///Implements co_await on the mutex
    class awaiter {
        union {
            //_owner is needed only for await_ready and await_suspend
            mutex *_owner;
            //_ownership is result object is needed by await_resume
            // so we can save a space to store both objects
            ownership _ownership;
        };
        //contains target
        target_type _t;
    public:
        awaiter(mutex &owner):_owner(&owner) {};
        awaiter(const awaiter &) = default;
        awaiter &operator=(const awaiter &) = delete;
        ~awaiter() {
            //no cleanup on ownership as it should be cleaned by await_resume()
        }

        bool await_ready() noexcept {
            if (_owner->try_lock_internal()) {
                //succesfully locked, store ownership
                std::construct_at(&_ownership,_owner);
                return true;
            }
            //still waiting
            return false;
        }
        bool await_suspend(std::coroutine_handle<> h) noexcept {
            //init target
            target_coroutine(_t, h, &_ownership);
            //try to register
            if (_owner->register_target_async(_t) == false) {
                //if failed to register, we already have a ownership
                //store it temporarily
                std::construct_at(&_ownership,_owner);
                //stop suspend now (wake up)
                return false;
            }
            //continue suspend
            return true;
        }

        ownership await_resume() noexcept {
            //_ownership must be always initialized
            //move it to return value
            auto r = std::move(_ownership);
           //destroy the original storage
            std::destroy_at(&_ownership);
            //return ownership
            return r;
        }

    };

    ///Register target for locking
    /**
     * @note this follows rule about targets, don't use for locking unless you need
     *  to lock with custom target
     *
     * @param t reference to target
     * @retval true target registered and will be activated once the ownership is gained
     * @retval false you gained ownership immediatelly. To retrieve Ownership object, just
     * construct Ownership with pointer to mutex
     */
    bool register_target_async(target_type &t) noexcept {
        //push target to _requests
        t.push_to(_requests);
        //check, whether there is "next" target
        if (t.next == nullptr) [[likely]] {
            //if there is nullptr, the mutex was not owned!
            //however we need to put lock_tag and build queue (even empty)
            build_queue(t);
            //target was not registered, return false
            return false;
        } else {
            //target is registered, return true
            return true;
        }
    }

    ///Register target for locking
    /**
     * @note this follows rule about targets, don't use for locking unless you need
     *  to lock with custom target
     *
     * @param t reference to target
     * @retval true target registered and will be activated once the ownership is gained
     * @retval false target activated synchronously
     */
    bool register_target(target_type &t) noexcept {
        if (register_target_async(t)) return true;
        t.activate_resume(ownership(this));
        return false;
    }

    bool register_target(unique_target<target_type> t) {
        target_type *tx = t.release();
        return register_target(*tx);
    }

    ///implement co_await
    awaiter operator co_await() noexcept {return *this;}

    ///lock synchronously
    ownership lock_sync() noexcept {
        if (!try_lock_internal()) {
            sync_target<target_type> sync;
            register_target(sync);
            return std::move(sync.wait());
        }
        return ownership(this);
    }


protected:
    ///Reserved target which marks busy mutex (mutex is owned)
    static constexpr target_type locked_tag = {};
    ///List of requests
    /**
     * If mutex is owned, there is always non-null value. This make queue
     * of targets requesting the ownership. The queue is reversed (lifo)
     *
     * Adding new item to the queue is lock-free atomic
     **/
    std::atomic<const target_type *> _requests = {nullptr};
    ///Internal queue ordered in correct order
    /**
     * This queue is managed by owner only, so no locking is needed. It contains
     * list of next-owners. When unlock, next target is popped and activated
     */
    const target_type *_queue= nullptr;

    ///try_lock internally (without creating ownership)
    bool try_lock_internal() noexcept {
        //to lock, we need nullptr
        const target_type *need = nullptr;
        //returns true, if locked_tag was exchanged - so mutex is owned
        return _requests.compare_exchange_strong(need, &locked_tag);
    }

    ///builds queue from _request to _queue
    /** Atomically picks all requests and builds _queue, in reverse order
     * This is perfomed by the owner.
     *
     * @param stop specified target where operation stops, this is the Target object
     * of current owner.
     */
    void build_queue(const target_type &stop) noexcept {
        //atomically move requests from public space into private space
        const target_type *req = _requests.exchange(&locked_tag);
        //reverse order of the queue and build it to _queue
        while (req && req != &stop) {
            auto x = req;
            req = req->next;
            x->next = _queue;
            _queue = x;
        }
    }

    std::coroutine_handle<> unlock() noexcept {
        //current owner is returning ownership, (but it is still owner)
        //check queue in private space.
        if (!_queue) [[likely]] {
            const target_type *need = &locked_tag;
            //if queue is empty, try to remove locked_tag from the _requests
            if (_requests.compare_exchange_strong(need, nullptr)) {
                //success, mutex is no longer owned
                return nullptr;
            }
            //failure? process new requests, build new queue
            build_queue(locked_tag);
        }
        //pick first target from queue
        const target_type *first = _queue;
        //remove this target from queue
        _queue = _queue->next;
        //active the target, transfer ownership
        //awaiting coroutine is resumed here
        return first->activate(ownership(this));
    }
};


}
