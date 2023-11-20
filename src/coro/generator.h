#pragma once

#include "future.h"
#include "allocator.h"

namespace coro {

template<typename Src>
class generator_iterator {
public:


    using storage_type = std::invoke_result_t<Src>;

    using iterator_category = std::input_iterator_tag;
    using value_type = typename std::decay_t<Src>::value_type;
    using reference = std::add_lvalue_reference_t<value_type>;
    using difference_type = std::ptrdiff_t;
    using pointer = std::add_pointer_t<value_type>;

    reference operator*() const {
        return _stor;
    }    

    pointer operator->() const {
        reference &r = _stor;
        return &r;
    }

    generator_iterator &operator++() {
        _stor = _src();
        _is_end = !_stor;
        return *this;
    }

    bool operator==(const generator_iterator &other) const {
        return _is_end == other._is_end;
    }

    static generator_iterator begin(Src src) {
        generator_iterator ret(src, false);
        ++ret;
        return ret;
    }

    static generator_iterator end(Src src) {
        return generator_iterator(src, true);
    }

protected:
    Src _src;
    bool _is_end;
    mutable storage_type _stor;

    generator_iterator(Src src, bool is_end):_src(src), _is_end(is_end) {
        
    }

};

template<typename T, coro_optional_allocator Alloc = void>
class generator;

template<typename T>
class generator<T, void> {
public:

    using value_type = T;
    using reference = std::add_lvalue_reference_t<T>;

    struct promise_type {

        typename lazy_future<T>::promise _awaiter;
        std::optional<value_type> _value_storage;
        std::exception_ptr _cur_exception;

        constexpr std::suspend_always initial_suspend() const {return {};}

        struct yield_suspender: std::suspend_always {
            reference _value;
            yield_suspender(reference v):_value(v) {}
            template<typename X>
            std::coroutine_handle<> await_suspend(std::coroutine_handle<X> me) {
                promise_type &p = me.promise();
                return p._awaiter(std::forward<value_type>(_value)).on_await_suspend();
            }
        };

        yield_suspender yield_value(reference v) {
            return yield_suspender(v);
        }

        template<std::convertible_to<value_type> From>
        yield_suspender yield_value(From &&v) {
            _value_storage.emplace(std::forward<From>(v));
            return yield_suspender(*_value_storage);
        }

        struct final_suspender: std::suspend_always {
            template<typename X>
            std::coroutine_handle<> await_suspend(std::coroutine_handle<X> me) noexcept {
                promise_type &p = me.promise();
                std::coroutine_handle<> h;
                if (p._cur_exception) {
                    h = p._awaiter.reject(p._cur_exception).on_await_suspend();
                } else {
                    h = p._awaiter.drop().on_await_suspend();
                }
                return h;
            }
        };

        final_suspender final_suspend() noexcept {return {};}

        void return_void() {}
        void unhandled_exception() {
            _cur_exception = std::current_exception();
        }

        bool is_done()  {
            return std::coroutine_handle<promise_type>::from_promise(*this).done();
        }

        generator get_return_object() {return {this};}

    };

    lazy_future<T> operator()() {
        using lpromise = typename lazy_future<T>::promise;
        if (_prom->is_done()) return {};
        target_simple_activation(_next_target, [prom = _prom.get()](lpromise &&promise){
            auto h = std::coroutine_handle<promise_type>::from_promise(*prom);
            if (!h.done()) {
                if (!prom->_awaiter) {
                    prom->_awaiter = std::move(promise);
                    return h;
                }
            }
            return std::coroutine_handle<promise_type>();
        });
        return _next_target;
    }

    explicit operator bool() const {
        return !_prom->is_done();
    }


    generator() = default;
    generator(generator &&x):_prom(std::move(x._prom)) {}


    auto begin() {
        return generator_iterator<generator &>::begin(*this);
    }
    auto end() {
        return generator_iterator<generator &>::end(*this);
    }

protected:

    generator(promise_type *p):_prom(p) {}

    struct deleter {
        void operator()(promise_type *x) {
            auto h = std::coroutine_handle<promise_type>::from_promise(*x);
            h.destroy();
        }
    };

    std::unique_ptr<promise_type,deleter> _prom;
    typename lazy_future<T>::promise_target_type _next_target;

};

template<typename T, coro_optional_allocator Alloc>
class generator : public generator<T, void>{
public:

    struct promise_type : generator<T, void>::promise_type
                        , promise_type_alloc_support<Alloc> {

        generator<T, Alloc> get_return_object() {return {this};}

    };

protected:
    generator(promise_type *p): generator<T, void>(p) {}

};


}
