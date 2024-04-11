#pragma once

#include "future.h"
#include "queue.h"

#include <condition_variable>
namespace coro {


///Wraps object into pointer
/**
 * Use on initialization, where pointer is expected, but we want to set it as reference
 *
 * @code
 * int v = 10;
 * pointer_wrapper<int> pwv(v);
 * int pb = pwv.get();
 * @endcode;
 *
 * The main benefit is, that defines operator *
 *
 * @tparam T type
 */
template<typename T>
class pointer_wrapper {
public:

    constexpr pointer_wrapper(T &item):_ptr(&item) {}

    constexpr T &operator* () const {return *_ptr;}
    constexpr T *operator->() const {return _ptr;}
    constexpr operator T &() const {return *_ptr;}
    constexpr T *get() const {return *_ptr;}

    explicit constexpr operator bool() const {return _ptr != nullptr;}

    friend constexpr bool operator==(const pointer_wrapper<T> &a, T *b) {
        return a._ptr == b;
    }
    friend constexpr bool operator==(T *a, const pointer_wrapper<T> &b) {
        return a == b._ptr;
    }
    friend constexpr bool operator==(const pointer_wrapper<T> &a, const pointer_wrapper<T> &b) {
        return a._ptr == b._ptr;
    }

protected:
    T *_ptr;
};

///determines type of pointer value
template<typename T>
using pointer_value = std::decay_t<decltype(*std::declval<T>())>;


///any iterator
template<typename T>
concept iterator_type = requires(T v) {
    {*v};
    {++v};
    {v == v};
};

///any container (must have begin and end)
template<typename T>
concept container_type = requires(T v) {
    {v.begin() == v.end()};
    {v.begin()}->iterator_type;
    {v.end()}->iterator_type;
    typename std::decay_t<T>::value_type;
};


///this awaitable is resolved, when all objects specified by constructor are resolved
/**
 * The list is defined as an container of pointer to these objects. However for
 * initializer list, you don't need to specify list of pointers
 *
 * @code
 * co_await all_of({f1,f2,f3,f4});
 *
 * std::vector<future<int> *> tasks;
 * co_await all_of(tasks);
 * @endcode
 *
 * @tparam T type of object
 */
template<typename T>
class all_of : public future<void> {
public:

    ///construct using initializer list
    /**
     * @param lst {f1,f2,f3,f4}
     */
    all_of(std::initializer_list<pointer_wrapper<T> > lst)
        :all_of(lst.begin(), lst.end()) {}

    ///construct using a container
    /**
     * @param cont container
     */
    template<container_type Container>
    all_of(Container &&cont)
        :all_of(cont.begin(), cont.end()) {}

    ///construct using iterator pair
    /**
     * @param from start range
     * @param to end range
     */

    template<iterator_type Iter>
    all_of(Iter from, Iter to) {
        _prom = this->get_promise();
        while (from != to) {
            ++_count;
            (*from)->then([this]{finish();});
            ++from;
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




///Process all futures in order of completion
/**
 * @code
 * for (auto fut: each_of({f1,f2,f3,f4}) {
 *      auto result = co_await fut;
 *      //use result
 * }
 * @endcode
 *
 * @tparam T type of future object (example each_of<future<int> >)
 */
template<typename T>
class each_of {
public:

    using result_future_type = future<std::add_lvalue_reference_t<typename T::value_type> >;

    using future_ptr = std::add_pointer_t<T>;

    using result_promise_type = typename result_future_type::promise_t;
    using result_notify_type = typename result_promise_type::notify;


    ///iterator for each_of - it always return awaitable reference (future)
    /**
     * The iterator is random access iterator. Underlying container is ordered in order
     * of completion. You can access not yet complete item, you only need to co_await on
     * it. Once the item is complete, you can retrieve its value.
     */
    class iterator {
    public:

        using iterator_category = std::random_access_iterator_tag;
        using value_type = typename result_future_type::value_type;
        using reference = result_future_type;
        using difference_type = std::ptrdiff_t;


        ///default construct - note such iterator cannot be used for iterating
        iterator() = default;
        ///retrieve item. Note that result is awaitable reference. You need co_await the result;
        result_future_type operator *() const {
            return [&](auto promise) {
                _prom = std::move(promise);
                _owner->charge(this);
            };
        }

        ///go to next item
        iterator &operator++() {_index++;return *this;}
        ///go to next item
        iterator operator++(int) {return iterator(_owner, _index++);}
        ///go to previous item
        iterator &operator--() {_index--;return *this;}
        ///go to previous item
        iterator operator--(int) {return iterator(_owner, _index--);}
        ///skip n items
        iterator &operator+=(std::ptrdiff_t n) {_index += n;return *this;}
        ///skip n items
        iterator &operator-=(std::ptrdiff_t n) {_index -= n;return *this;}
        ///difference
        std::ptrdiff_t operator-(const iterator &other) const {
            return static_cast<std::ptrdiff_t>(_index)-static_cast<std::ptrdiff_t>(other._index);
        }
        ///comparison
        bool operator==(const iterator &other) const {return operator-(other) == 0;}


    protected:
        iterator(each_of *owner, std::size_t index):_owner(owner),_index(index) {}

        each_of *_owner = nullptr;
        std::size_t _index = 0;
        mutable result_promise_type _prom;

        friend class each_of;

    };

    ///retrieve iterator
    /**
     * @return
     */
    iterator begin() {return {this,0};}
    ///retrieve end iterator
    iterator end() {return {this,_sep};}

    ///construct using initializer list
    /**
     * @param lst {f1,f2,f3,f4}
     */
    each_of(std::initializer_list<pointer_wrapper<T> > lst)
        :each_of(lst.begin(), lst.end()) {}

    ///construct using a container
    /**
     * @param cont container
     */
    template<container_type Container>
    each_of(Container &&cont)
        :each_of(cont.begin(), cont.end()) {}

    ///construct using iterator pair
    /**
     * @param from start range
     * @param to end range
     */
    template<iterator_type Iter>
    each_of(Iter from, Iter to) {
        std::lock_guard _(_mx);
        while (from != to) {
            future_ptr ptr = &(*(*from));
            _awaiting.push_back(ptr);
            ++from;
        }
        _sep = _awaiting.size();
        for (std::size_t i = 0; i < _sep; ++i) {
            future_ptr ptr = _awaiting[i];
            if (!ptr->set_callback([this, ptr]{
                return finish(ptr).symmetric_transfer();
            })) {
                _awaiting.push_back(ptr);

            }
        }

    }

    ~each_of() {
        cleanup();
    }


protected:
    std::vector<future_ptr> _awaiting;
    std::basic_string<const iterator *> _iters;
    std::size_t _sep;
    mutable std::mutex _mx;
    std::condition_variable *_cond = nullptr;

    auto remain_pending() {
        return 2*_sep - _awaiting.size();
    }

    result_notify_type finish(future_ptr ptr) {
        std::lock_guard _(_mx);
        auto idx = _awaiting.size() - _sep;
        _awaiting.push_back(ptr);
        result_promise_type proms;
        _iters.erase(std::remove_if(_iters.begin(), _iters.end(), [&](const iterator *it){
            if (it->_index == idx) {
                proms += it->_prom;
                return true;
            }
            return false;
        }));
        if (_cond) _cond->notify_all();;
        if (proms) return ptr->forward_to(proms);
        return {};
    }


    bool charge(const iterator *it) {
        result_notify_type ntf;
        std::lock_guard _(_mx);
        auto idx = it->_index+_sep;
        if (idx < _awaiting.size()) {
            ntf = _awaiting[idx]->forward_to(it->_prom);
            return false;
        }
        _iters.push_back(it);
        return true;
    }

    void cleanup() {
        if (remain_pending()) {
            std::unique_lock lk(_mx);
            std::size_t found_remain = 0;
            for (std::size_t i = 0; i < _sep; ++i) {
                if (_awaiting[i]->set_callback([]{})) ++found_remain;
            }
            if (remain_pending() != found_remain) {
                std::condition_variable cond;
                _cond = &cond;
                cond.wait(lk, [&]{
                    return remain_pending() == found_remain;
                });
            }
        }
    }

};

template<typename X>
each_of(std::initializer_list<X>) -> each_of<X>;
template<container_type T>
each_of(T) -> each_of<pointer_value<typename T::value_type> >;


///Awaitable returns result of a first complete object
/**
 * @code
 * int res = co_await any_of({f1,f2,f3...});
 * @endcode
 *
 * @tparam T type of object.
 *
 * @note for containers, it expects list of pointers
 *
 */
template<typename T>
class any_of : public each_of<T>::result_future_type {
public:

    any_of(std::initializer_list<pointer_wrapper<T> > lst)
        :any_of(lst.begin(), lst.end()) {}

    template<container_type Container>
    any_of(Container &&cont)
        :any_of(cont.begin(), cont.end()) {}

    template<iterator_type Iter>
    any_of(Iter from, Iter to)
        :_each_of(std::move(from), std::move(to)) {

        _iter = _each_of.begin();
        this->operator<<([&]{return *_iter;});
    }


protected:
    each_of<T> _each_of;
    typename each_of<T>::iterator _iter;

};

template<typename X>
any_of(std::initializer_list<X>) -> any_of<X>;
template<container_type T>
any_of(T) -> any_of<pointer_value<typename T::value_type> >;

}

