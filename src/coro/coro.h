#pragma once
#include "allocator.h"
#include <coroutine>
#include <exception>

namespace coro {

///COROUTINE: Basic coroutine
/**
 * @ingroup Coroutines
 * @see coro
 * @tparam Alloc allocator
 */
template<coro_allocator Alloc = std_allocator>
class basic_coro {
public:
    class promise_type: public coro_allocator_helper<Alloc> {
    public:
        static constexpr std::suspend_never initial_suspend() noexcept {return {};}
        static constexpr std::suspend_never final_suspend() noexcept {return {};}
        static constexpr void return_void() {}
        static void unhandled_exception() {std::terminate();}
        basic_coro get_return_object() const {return {};}
    };
};


///COROUTINE:  Basic coroutine, always detached, with no return value
/**
 * @ingroup Coroutines
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
using coro = basic_coro<std_allocator>;

}
