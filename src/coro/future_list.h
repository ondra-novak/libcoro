#pragma once

#include "future.h"

namespace coro {


template<typename T>
class pointer_wrapper {
public:

    constexpr pointer_wrapper(T &item):_ptr(&item) {}

    constexpr T &operator* () const {return *_ptr;}
    constexpr T *operator->() const {return _ptr;}
    constexpr operator T &() const {return *_ptr;}
    constexpr T *get() const {return *_ptr;}

protected:
    T *_ptr;
};

template<typename T>
using pointer_value = std::decay_t<decltype(*std::declval<T>())>;

template<typename T>
concept iterator_type = requires(T v) {
    {*v};
    {++v};
    {v == v};
};

template<typename T>
concept container_type = requires(T v) {
    {v.begin() == v.end()};
    {v.begin()}->iterator_type;
    {v.end()}->iterator_type;
    typename std::decay_t<T>::value_type;
};



template<typename T>
class all_of : public future<void> {
public:

    all_of(std::initializer_list<pointer_wrapper<T> > lst)
        :all_of(lst.begin(), lst.end()) {}

    template<container_type Container>
    all_of(Container &&cont)
        :all_of(cont.begin(), cont.end()) {}

    template<iterator_type Iter>
    all_of(Iter from, Iter to) {
        _prom = this->get_promise();
        while (from != to) {
            (*from)->then([this]{finish();});
            ++from;
            ++_count;
        }
        finish();
    }


protected:
    promise<void> _prom = {};
    std::atomic<unsigned int> _count = {1};
    void finish() {
        if (--_count == 0) {
            _prom();
        }
    }
};

template<typename X>
all_of(std::initializer_list<X>) -> all_of<X>;


template<typename T>
class any_of : public future<std::add_lvalue_reference_t<typename T::value_type> > {
public:


    using super = future<std::add_lvalue_reference_t<typename T::value_type> >;
    using value_type = typename super::value_type;
    using future_ptr = std::add_pointer_t<T>;


    any_of(std::initializer_list<pointer_wrapper<T> > lst)
        :any_of(lst.begin(), lst.end()) {}

    template<container_type Container>
    any_of(Container &&cont)
        :any_of(cont.begin(), cont.end()) {}

    template<iterator_type Iter>
    any_of(Iter from, Iter to) {
        _prom = this->get_promise();
        _cleanup = [from, to]() mutable {
            do_cleanup(from, to);
        };
        while (from != to) {
            future_ptr ptr = &(*(*from));
            ptr->then([this, ptr]{finish(ptr);});
            ++from;
        }
        future_ptr need = nullptr;
        if (!_selected.compare_exchange_strong(need, do_cleanup_ptr())) {
            _cleanup();
        }
    }

protected:
    promise<value_type> _prom = {};
    std::atomic<future_ptr> _selected= {nullptr};
    function<void()> _cleanup;

    static future_ptr do_cleanup_ptr() {return reinterpret_cast<future_ptr>(0x1);}

    void finish(future_ptr ptr) {
        auto r = _selected.exchange(ptr);
        if (r == do_cleanup_ptr()) {
            _cleanup();
        } else if (r != nullptr) {
            return;
        }
        try {
            if (ptr->has_value()) {
                _prom(ptr->get());
            } else {
                _prom.cancel();
            }
        } catch (...) {
            _prom.reject();
        }
    }

    template<typename Iter>
    static void do_cleanup(Iter &from, Iter &to) {
        while(from != to) {
            (*from)->then([]()->prepared_coro {return {};});
            ++from;
        }
    }
};




template<typename X>
any_of(std::initializer_list<X>) -> any_of<X>;
template<container_type T>
any_of(T) -> any_of<pointer_value<typename T::value_type> >;

}



