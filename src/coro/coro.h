#pragma once
#include "allocator.h"
#include <coroutine>
#include <exception>

namespace coro {

template<CoroAllocator Alloc = StdAllocator>
class coro_a {
public:
    class promise_type: public coro_allocator_helper<Alloc> {
    public:
        static constexpr std::suspend_never initial_suspend() noexcept {return {};}
        static constexpr std::suspend_never final_suspend() noexcept {return {};}
        static constexpr void return_void() {}
        static void unhandled_exception() {std::terminate();}
        coro_a get_return_object() const {return {};}
    };
};


///Very basic coroutine, always detached, with no return value
/**
 * @code
 * coro::coro my_coroutine(... args...) {
 *     ....
 *     co_await ...
 *     ....
 * }
 * @endcode
 *
 * @note assume noexcept. When exception is thrown, std::terminate() is called
 */
using coro = coro_a<StdAllocator>;

}
