#pragma once

#include "condition.h"

#include <stdexcept>
namespace coro {

namespace _details {

    class pool_control;

    struct pool_block {
        union {
            pool_control *_control;
            pool_block *_next;
        };
        std::size_t _size = 0;
        void *get_ptr() {
            return this+1;
        }

        static pool_block *alloc(std::size_t sz) {
            auto whole_size = sz + sizeof(pool_block);
            pool_block *b = reinterpret_cast<pool_block *>(::operator new(whole_size));
            b->_size = sz;
            return b;
        }

        static void dealloc(pool_block *blk) {
            ::operator delete(blk);
        }

        static pool_block *from_ptr(void *ptr) {
            auto me = reinterpret_cast<pool_block *>(ptr);
            return me-1;
        }
    };


    class pool_control {
    public:
        static thread_local pool_control instance;

        pool_control() = default;
        pool_control(const pool_control &) = delete;
        pool_control &operator=(const pool_control &) = delete;

        ~pool_control() {
            clean_all();
        }


        pool_block *pick(std::size_t sz) {
            pool_block *out;
            do {
                if (_hashtable.empty()) {
                    if (!process_dropped()) {
                        out = pool_block::alloc(sz);
                        break;
                    }
                }
                do {
                    pool_block *& b = map_size(sz);
                    pool_block **lnk = &b;
                    pool_block *p = b;
                    while (p && p->_size != sz) {
                        lnk = &p->_next;
                        p = *lnk;
                    }
                    if (p) {
                        *lnk = p->_next;
                        if (!b) --_keys;
                        p->_control = this;
                        return p;
                    }
                } while (process_dropped());
                out = pool_block::alloc(sz);
            } while (false);
            out->_control = this;
            return out;
        }

        pool_block *& map_size(std::size_t sz) {
            auto idx = sz % _hashtable.size();
            return _hashtable[idx];
        }

        void insert(pool_block *blk) {
            check_resize();
            auto &b = map_size(blk->_size);
            if (!b) ++_keys;
            blk->_next = b;
            b = blk;
        }

        bool process_dropped() {
            pool_block *lst = _dropped.exchange(nullptr, std::memory_order_relaxed);
            if (!lst) return false;
            while (lst) {
                auto p = lst;
                lst = lst->_next;
                insert(p);
            }
            return true;
        }

        void drop(pool_block *blk) {
            blk->_next = _dropped.load(std::memory_order_relaxed);
            while (!_dropped.compare_exchange_weak(blk->_next, blk,std::memory_order_relaxed));
        }

        void clean_all() {
            pool_block *lst = _dropped.exchange(nullptr, std::memory_order_relaxed);
            while (lst) {
                auto p = lst;
                lst = lst->_next;
                p->dealloc(p);
            }
            for (auto &x: _hashtable) {
                while (x) {
                    auto p = x;
                    x = x->_next;
                    p->dealloc(p);
                }
            }
        }

        void check_resize() {
            auto sz =_hashtable.size();
            if (_keys * 2 >= sz) {
                std::size_t newsz = next_prime_twice_than(std::max<std::size_t>(sz, 16));
                std::vector<pool_block *> tmp(newsz, nullptr);
                std::swap(tmp, _hashtable);
                _keys = 0;
                for (auto &b :tmp) {
                    while (b) {
                        auto x = b;
                        b = b->_next;
                        insert(x);
                    }
                }
            }
        }

    private:
        std::vector<pool_block *> _hashtable;
        std::atomic<pool_block *> _dropped = {};
        std::size_t _keys = 0;
    };


    inline thread_local pool_control pool_control::instance;

}

///A corutine allocator that caches unused frames in the pool.
/** The pool is implemented as a hash-map with a list of free frames.
 * Whenever a corutine leaves its frame,
 * it is inserted into the pool and is quickly released
 * when a new frame is requested for allocation
 *
 * Each thread has its own pool.
 * This makes the allocation not using locking.
 * Deallocations that are made in a thread other
 * than the mother thread of the frame are registered
 * in a lock-free list, which is then moved to
 * the mother pool at the appropriate time
 *
 * @ingroup allocators
 */
class pool_alloc {
public:

    ///allocate a block
    /**
     * @param sz requested size. The size is rounded to next 16 bytes
     * @return pointer to allocated block
     */
    static void *alloc(std::size_t sz) {
        sz = (sz + 0xF) & ~0xF;
        auto blk = _details::pool_control::instance.pick(sz);
        return blk->get_ptr();
    }

    ///deallocate block
    /**
     * @param ptr pointer to block, it must be allocated by this allocator
     * @param sz size of the block, it must be same size as requested during allocation
     */
    static void dealloc(void *ptr, std::size_t sz) {
        sz = (sz + 0xF) & ~0xF;
        auto blk = _details::pool_block::from_ptr(ptr);
        if (blk->_size != sz) throw std::runtime_error("coro::pool_alloc - invalid pointer for dealloc()");
        auto control = blk->_control;
        if (control != &_details::pool_control::instance) {
            control->drop(blk);
        } else {
            control->insert(blk);
        }
    }
};



}
