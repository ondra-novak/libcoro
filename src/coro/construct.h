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
 * co_return coro::construct([&]{return unmovable_object(arg1,arg2);});
 * @endif
 */
template<std::invocable<> Fn>
class construct {
public:
    using value_type = std::invoke_result_t<Fn>;

    construct(Fn &fn):_fn(fn) {}
    construct(Fn &&fn):_fn(fn) {}

    operator value_type() const {
        return _fn();
    }

protected:
    Fn &_fn;
};

}


