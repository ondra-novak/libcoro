#pragma once

namespace coro {

///handles allocation of single coroutine at time and caches the allocated space to be reused
/**
 * It is ideal to allocate frame of the same coroutine again and again, but
 * it keeps allocated space, so actual allocation is performed once
 *
 * You need to ensure, that only one coroutine uses the allocated space at time
 */
class reusabe_allocator {
public:

    void *allocate(std::size_t sz) {
        if (sz > buffer_size) {
            ::operator delete(buffer);
            buffer = ::operator new(sz);
            buffer_size = sz;
        }
        return buffer;
    }

    static void deallocate(void *, std::size_t) {}

    ~reusabe_allocator() {
        ::operator delete(buffer);
    }

    reusabe_allocator() = default;
    reusabe_allocator(const reusabe_allocator &) = delete;
    reusabe_allocator(reusabe_allocator &&other)
        :buffer(other.buffer)
        ,buffer_size(other.buffer_size) {}

    reusabe_allocator &operator=(const reusabe_allocator &) = delete;

    std::size_t get_alloc_size() const {return buffer_size;}

protected:
    void *buffer = nullptr;
    std::size_t buffer_size = 0;
};

///extends reusable_allocator by collision detection i.e. when multiple coroutines wants to use this object
/**
 * The class manages occupience flag. When the space is occupied, allocation
 * is performed by standard memory allocator.
 */
class reusabe_allocator_mt: private reusabe_allocator {
public:

    void *try_allocate(std::size_t sz) {
        if (_occupied.exchange(true)) return nullptr;
        auto needsz = sz + sizeof(reusabe_allocator_mt *);
        void *ptr = reusabe_allocator::allocate(needsz);
        auto myptr = reinterpret_cast<reusabe_allocator_mt **>(
                reinterpret_cast<char *>(ptr)+ sz);
        *myptr = this;
        return ptr;
    }

    void *mem_allocate(std::size_t sz) {
        auto needsz = sz + sizeof(reusabe_allocator_mt *);
        void *ptr = ::operator new(needsz);
        auto myptr = reinterpret_cast<reusabe_allocator_mt **>(
                reinterpret_cast<char *>(ptr)+ sz);
        *myptr = nullptr;
        return ptr;
    }

    static void deallocate(void *ptr, std::size_t sz) {
        auto myptr = reinterpret_cast<reusabe_allocator_mt **>(
                reinterpret_cast<char *>(ptr)+ sz);
        if (myptr) {
            auto my = *myptr;
            my->_occupied.store(false);
        } else {
            ::operator delete(ptr);
        }
    }

    void *allocate(std::size_t sz) {
        void *res = try_allocate(sz);
        if (res) return res;
        return mem_allocate(sz);
    }

protected:
    std::atomic<bool> _occupied = {false};
};

}
