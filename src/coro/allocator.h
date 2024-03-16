#pragma once

#include <concepts>


namespace coro {

class StdAllocator {};

inline constexpr StdAllocator standard_allocator;

template<typename T>
concept CoroAllocatorLocal = requires(T a, std::size_t sz, void *ptr) {
    {a.alloc(sz)} -> std::same_as<void *>;
    {T::dealloc(ptr, sz)}->std::same_as<void>;
};

template<typename T>
concept CoroAllocatorGlobal = requires(std::size_t sz, void *ptr) {
    {T::alloc(sz)} -> std::same_as<void *>;
    {T::dealloc(ptr, sz)}->std::same_as<void>;
};

template<typename T>
concept CoroAllocator = std::same_as<T, StdAllocator> || std::same_as<T, const StdAllocator>
                    || CoroAllocatorLocal<T> || CoroAllocatorGlobal<T>;


class ReusableAllocator {
public:

    ReusableAllocator() = default;
    ReusableAllocator(ReusableAllocator &) = delete;
    ReusableAllocator &operator=(ReusableAllocator &) = delete;

    void *alloc(std::size_t size) {
        if (sz < size) {
            operator delete(ptr);
            ptr = operator new(sz = size);
        }
        return ptr;
    }

    static void dealloc(void *, std::size_t) {}

protected:
    void *ptr = nullptr;
    std::size_t sz = 0;
};

static_assert(CoroAllocator<ReusableAllocator>);
static_assert(CoroAllocator<StdAllocator>);


template<CoroAllocator Alloc>
class coro_allocator_helper;

template<>
class coro_allocator_helper<StdAllocator> {
public:

};

template<>
class coro_allocator_helper<const StdAllocator> {
public:

};


template<CoroAllocatorLocal Alloc>
class coro_allocator_helper<Alloc> {
public:

    template<typename ... Args>
    void *operator new(std::size_t sz, Alloc &a, Args && ...) {
        return a.alloc(sz);
    }
    template<typename This, typename ... Args>
    void *operator new(std::size_t sz, This &&, Alloc &a, Args && ...) {
        return a.alloc(sz);
    }

    void operator delete(void *ptr, std::size_t sz) {
        Alloc::dealloc(ptr, sz);
    }

private:
    void *operator new(std::size_t sz);

};


template<CoroAllocatorGlobal Alloc>
class coro_allocator_helper<Alloc> {
public:
    void operator delete(void *ptr, std::size_t sz) {
        Alloc::dealloc(ptr, sz);
    }

    void *operator new(std::size_t sz) {
        return Alloc::alloc(sz);
    }
};


}




