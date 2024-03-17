#pragma once

#include "prepared_coro.h"

#include <atomic>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <bit>
#include <optional>

namespace coro {


/**
 * @defgroup condition Condition
 * Set of function that allows to synchronize on single variable
 */

namespace _details {


class abstract_condition_awaiter {
public:
    prepared_coro notify() noexcept {
        n.store(true, std::memory_order_relaxed);
        n.notify_all();
        return prepared_coro(h);
    }
    virtual bool test(const void *addr) noexcept  = 0;
    virtual const void *get_addr() noexcept = 0;
    virtual ~abstract_condition_awaiter() = default;
    abstract_condition_awaiter *_next = nullptr;
protected:
    std::coroutine_handle<> h;
    std::atomic<bool> n = {false};
};




    // Function that returns true if n
    // is prime else returns false
    constexpr bool isPrime(std::size_t n)
    {
        // Corner cases
        if (n <= 1)  return false;
        if (n <= 3)  return true;

        // This is checked so that we can skip
        // middle five numbers in below loop
        if (n%2 == 0 || n%3 == 0) return false;

        for (std::size_t i=5; i*i<=n; i=i+6)
            if (n%i == 0 || n%(i+2) == 0)
               return false;

        return true;
    }

    constexpr std::size_t next_prime_twice_than(std::size_t x) {
        x = x * 2+1;
        if (!(x & 1)) ++x;
        while (!isPrime(x)) {
            x+=2;
        }
        return x;
    }





class awaiter_map {
public:

    bool reg_awaiter(const void *addr, abstract_condition_awaiter *awt) {
        std::lock_guard _(_mx);
        if (awt->test(addr)) return false;
        insert_item(addr, awt);
        return true;
    }

    template<std::invocable<prepared_coro> Fn>
    void notify_addr(const void *addr, Fn &&fn) {
        auto done_lst = get_list_to_notify(addr);
        while (done_lst) {
            auto tmp = done_lst;
            done_lst = done_lst->_next;
            fn(tmp->notify());
        }
    }

    static awaiter_map instance;

protected:

    std::mutex _mx;
    std::vector<abstract_condition_awaiter *> _hashtable;
    std::size_t _count_keys;


    abstract_condition_awaiter * &map_address(const void *address) {
        std::uintptr_t n = std::bit_cast<std::uintptr_t>(address);
        auto pos = n % _hashtable.size();
        return _hashtable[pos];
    }

    void insert_item(const void *address, abstract_condition_awaiter *awt) {
        test_resize();
        auto &b = map_address(address);
        awt->_next = b;
        b = awt;
        if (awt->_next == nullptr) ++_count_keys;
    }

    abstract_condition_awaiter *get_list_to_notify(const void *addr) {
        abstract_condition_awaiter *new_lst = nullptr;
        abstract_condition_awaiter *done_lst = nullptr;

        std::lock_guard _(_mx);
        auto &b = map_address(addr);
        abstract_condition_awaiter *lst = b;
        if (lst == nullptr) return nullptr;
        while (lst != nullptr) {
            auto tmp = lst;
            lst = lst->_next;
            if (tmp->test(addr)) {
                tmp->_next = done_lst;
                done_lst = tmp;
            } else {
                tmp->_next = new_lst;
                new_lst = tmp;
            }
        }
        b = new_lst;
        if (!b) {
            --_count_keys;
        }

        return done_lst;
    }


    void test_resize() {
        auto sz =_hashtable.size();
        if (_count_keys * 2 >= sz) {
            std::size_t newsz = _details::next_prime_twice_than(std::max<std::size_t>(sz, 16));
            std::vector<abstract_condition_awaiter *> tmp(newsz, nullptr);
            std::swap(tmp, _hashtable);
            _count_keys = 0;
            for (auto &b :tmp) {
                while (b) {
                    auto x = b;
                    b = b->_next;
                    insert_item(x->get_addr(), x);
                }
            }
        }
    }



};

inline awaiter_map awaiter_map::instance;


}

///support for notify_condition()
/**
 * @ingroup condition
 */

template<typename T, typename Pred>
class condition_awaiter : public _details::abstract_condition_awaiter {
public:

    condition_awaiter(T &var, Pred &&pred):_variable(var),_predicate(std::forward<Pred>(pred)) {}


    bool await_ready() const {return _predicate(_variable);}
    T &await_resume() const {
        if (_exp) std::rethrow_exception(_exp);
        return _variable;
    }
    bool await_suspend(std::coroutine_handle<> h) {
        this->h = h;
        return _details::awaiter_map::instance.reg_awaiter(&_variable, this);
    }




protected:
    T &_variable;
    Pred _predicate;
    std::exception_ptr _exp;

    virtual bool test(const void *addr) noexcept override{
        try {
            return addr == &_variable && await_ready();
        } catch (...) {
            _exp = std::current_exception();
            return true;
        }
    }
    virtual const void *get_addr() noexcept override {
        return &_variable;
    }

};

///allows to @b co_await on variable while the variable is equal to value
/**
 * awaiting while variable equals value
 *
 * @param var variable
 * @param val value
 * @return awaitable object, a coroutine is resumed, when condition is false
 *
 * @note await when var == val, resume when var != val
 *
 * @ingroup condition awaitable
 * @see notify_condition
 */
template<typename T, typename U>
auto condition_equal(T &var, const U &val) noexcept{
    return condition_awaiter(var, [&val](T &var){return var != val;});
}
///allows to @b co_await on variable while the variable si less than value
/**
 * awaiting while variable is less than value
 *
 *
 *
 * @param var variable
 * @param val value
 * @return awaitable object, a coroutine is resumed, when condition is false
 *
 * @note await when var < val, resume otherwise
 *
 * @ingroup condition awaitable
 * @see notify_condition
 */
template<typename T, typename U>
auto condition_less(T &var, const U &val) noexcept{
    return condition_awaiter(var, [&val](T &var){return var >= val;});
}
///allows to @b co_await on variable while the variable si greater than value
/**
 * awaiting while variable is greater than value
 *
 * @param var variable
 * @param val value
 * @return awaitable object, a coroutine is resumed, when condition is false
 *
 * @note await when var > val, resume otherwise
 *
 * @ingroup condition awaitable
 * @see notify_condition
 */
template<typename T, typename U>
auto condition_greater(T &var, const U &val) noexcept{
    return condition_awaiter(var, [&val](T &var){return var <= val;});
}

///allows to @b co_await on variable while the variable is less or equal to a value
/**
 * awaiting while variable is less or equal to a value
 *
 * @param var variable
 * @param val value
 * @return awaitable object, a coroutine is resumed, when condition is false
 *
 *  * @note await when var <= val, resume otherwise
 * @ingroup condition awaitable
 * @see notify_condition
 *
 */
template<typename T, typename U>
auto condition_less_equal(T &var, const U &val) noexcept{
    return condition_awaiter(var, [&val](T &var){return var > val;});
}

///allows to @b co_await on variable while the variable is greater or equal to a value
/**
 * awaiting while variable is greater or equal to a value
 *
 * @param var variable
 * @param val value
 * @return awaitable object, a coroutine is resumed, when condition is false
 *
 *  * @note await when var >= val, resume otherwise
 *
 * @ingroup condition awaitable
 * @see notify_condition
 */
template<typename T, typename U>
auto condition_greater_equal(T &var, const U &val) noexcept{
    return condition_awaiter(var, [&val](T &var){return var < val;});
}

///notifies variable about change in the condition.
/**
 * @param var reference to shared variable.
 *
 * Tests all conditions and resumes all coroutines, where condition is broken.
 *
 * It is not UB when variable is already destroyed. You can pass anything. If the variable
 * is not awaited, nothing happens.
 *
 * @ingroup condition
 * @see condition_less, condition_equal, condition_greater, condition_less_equal, condition_greater_equal
 */
template<typename T>
void notify_condition(const T &var) noexcept {
    _details::awaiter_map::instance.notify_addr(&var,[](auto){});
}

///notifies variable about change in the condition.
/**
 * @param var reference to shared variable.
 * @param scheduler function which receives prepared_coro, and which is responsible
 * to schedule resumption of the coroutine. Default implementation simply resumes the
 * coroutine, but you can define own behaviour
 *
 * Tests all conditions and resumes all coroutines, where condition is broken.
 *
 * It is not UB when variable is already destroyed. You can pass anything. If the variable
 * is not awaited, nothing happens.
 *
 * @ingroup condition
 * @see condition_less, condition_equal, condition_greater, condition_less_equal, condition_greater_equal
 */
template<typename T, std::invocable<prepared_coro> Fn>
void notify_condition(const T &var, Fn &&scheduler) noexcept {
    _details::awaiter_map::instance.notify_addr(&var,scheduler);
}


}





