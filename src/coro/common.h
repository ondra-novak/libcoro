#pragma once

#include <coroutine>
#include <atomic>

/**
 * @defgroup Coroutines Coroutines
 * Classes which can be used to write coroutines
 *
 * @code
 * coro::coro my_coro1(...) {
 *      co_await...;
 *      co_return...;
 * }
 *
 * coro::async<int> my_coro2(...) {
 *      co_await...;
 *      co_return...;
 * }
 *
 * coro::future<int> my_coro3(...) {
 *      co_await...;
 *      co_return...;
 * }
 * @endcode
 */

/**
 * @defgroup awaitable Awaitables
 * Objects that can be awaited in coroutines
 */

/**
 * @defgroup allocators Allocators
 * Allocators for coroutines
 */


/**
 * @defgroup tools Tools
 * Helper classes or functions
 */

/**
 * @defgroup exceptions Exceptions
 */

///main namespace
namespace coro {


}




