#pragma once
#ifndef SRC_CORO_FINALLY_H_
#define SRC_CORO_FINALLY_H_

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


#endif /* SRC_CORO_FINALLY_H_ */


