#pragma once
#include "prepared_coro.h"

#include <atomic>


namespace coro {

///Mutex which allows locking across co_await and co_yield suspend points.
/**
 * This object can be used to hold exclusive access to a resource while the
 * coroutine is suspended on co_await on co_yield. Standard mutex can't support
 * such feature. This mutex is also co_awaitable.
 *
 * The object suports lock and try_lock. However the lock protocol is different. When
 * lock success, you receive an ownership object, which must be held to
 * keep exclusive access. Once the ownership is released, the mutex is unlocked.
 * The ownership object uses RAII to track to mutex ownership. So there is no
 * explicit unlock() function.
 *
 * The mutex object support co_await, lock_sync() and lock_callback(). The third
 * function allows to call a callback when lock is acquired.
 *
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
        ///releases ownership exlicitly (unlock)
        void release() {
            if (_inst) _inst->unlock();
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

    ///awaiter is object used in most of cases by coroutines, however it is building block of this class
    /**
     * The awaiter must be constructed to acquire the lock. The awaiter
     * can be configured to resume coroutine, send a signal to blocked thread or
     * to call a callback.
     */
    class awaiter {
    public:

        enum Mode {
            //not configures (should not be used to waiting)
            none,
            //resume coroutine
            coroutine,
            //synchronize
            sync,
            //call a callback
            callback
        };

        ///awaiter is copyable, however it must not be copied while it is waiting
        awaiter(const awaiter &other):_owner(other._owner) {}
        ///awaiter is not assignable
        awaiter &operator=(const awaiter &other) = delete;

        ///dtor
        ~awaiter() {
            switch (_mode) {
                default: break;
                case coroutine: std::destroy_at(&_coro);
                case sync: std::destroy_at(&_sync);
                case callback: std::destroy_at(&_resume_cb);
            }
        }

        ///coroutine - try to acquire lock
        bool await_ready() const noexcept {return _owner.try_acquire();}
        ///coroutine - request the lock and suspend
        bool await_suspend(std::coroutine_handle<> h) noexcept {
            _mode = coroutine;
            std::construct_at(&_coro,h);
            return _owner.do_lock(this);
        }
        ///coroutine - retrieve ownership
        ownership await_resume() const noexcept {
            return _owner.make_ownership();
        }

        ///perform synchronou wait on lock
        /**
         * @return ownership
         */
        ownership wait() {
            if (!await_ready()) {
                _mode = sync;
                std::construct_at(&_sync, false);
                if (_owner.do_lock(this)) {
                    _sync.wait(false);
                }
            }
            return await_resume();
        }

    protected:
        mutex &_owner;
        awaiter *_next = nullptr;
        Mode _mode = none;
        union {
            std::coroutine_handle<> _coro;
            std::atomic<bool> _sync;
            void (*_resume_cb)(awaiter *);
        };

        awaiter(mutex &owner):_owner(owner) {}

        prepared_coro resume() {
            switch (_mode) {
                default: break;
                case coroutine: return _coro;
                case sync:
                    _sync.store(true, std::memory_order_release);
                    _sync.notify_all();
                    break;
                case callback:
                    _resume_cb(this);
                    break;
            }
            return {};
        }

        void init_cb(void (*cb)(awaiter *)) {
            _mode = callback;
            _resume_cb = cb;
        }

        friend class mutex;
    };

    ///awaiter with a callback function
    /**
     * This object is never used directly
     *
     * @tparam Fn callback function
     * @tparam static_buffer set true, if the awaiter is constructed in static buffer
     */
    template<typename Fn, bool static_buffer>
    class awaiter_cb: public awaiter {
    public:

        awaiter_cb(mutex &owner, Fn &&fn):awaiter(owner), _fn(std::forward<Fn>(fn)) {
            init_cb(&cb_entry);
        }
        awaiter_cb(const awaiter_cb &) = delete;

        void resume() noexcept {
            _fn(_owner.make_ownership());
            if constexpr (static_buffer) {
                std::destroy_at(this);
            } else {
                delete this;
            }
        }

    protected:

        Fn _fn;

        static void cb_entry(awaiter *me) {
            auto self = static_cast<awaiter_cb *>(me);

        }
    };

    ///contains required size of static buffer to hold awaiter for given function
    /**
     * @tparam Fn callback function type
     */
    template<typename Fn>
    static constexpr std::size_t lock_callback_buffer_size = sizeof(awaiter_cb<Fn, true>);

    ///try to lock
    /**
     * @return ownership. If the function fails, the ownership is not given. You need
     * to convert ownership to bool and test it.
     */
    ownership try_lock() {
        return ownership(try_acquire()?this:nullptr);
    }

    ///co_await support
    awaiter operator co_await() {return *this;}

    ///lock synchronously
    ownership lock_sync() {
        awaiter awt(*this);
        return awt.wait();
    }

    ///lock and call a function when access is granted
    /**
     * @param fn function to call. The function accepts an ownership. The function must
     * not throw any exception
     *
     * @note this function allocates an awaiter on the heap, it is automatically released
     * after call is done.
     */
    template<std::invocable<ownership> Fn>
    void lock_callback(Fn &&fn) noexcept {
        auto a = new awaiter_cb<Fn, false>(*this, std::forward<Fn>(fn));
        if (do_lock(a)) return;
        a->resume();
    }

    ///lock and call a function when access is granted
    /**
     * The awaiter is allocated in static buffer. Size of the static buffer
     * can be determined by a constant lock_callback_buffer_size
     *
     * @param fn instance of function to call
     * @param buffer reserved space of sufficient size, where the awaiter is
     * temporary allocated. The buffer must stay valid until the callback
     * is called
     *
     * @note doesn't allocate on heap
     */
    template<std::invocable<ownership> Fn, std::size_t sz>
    void lock_callback(Fn &&fn, char (&buffer)[sz]) {
        static_assert(sz <= lock_callback_buffer_size<Fn>, "Buffer is too small");
        auto a = new(buffer) awaiter_cb<Fn, true>(*this, std::forward<Fn>(fn));
        if (do_lock(a)) return;
        a->resume();
    }


protected:

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
    bool do_lock(awaiter *awt) {
        while (!_req.compare_exchange_strong(awt->_next, awt, std::memory_order_relaxed));
        if (awt->_next == nullptr) {
            build_queue(_req.exchange(locked(), std::memory_order_acquire), awt);
            return false;
        }
        return true;
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
            auto n = r->_next;
            r->_next = _que;
            _que = r;
            r = n;
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
    prepared_coro unlock() {
        if (!_que) {
            auto lk = locked();
            auto need = lk;
            if (_req.compare_exchange_strong(need, nullptr, std::memory_order_release)) return {};
            build_queue(_req.exchange(lk, std::memory_order_relaxed), lk);
        }
        auto x = _que;
        _que = _que->_next;
        return x->resume();
    }


    ///creates ownership object
    ownership make_ownership() {return this;}
};



}




