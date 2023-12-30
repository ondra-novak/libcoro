#pragma once

#include <bit>
#include <atomic>
#include <new>

namespace coro {



template<std::size_t sz, std::size_t alignment = 32>
class pool_alloc_sz {
public:

    static constexpr std::size_t alloc_size = (((sz - 1)/alignment) + 1) * alignment;
    static constexpr std::size_t counter_mask  = alignment -1;

    union slot {
        std::uintptr_t next;
        char space[sz];
    };


    void *alloc() {
        auto need = _empty_slots.load(std::memory_order_relaxed);
        slot *s;
        do {
            if (need < sz) return alloc_raw();
            s = decode_ptr(need);
        } while (!_empty_slots.compare_exchange_strong(need, s->next, std::memory_order_relaxed));
        return s;
    }

    void dealloc(void *ptr) {
        slot *s = reinterpret_cast<slot *>(ptr);
        s->next = _empty_slots.load(std::memory_order_relaxed);
        std::uintptr_t newval = encode_ptr_counter(s, _counter++);
        do {
            if (s->next == 1) [[unlikely]] {
                dealloc_raw(ptr);
                return;
            }
        } while(!_empty_slots.compare_exchange_strong(s->next, newval, std::memory_order_relaxed));
    }

    static pool_alloc_sz instance;

    ~pool_alloc_sz() {
        std::uintptr_t p = _empty_slots.exchange(1,std::memory_order_relaxed);
        while (p) {
            slot *s = decode_ptr(p);
            p = s->next;
            dealloc_raw(s);
        }
    }


protected:



    void *alloc_raw() {
#ifdef _WIN32
        return _aligned_alloc(alignment, alloc_size);
#else
        return std::aligned_alloc(alignment, alloc_size);
#endif
    }
    void dealloc_raw(void *ptr) {
#ifdef _WIN32
        return _aligned_free(ptr);
#else
        return std::free(ptr);
#endif
     }

    std::uintptr_t encode_ptr_counter(slot *ptr, unsigned int counter) {
        return std::bit_cast<std::uintptr_t>(ptr) | (counter & counter_mask);
    }
    slot *decode_ptr(std::uintptr_t val) {
        return std::bit_cast<slot *>(val & ~counter_mask);
    }

    std::atomic<std::uintptr_t> _empty_slots;
    std::atomic<unsigned int> _counter;

};

template<std::size_t sz, std::size_t alignment>
inline pool_alloc_sz<sz,alignment> pool_alloc_sz<sz,alignment>::instance;


template<typename T, std::uintptr_t alignment = 32>
class pool_alloc: public pool_alloc_sz<sizeof(T), alignment> {
public:

    static pool_alloc &instance() {
        return static_cast<pool_alloc &>(pool_alloc_sz<sizeof(T), alignment>::instance);
    }

    template<typename ... Args>
    T *construct(Args &&... args) {return new(pool_alloc_sz<sizeof(T), alignment>::alloc()) T(std::forward<Args>(args)...);}
    void destroy(T *x) {std::destroy_at(x); pool_alloc_sz<sizeof(T), alignment>::dealloc(x);}
};

template<typename T, std::uintptr_t min_alignment = 32>
class pool_allocated final: public T {
public:
    using T::T;

    void *operator new(std::size_t) {
        return pool_alloc<T, min_alignment>::instance.alloc();
    }
    void operator delete(void *ptr) {
        return pool_alloc<T, min_alignment>::instance.dealloc(ptr);
    }
    void operator delete(void *ptr, std::size_t ) {
        return pool_alloc<T, min_alignment>::instance.dealloc(ptr);
    }
};



}
