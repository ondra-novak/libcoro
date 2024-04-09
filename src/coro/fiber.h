#pragma once

#include "sync_await.h"
#include "function.h"
#include "future.h"
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <ucontext.h>
#endif
#include <memory>
#include <exception>

namespace coro {

class fiber: public frame<fiber> {
public:

    template<typename Awaitable>
    static decltype(auto) await(Awaitable &awt) {
        if constexpr(has_co_await_operator<Awaitable>) {
            return await(awt.operator co_await());
        } else {
            fiber *c = _global._caller;
            if (c == nullptr || c == &_global) {
                return sync_await(awt);
            } else if (!awt.await_ready()) {
                c->suspend_on(awt);
            }
            return awt.await_resume();
        }
    }

    template<typename Awaitable>
    static decltype(auto) await(Awaitable &&awt) {
        return await(awt);
    }


    template<typename Fn, typename ... Args>
    static auto create(std::size_t stack_size, Fn &&fn, Args &&... args) -> deferred_future<std::invoke_result_t<Fn, Args...> >{
        auto me = create_frame(stack_size, std::forward<Fn>(fn), std::forward<Args>(args)...);
        return [me = std::unique_ptr<fiber, Deleter>(me)](auto promise) mutable -> prepared_coro {
            me->_fut = promise.release();
            return me.release()->get_handle();
        };
    }
    template<typename Fn, typename ... Args>
    static void create_detach(std::size_t stack_size, Fn &&fn, Args &&... args) {
        auto me = create_frame(stack_size, std::forward<Fn>(fn), std::forward<Args>(args)...);
        _global._caller->resume_frame(me);
    }
protected:

    enum DestroyFlag {_destroy_flag};


    using StartFN = function<void(void *)>;
    using AwtFN = function<std::coroutine_handle<>(std::coroutine_handle<>)>;

    fiber() = default;

    ///this coroutine machine state content
#ifdef _WIN32
    LPVOID _handle = nullptr;
#else    
    ucontext_t _this_context = {};
#endif
    ///starting function (contains arguments)
    StartFN _start_fn = {};
    ///awaiter function, handles await_suspend processing on suspend
    AwtFN _awt_fn = {};
    ///ponter to associated future
    void *_fut = nullptr;
    ///contains pointer na fiber, which resumes this fiber
    fiber *_caller = nullptr;
    ///contains any exception stored during processing, which must be rethrown on resume
    std::exception_ptr _excp = {};
    ///contains true, if resume_frame is called recurively, prevents creating new stack frames
    bool _lock_awt = false;
    ///contains coroutine handle where transfer execution after child coroutine is suspended
    prepared_coro _transfer;


    void dealloc_frame() {
#ifdef _WIN32
        LPVOID h = _handle;
        this->~fiber();
        DeleteFiber(h) ;
#else
        this->~fiber();
        operator delete(this);
#endif
    }

    friend class frame<fiber>;

    void resume() {
        _global._caller->resume_frame(this);
    }
    void destroy() {
        _excp = std::make_exception_ptr(_destroy_flag);
        _global._caller->resume_frame(this);
    }


    struct Deleter {
        void operator()(fiber *f) {
            f->dealloc_frame();
        }
    };

    #ifdef _WIN32
    
    struct InitInfo {
        LPVOID this_fiber = nullptr;            
        fiber *instance = nullptr;
        void *fn_memory = nullptr;
    };

    template<typename FnClosure>
    static void WINAPI InitFunction(LPVOID ptr) {
            InitInfo *nfo = reinterpret_cast<InitInfo *>(ptr);
            fiber inst;
            nfo->fn_memory = alloca(sizeof(FnClosure));
            nfo->instance = &inst;
            SwitchToFiber(nfo->this_fiber);
            entry_point(&inst);            
    }

    #endif

    template<typename Fn, typename ... Args>
    class StartupFunction {
    public:
        StartupFunction(Fn &&fn, Args && ... args):_fn(std::forward<Fn>(fn)),_args(std::forward<Args>(args)...) {}
        void operator()(void *fut) noexcept {

            using Ret = std::invoke_result_t<Fn, Args...>;
            typename promise<Ret>::notify ntf = {};

            if (fut) {
                promise<Ret> prom(reinterpret_cast<future<Ret> *>(fut));
                try {
                    if constexpr(std::is_void_v<Ret>) {
                        std::apply(_fn, _args);
                        ntf = prom();
                    } else {
                        ntf = prom(std::apply(_fn, _args));
                    }
                } catch (const DestroyFlag &) {
                    ntf = prom.cancel();
                } catch (...) {
                    ntf = prom.reject();
                }
            } else {
                try {
                    std::apply(_fn, _args);
                } catch (...) {
                    /* no exception processing possible */
                }

            }        

            //cleanup
            fiber *me = _global._caller;
            //define final awaiter
            me->_awt_fn = [ntf = std::move(ntf), me](auto) mutable -> std::coroutine_handle<> {                
                auto ntf2 = std::move(ntf);
                //destroy frame
                me->dealloc_frame();
                //transfer to other coroutine
                return ntf2.symmetric_transfer();
            };
        }

    protected:
        Fn _fn;
        std::tuple<Args...> _args;
        
    };

    template<typename Fn, typename ... Args>
    static fiber * create_frame(std::size_t stack_size, Fn &&fn, Args &&... args) {


        init_fibers();

        std::size_t need_size = stack_size + sizeof(fiber) +  sizeof(StartupFunction<Fn,Args...>);
        #ifdef _WIN32

        InitInfo nfo = {GetCurrentFiber(),nullptr};
        LPVOID handle = CreateFiber(need_size,&InitFunction<StartupFunction<Fn,Args...>>, &nfo);
        SwitchToFiber(handle);

        fiber *me = nfo.instance;
        void *args_begin = nfo.fn_memory;
        me->_handle = handle;

        #else

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
        
        #endif

        me->_start_fn = StartFN(args_begin, StartupFunction<Fn,Args...>(std::forward<Fn>(fn), std::forward<Args>(args)...));
        return me;
    }

    static void init_fibers() {
            if (_global._caller == nullptr) {
            _global._caller = &_global;
#ifdef _WIN32
            _global._handle = ConvertThreadToFiber(nullptr);
#endif
        }
    }

    void resume_frame(fiber *frm) {
        init_fibers();
        if (frm->_caller) return;
        frm->_caller = this;
#ifdef _WIN32
        SwitchToFiber(frm->_handle);
#else
        swapcontext(&_this_context, &frm->_this_context);
#endif
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
        _awt_fn = [this, &awt](std::coroutine_handle<> h) -> std::coroutine_handle<> {
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
                this->_excp = std::current_exception();
                return std::noop_coroutine();
            }
        };
        #ifdef _WIN32
            SwitchToFiber(_caller->_handle);
        #else
            swapcontext(&_this_context, &_caller->_this_context);
        #endif
        _global._caller = this;
        if (_excp) std::rethrow_exception(_excp);
    }

    static thread_local fiber _global;




    static void entry_point(fiber *frame) noexcept {
        _global._caller = frame;
        frame->_start_fn(frame->_fut);  //_start_fn is responsible to define cleanup awaiter
        #ifdef _WIN32
            SwitchToFiber(frame->_caller->_handle);
        #else
            swapcontext(&frame->_this_context, &frame->_caller->_this_context);
        #endif
    }
};

inline thread_local fiber fiber::_global;

}
