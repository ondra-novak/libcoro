#pragma once

#include "function.h"

#include <coroutine>


namespace coro {

template<typename T>
concept has_co_await_operator = requires(T v){
    {v.operator co_await()};
};


///Creates coroutine compatible memory layout, so the object acts as an coroutine
/**
 * Note this is experimental and undocumented feature. It is highly implemetation depended.
 * Currently only GCC, CLANG and MSC are supported
 *
 * @tparam T class must defined two functions resume() and destroy(). The functions resume() and destroy()
 * are mapped to coroutine_handle<>::resume() and coroutine_handle<>::destroy(). There
 * is no specification, what these function must do.
 *
 * To retrieve handle of such "fake" coroutine, call get_handle()
 *
 */

template<typename T>
class frame {
protected:
    void (*_resume_fn)(std::coroutine_handle<>) = [](std::coroutine_handle<> h) {
        static_assert(requires(T v){{v.resume()};}, "The child class must have T::resume() function");
        auto *me = reinterpret_cast<frame *>(h.address());
        me->do_resume();
    };
    void (*_destroy_fn)(std::coroutine_handle<>) = [](std::coroutine_handle<> h) {
        static_assert(requires(T v){{v.resume()};}, "The child class must have T::destroy() function");
        auto *me = reinterpret_cast<frame *>(h.address());
        auto *content = static_cast<T *>(me);
        content->destroy();
    };

    void do_resume() {
        auto *content = static_cast<T *>(this);
        content->resume();
    }
    ///Sets done flag
    /**
     * This causes that h.done() return true. Note that once coroutine is set as done(), it can't be resumed
     */
    void set_done() {
        _resume_fn = nullptr;
    }
public:
    ///Retrieve coroutine handle of this frame.
    std::coroutine_handle<> get_handle() {
        return std::coroutine_handle<>::from_address(this);
    }

    ///Emulates co_await (without await_resume)
    /**
     * @param awt an compatible awaitable
     *
     * The function tests await_ready() and await_suspend();
     * The frame is resumed through resume() function
     */
    template<typename Awt>
    void await(Awt &awt) {
        if (!try_await(awt)) do_resume();
    }

    ///Emulates co_await (without await_resume) with no suspend when ready
    /**
     * @param awt an compatible awaitable
     * @retval true suspended, resume() or destroy() will be called
     * @retval false already resolved, no suspend needed
     */
    template<typename Awt>
    bool try_await(Awt &awt) {
        if (!awt.await_ready()) {
            auto h = get_handle();
            using Ret = decltype(awt.await_suspend(h));
            if constexpr(std::is_void_v<bool>) {
                if (!awt.await_suspend(h)) {
                    return false;
                }
            } else if constexpr(std::is_convertible_v<Ret, std::coroutine_handle<> >) {
                auto g = awt.await_suspend(h);
                if (g == h) return false;
                g.resume();
            } else {
                awt.await_suspend(get_handle());
            }
            return true;
        } else {
            return false;
        }
    }


};




}
