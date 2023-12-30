#pragma once

#include "waitable_atomic.h"

namespace coro {


template<std::size_t sz>
class pool_alloc_sz {
public:

    union slot {
        std::uintptr_t next;
        char space[sz];
    };


    void *alloc() {
        auto need = _empty_slots.load(std::memory_order_relaxed);
        slot *s;
        do {
            if (need == 0) return alloc_raw();
            s = decode_ptr(need);
        } while (!_empty_slots.compare_exchange_strong(need, s->next, std::memory_order_relaxed));
        return s;
    }

    void dealloc(void *ptr) {
        slot *s = reinterpret_cast<slot *>(ptr);
        std::uintptr_t newval = encode_ptr_counter(s, _counter++);
        s->next = _empty_slots.load(std::memory_order_relaxed);
        while(!_empty_slots.compare_exchange_strong(s->next, newval, std::memory_order_relaxed));
    }


protected:
    static constexpr unsigned int _counter_bytes = 5;
    static constexpr unsigned int _align = 1<<_counter_bytes;
    static constexpr unsigned int _counter_mask = _align - 1;
    static constexpr unsigned int _align_sz = ((sz - 1)/_align + 1) * _align;
    void *alloc_raw() {
#ifdef _WIN32
        return _aligned_alloc(_counter_bytes , _align_sz);
#else
        return std::aligned_alloc(_counter_bytes , _align_sz);
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
        return std::bit_cast<std::uintptr_t>(ptr) | (counter & _counter_mask);
    }
    slot *decode_ptr(std::uintptr_t val) {
        return std::bit_cast<slot *>(val & ~_counter_mask);
    }

    std::atomic<std::uintptr_t> _empty_slots;
    std::atomic<unsigned int> _counter;
    bool _valid = true;
};

}
