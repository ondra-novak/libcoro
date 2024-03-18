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

///await on a condition
/**
 * @ingroup condition
 *
 * @tparam T type of shared variable to monitor
 * @tparam Pred a predicate which tests the condition. The predicate must
 * return true or false and it should test condition related to shared variable
 *
 * @code
 * co_await coro::condition(var, [&]{return var == value;});
 * @endcode
 *
 * The coroutine is suspended until the condition is fullfilled.
 *
 * @see notify_condition
 *
 * @note The class is not MT Safe. The predicate should use proper synchronization
 * if used in multiple threads
 *
 */

template<typename T, typename Pred>
class condition : public _details::abstract_condition_awaiter {
public:

    static_assert(std::is_invocable_r_v<bool, Pred, T &> || std::is_invocable_r_v<bool, Pred>);

    ///constructor
    /**
     * @param var shared variable
     * @param pred predicate which should return boolean if condition is fulfilled
     */
    condition(T &var, Pred &&pred):_variable(var),_predicate(std::forward<Pred>(pred)) {}


    ///@b co_await support
    /**
     * Tests condition.
     * @retval true condition is true - we can continue
     * @retval false condition is false - we must sleep
     */
    bool await_ready() const {
        if constexpr (std::is_invocable_r_v<bool, Pred>) {
            return _predicate();
        } else {
            return _predicate(_variable);
        }
    }

    ///@b co_await support
    /**
     * Returns value of shared variable. However, if the test of condition throws
     * an exception, the exception is rethrown now
     *
     * @return shared variable
     */
    T &await_resume() const {
        if (_exp) std::rethrow_exception(_exp);
        return _variable;
    }

    ///@b co_await support
    /** called by @b co_await when coroutine is suspened */
    bool await_suspend(std::coroutine_handle<> h) {
        this->h = h;
        return _details::awaiter_map::instance.reg_awaiter(&_variable, this);
    }

    ///Synchronous waiting, allows to condition be used in normal function
    /**
     * @code
     * coro::condition(shared, [&]{shared == val}).wait();
     * @endcode
     *
     * @return
     */
    T &wait() const {
        this->n.wait(false);
        return await_resume();
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
 * @see condition
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
 * @see condition
 * @ingroup condition
 */
template<typename T, std::invocable<prepared_coro> Fn>
void notify_condition(const T &var, Fn &&scheduler) noexcept {
    _details::awaiter_map::instance.notify_addr(&var,scheduler);
}


///Perform synchronous waiting with condition
/**
 * @param var shared variable
 * @param pred a predicate testing condition,
 * the function blocks execution, if the condition
 * is not true.
 *
 * @note Not MT Safe. The predicate must use proper synchronization.

 * @see condition
 * @ingroup condition
 */
template<typename T, typename Pred>
void condition_sync_wait(T &var, Pred &&pred) {
    coro::condition<T, Pred> c(var, std::forward<Pred>(pred));
    return c.wait();

}

}





