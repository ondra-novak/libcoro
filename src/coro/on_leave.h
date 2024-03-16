#pragma once

#include <algorithm>
#include <concepts>

namespace coro {

///Defines function, which is called when function is exited
/**
 * @code
 * on_leave lv = [&] {
 *      //..code...
 * };
 * @endcode
 *
 * This tool can be used especially in generators. The function is called
 * also before the generator is destroyed.
 *
 * @tparam Fn function
 * @ingroup tools
 */
template<std::invocable<> Fn>
class on_leave {
public:
    on_leave(Fn &&fn):_fn(std::forward<Fn>(fn)) {}
    ~on_leave() {_fn();}

protected:
    Fn _fn;
};


}



