#pragma once

#include "frame.h"
#include "function.h"
#include "future.h"
#include <ucontext.h>
#include <memory>

namespace coro {

class fiber: public frame<fiber> {
public:
    void resume() {
        _global._caller->resume_frame(this);
    }
    void destroy() {
        _except = std::make_exception_ptr(_destroy_flag);
        _global._caller->resume_frame(this);
    }

    template<typename Awaitable>
    static decltype(auto) await(Awaitable &awt) {
        if (!awt.await_ready()) {
            _global._caller->suspend_on(awt);
        }
        return awt.await_resume();
    }

    template<typename Awaitable>
    static decltype(auto) await(Awaitable &&awt) {
        return await(awt);
    }


    template<typename Fn, typename ... Args>
    static auto start(std::size_t stack_size, Fn &&fn, Args &&... args) -> deferred_future<std::invoke_result_t<Fn, Args...> >{
        auto me = create_frame(stack_size, std::forward<Fn>(fn), std::forward<Args>(args)...);
        return [me = std::unique_ptr<fiber>(me)](auto promise) mutable -> prepared_coro {
            me->_fut = promise.release();
            return me.release()->get_handle();
        };
    }
    template<typename Fn, typename ... Args>
    static void detach(std::size_t stack_size, Fn &&fn, Args &&... args) {
        auto me = create_frame(stack_size, std::forward<Fn>(fn), std::forward<Args>(args)...);
        _global._caller->resume_frame(me);
    }
protected:

    enum DestroyFlag {_destroy_flag};


    using StartFN = function<void(void *)>;
    using AwtFN = function<std::coroutine_handle<>(std::coroutine_handle<>)>;

    fiber() = default;

    ///this coroutine machine state content
    ucontext_t _this_context = {};
    ///starting function (contains arguments)
    StartFN _start_fn = {};
    ///awaiter function, handles await_suspend processing on suspend
    AwtFN _awt_fn = {};
    ///ponter to associated future
    void *_fut = nullptr;
    ///contains pointer na fiber, which resumes this fiber
    fiber *_caller = nullptr;
    ///contains any exception stored during processing, which must be rethrown on resume
    std::exception_ptr _except = {};
    ///contains true, if resume_frame is called recurively, prevents creating new stack frames
    bool _lock_awt = false;
    ///contains coroutine handle where transfer execution after child coroutine is suspended
    prepared_coro _transfer;

    void dealloc_frame() {
        this->~fiber();
        operator delete(this);
    }

    template<typename Fn, typename ... Args>
    static fiber * create_frame(std::size_t stack_size, Fn &&fn, Args &&... args) {

        if (_global._caller == nullptr) {
            _global._caller = &_global;
        }

        using Ret = std::invoke_result_t<Fn, Args...>;

        using FnClosure = decltype([fn = std::move(fn), args = std::make_tuple(std::forward<Args>(args)...)](void *){});

        std::size_t need_size = stack_size + sizeof(fiber) +  sizeof(FnClosure);
        void *mem = ::operator new(need_size);
        void *args_begin = reinterpret_cast<char *>(mem) + sizeof(fiber);
        void *stack_begin = reinterpret_cast<char *>(args_begin) + sizeof(FnClosure);
        fiber *me = new(mem) fiber;

        getcontext(&me->_this_context);
        me->_this_context.uc_stack.ss_sp = stack_begin;
        me->_this_context.uc_stack.ss_size = stack_size;
        me->_this_context.uc_link = nullptr;
        using TGFN = void (*)(void);
        makecontext(&me->_this_context, reinterpret_cast<TGFN>(&entry_point), 1, me);

        me->_start_fn = StartFN(args_begin, [fn = std::move(fn), args = std::make_tuple(std::forward<Args>(args)...)](void *fut) mutable {
            if (fut) {
                promise<Ret> prom(reinterpret_cast<future<Ret> *>(fut));
                typename promise<Ret>::notify ntf;
                try {
                    if constexpr(std::is_void_v<Ret>) {
                        std::apply(fn, args);
                        ntf = prom();
                    } else {
                        ntf = prom(std::apply(fn, args));
                    }
                } catch (const DestroyFlag &) {
                    ntf = prom.cancel();
                } catch (...) {
                    ntf = prom.reject();
                }
                fiber *me = _global._caller;
                me->_awt_fn = [ntf = std::move(ntf), me](auto) mutable -> std::coroutine_handle<> {
                    auto ntf2 = std::move(ntf);
                    me->dealloc_frame();
                    return ntf2.symmetric_transfer();
                };
            } else {
                try {
                    std::apply(fn, args);
                } catch (...) {
                    /* no exception processing possible */
                }
            }
        });
        return me;
    }

    void resume_frame(fiber *frm) {
        if (frm->_caller) return;
        frm->_caller = this;
        swapcontext(&_this_context, &frm->_this_context);
        _global._caller = this;
        frm->_caller = nullptr;
        _transfer = frm->_awt_fn(frm->get_handle());
        if (!_lock_awt) {
            _lock_awt = true;
            while (_transfer) {
                _transfer();
            }
            _lock_awt = false;
        }
    }

    template<typename Awt>
    void suspend_on(Awt &awt) {
        _awt_fn = [this, &awt](std::coroutine_handle<> h) {
            try {
                using RetVal = decltype(awt.await_suspend(h));
                if constexpr(std::is_same_v<RetVal,bool>) {
                    bool b = awt.await_suspend(h);
                    return b?std::noop_coroutine():h;
                } else if constexpr(std::is_convertible_v<RetVal, std::coroutine_handle<> >){
                    return static_cast<std::coroutine_handle<> >(awt.await_suspend(h));
                } else {
                    awt.await_suspend(h);
                    return std::noop_coroutine();
                }
            } catch (...) {
                this->_except = std::current_exception();
            }
        };
        swapcontext(&_this_context, &_caller->_this_context);
        _global._caller = this;
        if (_except) std::rethrow_exception(_except);
    }

    static thread_local fiber _global;




    static void entry_point(fiber *frame) noexcept {
        _global._caller = frame;
        frame->_start_fn(frame->_fut);  //_start_fn is responsible to define cleanup awaiter
        swapcontext(&frame->_this_context, &frame->_caller->_this_context);
    }
};

inline thread_local fiber fiber::_global;

}
