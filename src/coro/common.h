/** @file common.h
 * */
#pragma once

#include <coroutine>
#include <atomic>
#include <concepts>

/**
 * @defgroup Coroutines Coroutines
 * Classes which can be used to write coroutines
 *
 * @code
 * coro::coroutine my_coro1(...) {
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

///Tests, whether T is valid await_suspend return value
template<typename T>
concept await_suspend_valid_return_value = (std::is_void_v<T>||std::is_convertible_v<T, bool>||std::is_convertible_v<T, std::coroutine_handle<> >);

///Tests, whether T is coroutine awaitable
template<typename T>
concept awaitable = requires(T v, std::coroutine_handle<> h) {
    {v.await_ready()}->std::convertible_to<bool>;
    {v.await_suspend(h)}->await_suspend_valid_return_value;
    {v.await_resume()};
};

///Tests, whether T is coroutine awaitable returning given value
template<typename T, typename RetVal>
concept awaitable_r = requires(T v, std::coroutine_handle<> h) {
    {v.await_ready()}->std::convertible_to<bool>;
    {v.await_suspend(h)}->await_suspend_valid_return_value;
    {v.await_resume()}->std::convertible_to<RetVal>;
};


}




