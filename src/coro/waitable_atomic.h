#pragma once

//Waitable atomic polyfil for GCC 10

#include <atomic>


 #if defined(__GNUC__) && (__GNUC__ < 11)

#include <condition_variable>

template<typename T>
class waitable_atomic: public std::atomic<T> {
public:
    using std::atomic<T>::atomic;

    void wait(const T &val, std::memory_order order = std::memory_order_seq_cst);

    void notify_all();


};


struct ConditionMap {
    static constexpr int slot_count = 61;
    std::condition_variable _cond[slot_count];
    std::mutex _mx;

    int address2index(const void *addr) {
        auto p = reinterpret_cast<std::uintptr_t>(addr);
        auto idx = (p / sizeof(void *)) % slot_count;
        return idx;
    }

    template<typename Pred>
    void wait_on_addr(void *addr, Pred &&pred) {
        int index = address2index(addr);
        std::unique_lock lk(_mx);
        _cond[index].wait(lk, std::forward<Pred>(pred));
    }

    void notify_on_addr(void *addr) {
        int index = address2index(addr);
        _cond[index].notify_all();
    }
};

inline ConditionMap __condition_var_map;

template<typename T>
void waitable_atomic<T>::wait(const T &val, std::memory_order order) {
    if (val == this->load(order)) {
        __condition_var_map.wait_on_addr(this, [&]{
            return val != this->load(order);
        });
    }
}

template<typename T>
void waitable_atomic<T>::notify_all() {
    __condition_var_map.notify_on_addr(this);
}

#else

template<typename T>
using waitable_atomic = std::atomic<T>;

#endif
