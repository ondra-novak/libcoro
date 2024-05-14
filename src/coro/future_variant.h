#pragma once
#include "future.h"

namespace coro {

/// makes a variant future - multiple futures shares single space
/**
 * It is similar as variant over futures, but it is much easier to use
 * 
 * @tparam Types list of types hosted by this object
 * 
 * You can use operator << to redirect result to this future object. This operator
 * returns a reference to a selected future, which can be used to chain a callback
 * 
 * @code
 * future_variant<int, double> fut;
 * fut << [&]{return async_double();} >> [&]{
 *      auto res = fut.get();
 * } 
 * @endcode;
 * 
 * You can use get_promise to initialize future and get promise at the same time
 * 
 * @code
 * future_variant<int, double> fut;
 * promise<double> prom;
 * auto &f = fut.get_promise(prom);
 *  // f = future<double>;
 * @endcode
 * 
 * @note you can switch future type only when underlying future is resolved 
 */
template<typename ... Types>
class future_variant {
public:

    future_variant() = default;
    future_variant(const future_variant &) = delete;
    future_variant &operator=(const future_variant &) = delete;
    ~future_variant() {
        destroy_fn(buffer);
    }

    /// Redirect return value to future object
    /**
     * @param fn invocable object without arguments, which returns a future or deferred_future,
     * The return value is constructed inside of future_variant instance. Any previous instance
     * is destroyed automatically. 
     * @return reference to newly initialized future object in correct type 
     */  
    template<std::invocable<> Fn> 
    auto &operator<<(Fn &&fn) {
        destroy_fn(buffer);
        using fut_type = std::invoke_result_t<Fn>;
        static_assert(sizeof(fut_type) <= buffer_size, "Returned future is not expected");
        fut_type *ptr = new(buffer) fut_type(fn());
        destroy_fn = [](void *buff){
            fut_type *xptr = reinterpret_cast<fut_type *>(buff);
            std::destroy_at(xptr);
        };
        return *ptr;
    }

    ///Delete any underlying future and return object into uninitalized state
    void reset() {
        destroy_fn(buffer);
        destroy_fn = [](void *){};
    }

    ///Initialize underlying future and retrieves promise
    /**
     * @param p reference to unbound promise object
     * @return reference to associated future after promise is bound to it
     */
    template<typename promise_type>
    auto &get_promise(promise_type &p) {
        using fut_type = typename promise_type::FutureType;
        static_assert(sizeof(fut_type) <= buffer_size, "Future type is not expected");
        fut_type *ptr = new(buffer) fut_type;
        p = ptr->get_promise();
        destroy_fn = [](void *buff){
            fut_type *xptr = reinterpret_cast<fut_type *>(buff);
            std::destroy_at(xptr);
        };
        return *ptr;
        
    }

protected:
    static constexpr auto buffer_size = std::max({sizeof(future<Types>)...});
    void (*destroy_fn)(void *buffer) = [](void *){};
    char buffer[buffer_size];
};


}