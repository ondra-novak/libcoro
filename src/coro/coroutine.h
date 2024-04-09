#pragma once
#include "allocator.h"
#include <coroutine>
#include <exception>

namespace coro {

///COROUTINE: Basic coroutine
/**
 * @ingroup Coroutines
 * @see coroutine
 * @tparam Alloc allocator
 *
 *
 * @code
 * coro::basic_coroutine<coro::pool_alloc> my_coroutine(...args...) {
 *      co_await...;
 * }
 * @endcode
 *
 * @code
 * coro::basic_coroutine<coro::reusable_allocator> my_coroutine(coro::reusable_allocator &, ...args...) {
 *      co_await...;
 * }
 * @endcode
 */
template<coro_allocator Alloc = std_allocator>
class basic_coroutine {
public:
    class promise_type: public coro_allocator_helper<Alloc> {
    public:
        static constexpr std::suspend_never initial_suspend() noexcept {return {};}
        static constexpr std::suspend_never final_suspend() noexcept {return {};}
        static constexpr void return_void() {}
        static void unhandled_exception() {std::terminate();}
        basic_coroutine get_return_object() const {return {};}
    };
};


///COROUTINE:  Basic coroutine, always detached, with no return value
/**
 * @ingroup Coroutines
 * @code
 * coro::coroutine my_coroutine(... args...) {
 *     ....
 *     co_await ...
 *     ....
 * }
 * @endcode
 *
 * @note assume noexcept. When exception is thrown, std::terminate() is called
 */
using coroutine = basic_coroutine<std_allocator>;

}
