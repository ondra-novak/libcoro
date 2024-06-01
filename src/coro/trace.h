#pragma once

#ifdef LIBCORO_ENABLE_TRACE

#include "common.h"

#include <fstream>
#include <filesystem>
#include <mutex>
#include <atomic>
#include <typeinfo>
namespace coro {



class trace {
public:


    static std::string get_file_name() {
        std::filesystem::path exe_path = std::filesystem::read_symlink("/proc/self/exe");
        std::string exe_name = exe_path.stem().string();
        std::string trace_filename = exe_name + ".trace";
        return trace_filename;
    }


    static constexpr char separator = '|';

    enum class record_type: char {
        create = 'c',
        destroy = 'x',
        resume_enter = 'e',
        resume_exit = 'r',
        sym_switch = 's',
        awaits_on = 'a',
        yield = 'y',
        name = 'N',
        user_report = 'U'

    };

    std::ostream &stream() {
        if (_foutput.is_open()) return _foutput;
        std::string trace_file = get_file_name();
        _foutput.open(trace_file, std::ios::trunc);
        if (!_foutput) throw std::runtime_error("Failed to create trace file:" + trace_file);
        return _foutput;
    }


    struct thread_state {
        static std::atomic<unsigned int> counter;
        unsigned int id = 0;
        thread_state():id(++counter) {}
    };

    std::ostream &header(record_type rt) {
        auto &f = stream();
        f  << _state.id << separator << static_cast<char>(rt) << separator;
        return f;

    }

    void on_create(const void *ptr, std::size_t size) {
        std::lock_guard _(_mx);
        header(record_type::create) << ptr << separator << size << std::endl;
        _foutput.flush();
    }
    void on_destroy(const void *ptr) {
        std::lock_guard _(_mx);
        header(record_type::destroy) << ptr << std::endl;
    }
    void on_resume_enter(const void *ptr) {
        std::lock_guard _(_mx);
        header(record_type::resume_enter) << ptr << std::endl;
    }
    void on_resume_exit() {
        std::lock_guard _(_mx);
        header(record_type::resume_exit) << std::endl;
    }
    void on_switch(const void *from, const void *to) {
        std::lock_guard _(_mx);
        if (to == std::noop_coroutine().address()) to = nullptr;
        header(record_type::sym_switch) << from << separator << to << std::endl;
    }
    void on_await_on(const void *coro, const void *on, const char *awt_name) {
        std::lock_guard _(_mx);
        header(record_type::awaits_on) << coro << separator << awt_name << separator << on << std::endl;
    }
    template<typename Arg>
    void on_yield(const void *coro, Arg &) {
        std::lock_guard _(_mx);
        header(record_type::yield) << coro << separator << typeid(Arg).name() << std::endl;
    }

    void set_name(const void *ptr, const char *src, const char *fn) {
        std::lock_guard _(_mx);
        header(record_type::name) << ptr << separator << src << separator << fn << std::endl;
    }

    template<typename ... Args>
    void user_report(Args && ... args) {
        std::lock_guard _(_mx);
        auto &f = header(record_type::user_report);
        ((f << args), ...);
        f << std::endl;
    }

    class on_resume {
    public:
        on_resume (void *ptr) {_instance.on_resume_enter(ptr);}
        ~on_resume () {_instance.on_resume_exit();}
    };


    static trace _instance;
    static thread_local thread_state _state;

protected:
    std::ofstream _foutput;
    std::mutex _mx;
};

template<typename X>
class trace_awaiter {
public:

    template<std::invocable<> Fn>
    trace_awaiter(Fn &&fn):_awt(fn()) {}
    bool await_ready() {return _awt.await_ready();}
    auto await_suspend(std::coroutine_handle<> h) {
        using RetVal = decltype(_awt.await_suspend(h));
        trace::_instance.on_await_on(h.address(), &_awt, typeid(X).name());
        if constexpr(std::is_convertible_v<RetVal, std::coroutine_handle<> >) {
            std::coroutine_handle<> r = _awt.await_suspend(h);
            trace::_instance.on_switch(h.address(), r.address());
            return r;
        } else if constexpr(std::is_convertible_v<RetVal, bool>) {
            bool b = _awt.await_suspend(h);
            if (b) trace::_instance.on_switch(h.address(), nullptr);
            return b;
        } else {
            trace::_instance.on_switch(h.address(), nullptr);
            return _awt.await_suspend(h);
        }
    }
    decltype(auto) await_resume() {
        return _awt.await_resume();
    }


protected:
    X _awt;
};

template<typename X>
inline auto handle_await_transform(X &&awt) {
    if constexpr(indirectly_awaitable<X>) {
        using T = decltype(awt.operator co_await());
        return trace_awaiter<T>([&]{return awt.operator co_await();});
    } else {
        return trace_awaiter<X &>([&]()->X &{return awt;});
    }
}

struct trace_name: std::suspend_always {
    const char *src_name;
    const char *fn_name;
    trace_name(const char *src, const char *fn):src_name(src),fn_name(fn) {}
    bool await_suspend(std::coroutine_handle<> h) {
        trace::_instance.set_name(h.address(), src_name, fn_name);
        return false;
    }
};



inline trace trace::_instance;
inline std::atomic<unsigned int> trace::thread_state::counter ={0};
inline thread_local trace::thread_state trace::_state;

#define LIBCORO_TRACE_ON_CREATE(ptr,size) ::coro::trace::_instance.on_create(ptr,size)
#define LIBCORO_TRACE_ON_DESTROY(ptr) ::coro::trace::_instance.on_destroy(ptr)
#define LIBCORO_TRACE_ON_RESUME(h) ::coro::trace::on_resume __trace__(h.address())
#define LIBCORO_TRACE_ON_SWITCH(from, to) ::coro::trace::_instance.on_switch(from.address(), to.address())
#define LIBCORO_TRACE_AWAIT template<typename X> auto await_transform(X &&awt) {return handle_await_transform(std::forward<X>(awt));}
#define LIBCORO_TRACE_YIELD(h, arg) ::coro::trace::_instance.on_yield(h.address(), arg)
#define LIBCORO_TRACE_SET_NAME() (co_await ::coro::trace_name(__FILE__, __FUNCTION__))
#define LIBCORO_TRACE_LOG(...) ::coro::trace::_instance.user_report(__VA_ARGS__)
#define LIBCORO_TRACE_AWAIT_ON(h, awaiter, type) ::coro::trace::_instance.on_await_on(h.address(),awaiter,type)

struct suspend_always : public std::suspend_always{
    static void await_suspend(std::coroutine_handle<> h) noexcept  {
        LIBCORO_TRACE_ON_SWITCH(h, std::coroutine_handle<>());
    }
};

}
#else
#define LIBCORO_TRACE_ON_CREATE(ptr,size)
#define LIBCORO_TRACE_ON_DESTROY(ptr)
#define LIBCORO_TRACE_ON_RESUME(h)
#define LIBCORO_TRACE_ON_SWITCH(from, to)
#define LIBCORO_TRACE_AWAIT
#define LIBCORO_TRACE_YIELD(h, arg)
#define LIBCORO_TRACE_LOG(...)
#define LIBCORO_TRACE_SET_NAME()
#define LIBCORO_TRACE_AWAIT_ON(h, awaiter, type)

namespace coro {
    using suspend_always = std::suspend_always;
}


#endif

