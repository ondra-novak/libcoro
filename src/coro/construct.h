#pragma once

#include <concepts>

namespace coro {

///Constructor for emplace
/**
 * @tparam Fn function which returns value to emplace or co_return
 *
 * following code constructs result directly in output future
 *
 * @code
 * co_return coro::construct_using([&]{return unmovable_object(arg1,arg2);});
 * @endcode
 *
 * @ingroup utils
 */
template<std::invocable<> Fn>
class construct_using {
public:
    using value_type = std::invoke_result_t<Fn>;

    construct_using(Fn &fn):_fn(fn) {}
    construct_using(Fn &&fn):_fn(fn) {}

    operator value_type() const {
        return _fn();
    }

protected:
    Fn &_fn;
};

}


