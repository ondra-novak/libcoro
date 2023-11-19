#pragma once

#include "types.h"

namespace coro {

template<coro_allocator Alloc>
struct promise_type_alloc_support {


    template<typename ... Args>
    void *operator new(std::size_t sz, Alloc &alloc, Args && ...) {
        return alloc.allocate(sz);
    }

    template<typename This, typename ... Args>
    void *operator new(std::size_t sz, This &, Alloc &alloc, Args && ...) {
        return alloc.allocate(sz);
    }

    void operator delete(void *ptr, std::size_t sz) {
        Alloc::deallocate(ptr, sz);
    }

private:
        void *operator new(std::size_t sz);

};


class standard_allocator {
public:
    static void *allocate(std::size_t sz) {return ::operator new(sz);}
    static void deallocate(void *ptr, std::size_t) {::operator delete(ptr);}
};

}
