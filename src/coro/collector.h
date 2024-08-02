#pragma once

#include "future.h"
#include "async.h"


namespace coro {

namespace _details {

    template<typename Collectible>
    class collectible_factory {
    public:
        virtual ~collectible_factory() = default;
        virtual Collectible create() = 0;
    };

    template<typename Collectible, typename Fn>
    class collectible_factory_fn: public collectible_factory<Collectible> {
    public:
        collectible_factory_fn(Fn &&fn):_fn(std::forward<Fn>(fn)) {}
        virtual Collectible create() {return _fn();}
    protected:
        std::decay_t<Fn> _fn;
    };

    template<typename Collectible, typename Fn>
    auto create_collectible_factory(Fn &&fn) {
        return collectible_factory_fn<Collectible, Fn>(std::forward<Fn>(fn));
    }

}

///The collector is a reversed generator. The coroutine consumes values and then returns a result
/**
 * The values are awaited by `co_yield nullptr` (or co_yield <anything>). The value itself
 * is ignored. Return value of co_yield is next collectible item;
 *
 * @tparam Collectible Items to collect
 * @tparam Result Type of return value
 * @tparam Alloc optional allocator
 */
template<typename Collectible, typename Result, coro_allocator Alloc =  std_allocator>
class collector: public future<Result> {
public:

    class promise_type: public _details::coro_promise<Result> {
    public:
        std::suspend_never initial_suspend() const noexcept {return {};}

        promise_type() {
            trace::set_class(std::coroutine_handle<promise_type>::from_promise(*this), typeid(collector).name());
        }

        struct final_awaiter: std::suspend_always {
            std::coroutine_handle<> await_suspend(std::coroutine_handle<promise_type> me) noexcept {
                promise_type &self = me.promise();
                self.set_resolved();
                return trace::on_switch(me,self._waiting(true).symmetric_transfer(),{});
            }
        };

        final_awaiter final_suspend() const noexcept {return {};}
        collector get_return_object() {return {this};}

        struct yield_awaiter {
            promise_type *self;
            static constexpr bool await_ready() noexcept {return false;}
            std::coroutine_handle<> await_suspend(std::coroutine_handle<promise_type> me, std::source_location loc = std::source_location::current()) {
               self = &me.promise();
               return trace::on_switch(me, self->_waiting(false).symmetric_transfer(),&loc);
            }
            Collectible await_resume() {
                return self->_factory->create();
            }
        };

        template<typename X>
        yield_awaiter yield_value([[maybe_unused]] X &&x) {
            trace::on_yield(std::coroutine_handle<const promise_type>::from_promise(*this),x);
            return {};
        }

        prepared_coro connect(collector *col) {
            prepared_coro out;
            if (this->fut != col) {
                if (this->fut) out = this->set_resolved();
                this->fut = col->get_promise().release();
            }
            return out;
        }

        void resume() {
            auto h =std::coroutine_handle<promise_type>::from_promise(*this);
            trace::awaiting_ref(h, _waiting.get_future());
            trace::resume(h);
        }
        void destroy() {
            std::coroutine_handle<promise_type>::from_promise(*this).destroy();
        }
        bool done() const {
            return std::coroutine_handle<const promise_type>::from_promise(*this).done();
        }

        _details::collectible_factory<Collectible> *_factory;
        promise<bool> _waiting;

    };

    ///construct uninitalized collector - you can initialize it later by assignment
    collector() = default;
    ///move
    collector(collector &&other):_prom(std::move(other._prom)) {
        _prom->connect(this);
    }
    ///convert from different allocator
    template<typename A>
    collector(collector<Collectible, Result, A> &&other): _prom(cast_promise(other._prom.release())) {

    }


    ///assign by move
    collector &operator=(collector &&other){
        if (this != &other) {
            std::destroy_at(this);
            std::construct_at(this, std::move(other));
        }
        return *this;
    }

    ///call collected and push next collectible item
    /**
     * @param args arguments to construct the item or function which is called without
     * arguments and returns the collectible item
     * @return future, which is resolved by
     * @retval true result is available, no more items needed
     * @retval false result is not yet available, add more items
     */
    template<typename ... Args>
    future<bool> operator()(Args &&... args) {
        static_assert(future_constructible<Collectible, Args ...>);
        return [&](auto promise) {
            if (_prom->done()) {
                promise(true);
                return;
            }
            _prom->_waiting = std::move(promise);

            if constexpr(sizeof...(Args) == 1 && (invocable_r_exact<Args, Collectible> && ...)) {
                [this](auto &&arg, auto &&...) {
                    this->set_factory_resume([&]()->Collectible{
                        return arg();
                    });
                }();
            } else {
                this->set_factory_resume([&]{
                    return Collectible(std::forward<Args>(args)...);
                });
            }
        };
    }

    operator ident_t() const {return std::coroutine_handle<promise_type>::from_promise(*_prom);}

protected:

    collector(promise_type *p):_prom(p) {
        _prom->connect(this);
    }

    template<typename X>
    static promise_type *cast_promise(X *other) {
        return static_cast<promise_type *>(static_cast<_details::coro_promise_base<Result> *>(other));
    }


    struct Deleter {
        void operator()(promise_type *x) {
            x->destroy();
        }
    };

    template<typename A, typename B, coro_allocator C> friend class collector;

    std::unique_ptr<promise_type,Deleter> _prom;

    template<typename Fn>
    void set_factory_resume(Fn &&fn) {
        _details::collectible_factory_fn<Collectible, Fn> f(std::forward<Fn>(fn));
        _prom->_factory = &f;
        _prom->resume();
    }


};



}
