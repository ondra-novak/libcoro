#pragma once

#include "future.h"
#include "async.h"
#include <deque>

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

template<typename T>
concept is_pointer_like = (requires(T v) {
    {*v};
} && std::is_move_constructible_v<T>);

///determines type of pointer value
template<typename T>
using pointer_value = std::decay_t<decltype(*std::declval<T>())>;


template<typename T>
struct unwrap_pointer_like_def {using type = T;};
template<is_pointer_like T>
struct unwrap_pointer_like_def<T> {using type = pointer_value<T>;};
template<typename T>
using unwrap_pointer_like=typename unwrap_pointer_like_def<T>::type;





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
            if constexpr(is_pointer_like<decltype(*from)>) {
                (*from)->then([this]{finish();});
            } else {
                (*from).then([this]{finish();});
            }
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
template<container_type T>
all_of(T) -> all_of<unwrap_pointer_like<typename T::value_type> >;




///Process all futures in order of completion
/**
 * @code
 * for (auto fut: when_each({f1,f2,f3,f4}) {
 *      auto result = co_await fut;
 *      //use result
 * }
 * @endcode
 *
 * @tparam T type of future object (example when_each<future<int> >)
 */
template<typename T>
class when_each {
public:

    using result_future_type = future<std::add_lvalue_reference_t<typename T::value_type> >;

    using future_ptr = std::add_pointer_t<T>;

    using result_promise_type = typename result_future_type::promise_t;
    using result_notify_type = typename result_promise_type::notify;


    ///iterator for when_each - it always return awaitable reference (future)
    /**
     * The iterator is random access iterator. Underlying container is ordered in order
     * of completion. You can access not yet complete item, you only need to co_await on
     * it. Once the item is complete, you can retrieve its value.
     */
    class iterator {
    public:

        using iterator_category = std::random_access_iterator_tag;
        using value_type = typename result_future_type::value_type;
        using reference = std::add_lvalue_reference<result_future_type>;
        using difference_type = std::ptrdiff_t;


        ///default construct - note such iterator cannot be used for iterating
        iterator() = default;
        ///retrieve item. Note that result is awaitable reference. You need co_await the result;
        result_future_type &operator *() const {
            _tmp._prom = _tmp._fut.get_promise();
            _owner->charge(this);
            return _tmp._fut;
        }

        void operator()(result_promise_type prom) const {
            _tmp._prom = std::move(prom);
            _owner->charge(this);
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
        iterator(when_each *owner, std::size_t index):_owner(owner),_index(index) {}

        struct tmp {
            result_promise_type _prom;
            result_future_type _fut;
            tmp() = default;
            tmp(tmp &&other):_prom(std::move(other._prom)) {}
            tmp &operator=(tmp &&other) {
                _prom = std::move(other._prom);
                return *this;
            }
        };

        when_each *_owner = nullptr;
        std::size_t _index = 0;
        mutable tmp _tmp;
        friend class when_each;

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
    when_each(std::initializer_list<pointer_wrapper<T> > lst)
        :when_each(lst.begin(), lst.end()) {}

    ///construct using a container
    /**
     * @param cont container
     */
    template<container_type Container>
    when_each(Container &cont)
        :when_each(cont.begin(), cont.end()) {}

    ///construct using iterator pair
    /**
     * @param from start range
     * @param to end range
     */
    template<iterator_type Iter>
    when_each(Iter from, Iter to) {
        std::lock_guard _(_mx);
        while (from != to) {
            if constexpr(is_pointer_like<decltype(*from)>) {
                future_ptr ptr = &(*(*from));
                _awaiting.push_back(ptr);
            } else {
                future_ptr ptr = &(*from);
                _awaiting.push_back(ptr);
            }
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

    ~when_each() {
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
        _iters.erase( std::remove_if(_iters.begin(), _iters.end(), [&](const iterator *it){
            if (it->_index == idx) {
                proms += it->_tmp._prom;
                return true;
            }
            return false;
        }),_iters.end());
        if (_cond) _cond->notify_all();;
        if (proms) return ptr->forward_to(proms);
        return {};
    }


    bool charge(const iterator *it) {
        result_notify_type ntf;
        std::lock_guard _(_mx);
        auto idx = it->_index+_sep;
        if (idx < _awaiting.size()) {
            ntf = _awaiting[idx]->forward_to(it->_tmp._prom);
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
when_each(std::initializer_list<X>) -> when_each<X>;
template<container_type T>
when_each(T) -> when_each<unwrap_pointer_like<typename T::value_type> >;


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
class any_of : public when_each<T>::result_future_type {
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
        _iter(this->get_promise());

    }


protected:
    when_each<T> _each_of;
    typename when_each<T>::iterator _iter;

};

template<typename X>
any_of(std::initializer_list<X>) -> any_of<X>;
template<container_type T>
any_of(T) -> any_of<unwrap_pointer_like<typename T::value_type> >;




///helps to create task list: list of started task as list of futures
/**
 * @tparam T can be coro::future<T>, coro::deferred_future<T> or any awaitable
 * compatible with these objects. T can be unmovable, however you need to use
 * special push operations in this case.
 *
 */
template<typename T>
class task_list: public std::deque<T> {
public:

    using std::deque<T>::deque;

    ///Redirect return value to the task list
    /**
     * @param fn a function which returns value to be pushed into the list. The
     * return value is pushed as last item.
     *
     * Its is expected that returned value is not movable nor copyable. It is
     * constructed directly in the list
     */
    template<std::invocable Fn>
    void operator<<(Fn &&fn) {
        static_assert(std::is_same_v<std::invoke_result_t<Fn>, T>, "Return value type mismatch");
        this->emplace_back(construct_using<Fn>(fn));
    }


    ///Redirect return value to the task list
    /**
     * @param fn a function which returns value to be pushed into the list. The
     * return value is pushed as first iten.
     *
     * Its is expected that returned value is not movable nor copyable. It is
     * constructed directly in the list
     */
    template<std::invocable Fn>
    friend void operator>>(Fn &&fn, task_list &lst) {
        static_assert(std::is_same_v<std::invoke_result_t<Fn>, T>, "Return value type mismatch");
        lst.emplace_front(construct_using<Fn &>(fn));
    }

    using std::deque<T>::push_back;
    using std::deque<T>::push_front;

    ///Start async function and store result to the list (as last item)
    template<typename X, typename Y> requires std::constructible_from<T, async<X,Y> >
    void push_back(async<X,Y> x) {
        (*this) << [&]()->T{return x;};
    }
    ///Start async function and store result to the list (as first item)
    template<typename X, typename Y> requires std::constructible_from<T, async<X,Y> >
    void push_front(async<X,Y> x) {
        [&]()->T{return x;} >> (*this);
    }

};


}




