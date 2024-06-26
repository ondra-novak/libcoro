#pragma once

#include <concepts>
#include <stdexcept>
#include "trace.h"

namespace coro {

///represents standard allocator for coroutines
/**
 * This class is empty, just activates usage of standard allocator.
 *
 * You can use global (constexpr) instance standard_allocator
 * @ingroup allocators
 */
class std_allocator {};

///Global instance for std_allocator which can be used anywhere the allocator is requested
/**
 * @ingroup allocators
 */
inline constexpr std_allocator standard_allocator;

template<typename T>
concept coro_allocator_global = requires(std::size_t sz, void *ptr) {
    {T::alloc(sz)} -> std::same_as<void *>;
    {T::dealloc(ptr, sz)}->std::same_as<void>;
};

template<typename T>
concept coro_allocator_local = (!coro_allocator_global<T> && requires(T a, std::size_t sz, void *ptr) {
    {a.alloc(sz)} -> std::same_as<void *>;
    {T::dealloc(ptr, sz)}->std::same_as<void>;
});


template<typename T>
concept coro_allocator = std::same_as<T, std_allocator> || std::same_as<T, const std_allocator>
                    || coro_allocator_local<T> || coro_allocator_global<T>;


///Handles allocation of single coroutine, if it is repeatedly allocated and deallocated
/**
 * Holds allocated space. This can be useful in cycles, where a coroutine is repeatedly called.
 * You can preserve allocated space for each loop and avoid costly allocations.
 *
 * @ingroup allocators
 */
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


///inherit this class to include coro_allocator into your promise_type
template<coro_allocator Alloc>
class coro_allocator_helper;

template<>
class coro_allocator_helper<std_allocator> {
public:
#ifdef LIBCORO_ENABLE_TRACE
    void *operator new(std::size_t sz) {
        void *ptr = ::operator new(sz);
        trace::on_create(ptr, sz);
        return ptr;
    }
    void operator delete(void *ptr, std::size_t sz) {
        trace::on_destroy(ptr, sz);
        ::operator delete(ptr);
    }
#endif
};

template<>
class coro_allocator_helper<const std_allocator>: public coro_allocator_helper<std_allocator> {
public:

};


template<coro_allocator_local Alloc>
class coro_allocator_helper<Alloc> {
public:


    void operator delete(void *ptr, std::size_t sz) {
        trace::on_destroy(ptr, sz);
        Alloc::dealloc(ptr, sz);
    }

    template<typename ... Args>
    void *operator new(std::size_t sz, Alloc &a, Args  && ...) {
        void *ptr = a.alloc(sz);
        trace::on_create(ptr, sz);
        return ptr;
    }

    template<typename ... Args>
    void operator delete(void *, Alloc &, Args  && ...) {
        throw std::logic_error("unreachable");
    }


    template<typename This, typename ... Args>
    void *operator new(std::size_t sz, This &, Alloc &a, Args && ...) {
        void *ptr = a.alloc(sz);
        trace::on_create(ptr, sz);
        return ptr;
    }

    template<typename This, typename ... Args>
    void operator delete(void *, This &, Alloc &, Args && ...) {
        throw std::logic_error("unreachable");
    }

#ifndef __clang__
    //clang 15+ doesn't like operator new declared as private
    //so we left it undefined. This ensures that standard allocator will not be used
private:
#endif
    void *operator new(std::size_t sz);



};

template<coro_allocator_global Alloc>
class coro_allocator_helper<Alloc> {
public:
    void operator delete(void *ptr, std::size_t sz) {
        trace::on_destroy(ptr, sz);
        Alloc::dealloc(ptr, sz);
    }

    void *operator new(std::size_t sz) {
        void *ptr = Alloc::alloc(sz);;
        trace::on_create(ptr, sz);
        return ptr;
    }
};

template<typename T>
concept memory_resource_pointer = requires(T x, std::size_t sz, void *ptr) {
    {x->allocate(sz)} ->std::same_as<void *>;
    {x->deallocate(ptr,sz)} ->std::same_as<void>;
    requires std::copy_constructible<T>;
};


///Creates `libcoro` compatible allocator which uses an instance of std::pmr::memory_resource for allocations
/**
 * @tparam Res pointer to memory resource, it can be also any smart pointer
 * which acts as pointer (defines ->).  You can use std::shared_ptr which causes
 * that memory resource is automatically released when last coroutine is
 * finished
 *
 * @code
 * coro::async<int, coro::pmr_allocator<std::pmr::memory_resource *> >
 *          my_coro(coro::pmr_allocator<std::pmr::memory_resource *>, int arg) {
 *
 *
 * }
 * @endcode
 * @ingroup allocators
 *
 */
template<memory_resource_pointer Res>
class pmr_allocator {
public:
    template<std::convertible_to<Res> T>
    pmr_allocator(T &&resource):_memory_resource(std::forward<Res>(resource)) {}

    void *alloc(std::size_t sz) {
        auto needsz = sz + sizeof(Res);
        void *ptr = _memory_resource->allocate(needsz);
        Res *resptr = reinterpret_cast<Res *>(reinterpret_cast<char *>(ptr)+sz);
        std::construct_at(resptr, _memory_resource);
        return ptr;
    }

    static void dealloc(void *ptr, std::size_t sz) {
        auto needsz = sz + sizeof(Res);
        Res *resptr = reinterpret_cast<Res *>(reinterpret_cast<char *>(ptr)+sz);
        Res memory_res ( std::move(*resptr));
        std::destroy_at(resptr);
        memory_res->deallocate(ptr, needsz);
    }

protected:
    Res _memory_resource;
};

}




