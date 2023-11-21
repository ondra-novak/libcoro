#pragma once

#include "future.h"

#include <atomic>

namespace coro {

namespace _details {

template<typename T, bool lazy, typename Alloc>
class shared_future_t {
public:

    using future_type = std::conditional_t<lazy,lazy_future<T>, future<T> >;

    using promise = typename future_type::promise;
    using construct_type = typename future_type::construct_type;
    using ret_type = typename future_type::ret_type;
    using cast_ret_type = typename future_type::cast_ret_type;
    using target_type = typename future_type::target_type;
    using unique_target_type = typename future_type::unique_target_type;


    shared_future_t() = default;
    shared_future_t(Alloc alloc):_alloc(std::move(alloc)) {}
    shared_future_t(const shared_future_t &other):_ptr(other._ptr) {
        if (_ptr) _ptr->add_ref();
    }
    shared_future_t(shared_future_t &&other):_ptr(other._ptr) {
        other._ptr = nullptr;
    }
    shared_future_t &operator=(const shared_future_t &other) {
        if (this != &other) {
            reset();
            _ptr = other._ptr;
            if (_ptr) _ptr->add_ref();
        }
        return *this;
    }
    shared_future_t &operator=(shared_future_t &&other) {
        if (this != &other) {
            reset();
            _ptr = other._ptr;
            other._ptr = nullptr;
        }
        return *this;
    }

    shared_future_t(construct_type v):_ptr(init(_alloc,std::forward<construct_type>(v))) {}
    shared_future_t(construct_type v, Alloc alloc):_alloc(std::move(alloc)),_ptr(init(_alloc, std::forward<construct_type>(v))) {}

    shared_future_t(std::exception_ptr e):_ptr(init(_alloc,std::move(e))) {}
    shared_future_t(std::exception_ptr e, Alloc alloc):_alloc(std::move(alloc)),_ptr(init(_alloc,std::move(e))) {}

    template<invocable_with_result<void, promise> Fn>
    shared_future_t(Fn &&fn):_ptr(init(_alloc,std::forward<Fn>(fn))) {
        _ptr->hold_ref();
    }
    template<invocable_with_result<void, promise> Fn>
    shared_future_t(Fn &&fn, Alloc alloc):_alloc(std::move(alloc)), _ptr(init(_alloc,std::forward<Fn>(fn))) {
        _ptr->hold_ref();
    }

    template<invocable_with_result<future_type> Fn>
    shared_future_t(Fn &&fn):_ptr(init(_alloc,std::forward<Fn>(fn))) {
        _ptr->hold_ref();
    }
    template<invocable_with_result<future_type> Fn>
    shared_future_t(Fn &&fn, Alloc alloc):_alloc(std::move(alloc)),_ptr(init(_alloc,std::forward<Fn>(fn))) {
        _ptr->hold_ref();
    }

    template<typename X, typename Y>
    shared_future_t(async<X, Y> coro):_ptr(init(_alloc, [&]()->future_type {return coro;})) {
        _ptr->hold_ref();
    }
    template<typename X, typename Y>
    shared_future_t(async<X, Y> coro, Alloc alloc)
        :_alloc(std::move(alloc))
        ,_ptr(init(_alloc, [&]()->future_type {return coro;})) {
        _ptr->hold_ref();
    }


    ~shared_future_t() {
        reset();
    }

    void reset(){
        auto x = std::exchange(_ptr, nullptr);
        if (x) {
            if (x->release_ref()) {
                delete x;
            }
        }

    }

    bool is_pending() const {
        return _ptr && _ptr->is_pending();
    }

    promise get_promise() {
        reset();
        _ptr = init(_alloc);
        auto x = _ptr->get_promise();
        _ptr->hold_ref();
        return x;
    }

    ///synchronous wait
    /**
     * @note to perform asynchonous wait, use co_await on has_value()
     */
    void wait() noexcept {
        _ptr->wait();
    }

    ret_type get() {
        return _ptr->get();
    }

    operator cast_ret_type() {
        return (*_ptr);
    }

    bool register_target_async(target_type &t) {
        return _ptr->register_target_async(t);
    }
    bool register_target(target_type &t) {
        return _ptr->register_target(t);
    }
    bool register_target_async(unique_target_type t) {
        return _ptr->register_target_async(std::move(t));
    }
    bool register_target(unique_target_type t) {
        return _ptr->register_target(std::move(t));
    }

    template<typename Fn>
    bool operator >> (Fn &&fn) {
        return _ptr->operator >> (std::forward<Fn>(fn));
    }

    template<invocable_with_result<future<T> > Fn>
    void operator << (Fn &&fn) {
        reset();
        _ptr = init(std::forward<Fn>(fn));
    }

    std::exception_ptr get_exception_ptr() const noexcept {
        return _ptr->get_exception_ptr();
    }

    auto has_value() {return _ptr->has_value();}

    auto operator!() const {return _ptr->operator!();}

    auto operator co_await() {
        return _ptr->operator co_await();
    }

    template<typename Fn>
    auto visit(Fn fn) {
        return _ptr->visiit(std::forward<Fn>(fn));
    }

    template<std::convertible_to<T> X>
    auto forward(typename future<X>::promise &&p) noexcept {
        return _ptr->forward(std::move(p));
    }

    using promise_type = typename async<T>::promise_type;

protected:

    class ref_cnt_future: public future_type {
    public:

        void add_ref() {
            _counter.fetch_add(1, std::memory_order_relaxed);
        }

        bool release_ref() {
            return _counter.fetch_sub(1, std::memory_order_release) <= 1;
        }

        using future_type::future_type;


        void hold_ref() {
            if (!lazy) {
                ++_counter;
                target_simple_activation(_release_ref_target, [&](auto){
                    if (release_ref()) delete this;
                });
                this->register_target(_release_ref_target);
            }
        }

        struct AllocInfo { // @suppress("Miss copy constructor or assignment operator")
            Alloc *allocator;
            std::size_t size = 0;
        };

        void *operator new(std::size_t sz, AllocInfo &nfo) {
            nfo.size = sz;
            return nfo.allocator->allocate(sz);
        }
        void operator delete(void *ptr, AllocInfo &nfo) {
            Alloc::deallocate(ptr, nfo.size);
        }
        void operator delete(void *ptr, std::size_t sz) {
            Alloc::deallocate(ptr, sz);
        }



    protected:
        std::atomic<unsigned int> _counter = {1};
        typename future_type::target_type _release_ref_target;

    };


    [[no_unique_address]] Alloc _alloc;
    ref_cnt_future *_ptr = nullptr;

    template<typename ... Args>
    static ref_cnt_future *init(std::add_lvalue_reference_t<Alloc> alloc, Args && ... args) {
        typename ref_cnt_future::AllocInfo nfo {&alloc};
        return new(nfo) ref_cnt_future(std::forward<Args>(args)...);
    }

};

}


template<typename T, coro_allocator Alloc = coro::standard_allocator> using shared_future = _details::shared_future_t<T, false, Alloc>;
template<typename T, coro_allocator Alloc = coro::standard_allocator> using shared_lazy_future = _details::shared_future_t<T, true, Alloc>;



}
