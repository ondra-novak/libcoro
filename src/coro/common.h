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
 * @defgroup trace Trace
 * API to trace coroutine execution
 */

/**
 * @defgroup exceptions Exceptions
 */



///main namespace
namespace coro {

///Tests, whether T is valid await_suspend return value
template<typename T>
concept await_suspend_valid_return_value = (std::is_void_v<T>||std::is_convertible_v<T, bool>||std::is_convertible_v<T, std::coroutine_handle<> >);

template<typename T>
concept directly_awaitable = requires(T v, std::coroutine_handle<> h) {
    {v.await_ready()}->std::convertible_to<bool>;
    {v.await_suspend(h)}->await_suspend_valid_return_value;
    {v.await_resume()};
};

template<typename T>
concept indirectly_awaitable = requires(T v) {
    {v.operator co_await()} -> directly_awaitable;
};

///Tests, whether T is coroutine awaitable
template<typename T>
concept awaitable = directly_awaitable<T> || indirectly_awaitable<T>;

template<typename T, typename RetVal>
concept directly_awaitable_r = requires(T v, std::coroutine_handle<> h) {
    {v.await_ready()}->std::convertible_to<bool>;
    {v.await_suspend(h)}->await_suspend_valid_return_value;
    {v.await_resume()}->std::convertible_to<RetVal>;
};

template<typename T, typename RetVal>
concept indirectly_awaitable_r = requires(T v) {
    {v.operator co_await()} -> directly_awaitable_r<RetVal>;
};

template<typename T, typename RetVal>
concept awaitable_r = directly_awaitable_r<T,RetVal> || indirectly_awaitable_r<T,RetVal>;

template<typename T>
struct awaitable_result_impl;

template<directly_awaitable T>
struct awaitable_result_impl<T> {
    using type = decltype(std::declval<T>().await_resume());
};

template<indirectly_awaitable T>
struct awaitable_result_impl<T> {
    using type = typename awaitable_result_impl<decltype(std::declval<T>().operator co_await())>::type;
};

template<awaitable T>
using awaitable_result = typename awaitable_result_impl<T>::type;


#ifdef __clang__
#define CORO_OPT_BARRIER [[clang::optnone]]
#else
///marks function which servers as barrier between suspended coroutine and normal code
/**
 * Some compilers (clang) have too aggresive optimization which leads to
 * declaring stack variables in coroutine frame, which can lead to
 * crash,when these variables are overwritten after nested resume. This
 * option disables optimization for the barrier function, which
 * prevents compiler to reuse space allocated in coroutine frame
 */
#define CORO_OPT_BARRIER
#endif

}




