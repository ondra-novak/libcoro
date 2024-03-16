#pragma once

#include <concepts>


namespace coro {

class std_allocator {};

inline constexpr std_allocator standard_allocator;

template<typename T>
concept coro_allocator_local = requires(T a, std::size_t sz, void *ptr) {
    {a.alloc(sz)} -> std::same_as<void *>;
    {T::dealloc(ptr, sz)}->std::same_as<void>;
};

template<typename T>
concept coro_allocator_global = requires(std::size_t sz, void *ptr) {
    {T::alloc(sz)} -> std::same_as<void *>;
    {T::dealloc(ptr, sz)}->std::same_as<void>;
};

template<typename T>
concept coro_allocator = std::same_as<T, std_allocator> || std::same_as<T, const std_allocator>
                    || coro_allocator_local<T> || coro_allocator_global<T>;


class reusable_allocator {
public:

    reusable_allocator() = default;
    reusable_allocator(reusable_allocator &) = delete;
    reusable_allocator &operator=(reusable_allocator &) = delete;

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

static_assert(coro_allocator<reusable_allocator>);
static_assert(coro_allocator<std_allocator>);


template<coro_allocator Alloc>
class coro_allocator_helper;

template<>
class coro_allocator_helper<std_allocator> {
public:

};

template<>
class coro_allocator_helper<const std_allocator> {
public:

};


template<coro_allocator_local Alloc>
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


template<coro_allocator_global Alloc>
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




