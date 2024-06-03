#pragma once


#include "common.h"
#include <mutex>
#ifdef LIBCORO_ENABLE_TRACE
#include <fstream>
#include <filesystem>
#include <atomic>
#include <typeinfo>
#include <thread>

namespace coro {



class trace {
public:


    static std::string get_file_name() {
        std::filesystem::path exe_path = std::filesystem::read_symlink("/proc/self/exe");
        std::string exe_name = exe_path.stem().string();
        std::string trace_filename = exe_name + ".corotrace";
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
        user_report = 'U',
        thread = 'T',
        hr = 'H',
        coroutine_type = 't',
        link = 'l',
        proxy = 'p',


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
        bool is_new = true;
        thread_state():id(++counter) {}
    };

    std::ostream &header(record_type rt) {
        auto &f = stream();
        if (_state.is_new) {
            f << _state.id << separator << static_cast<char>(record_type::thread) << separator
                        << gettid() << std::endl;
            _state.is_new = false;
        }
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

    void set_coroutine_type(const void *ptr, const char *type) {
        std::lock_guard _(_mx);
        header(record_type::coroutine_type) << ptr << separator << type << std::endl;
    }

    void on_link(const void *from, const void *to) {
        std::lock_guard _(_mx);
        header(record_type::link) << from << separator << to<< std::endl;
    }
    void set_proxy(void *ptr, std::size_t sz, bool destroyed) {
        std::lock_guard _(_mx);
        header(record_type::link) << ptr << separator << sz<< separator << destroyed <<std::endl;
    }

    template<typename ... Args>
    void set_name(const void *ptr, const char *src, const char *fn, unsigned int line, const std::tuple<Args...> &args) {
        std::lock_guard _(_mx);
        auto &f = header(record_type::name);
        f << ptr << separator << src << ":" << line << separator << fn << separator;
        std::apply([&](const auto & ... args){
            ((f << args),...);
        }, args);
        f << std::endl;
    }

    void hline(std::string_view text) {
        std::lock_guard _(_mx);
        header(record_type::hr) << text << std::endl;
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

template<typename ... Args>
struct trace_name: std::suspend_always {
    const char *src_name;
    const char *fn_name;
    unsigned int line;
    std::tuple<std::conditional_t<std::is_array_v<Args>,
        std::add_pointer_t<std::add_const_t<std::remove_extent_t<Args> > >,
        std::add_lvalue_reference_t<std::add_const_t<Args> > >...> args;
    trace_name(const char *src, const char *fn, unsigned int line, const Args &...args):src_name(src),fn_name(fn),line(line),args(args...) {}
    bool await_suspend(std::coroutine_handle<> h) {
        trace::_instance.set_name(h.address(), src_name, fn_name,line,args);
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
#define LIBCORO_TRACE_SET_NAME(...) (co_await ::coro::trace_name(__FILE__, __FUNCTION__,__LINE__ __VA_OPT__(,) __VA_ARGS__))
#define LIBCORO_TRACE_SET_CORO_TYPE(h,name) ::coro::trace::_instance.set_coroutine_type(h.address(), name);
#define LIBCORO_TRACE_LOG(...) ::coro::trace::_instance.user_report(__VA_ARGS__)
#define LIBCORO_TRACE_AWAIT_ON(h, awaiter, type) ::coro::trace::_instance.on_await_on(h.address(),awaiter,type)
#define LIBCORO_TRACE_LINK(from_ptr, to_ptr) ::coro::trace::_instance.on_link(from_ptr, to_ptr);
#define LIBCORO_TRACE_SEPARATOR(text) ::coro::trace::_instance.hline(text)

struct suspend_always : public std::suspend_always{
    static void await_suspend(std::coroutine_handle<> h) noexcept  {
        LIBCORO_TRACE_ON_SWITCH(h, std::coroutine_handle<>());
    }
};

}
#else


///Record creation of an coroutine
/**
 * @param ptr pointer to coroutine (handle.address())
 * @param size size in bytes
 *
 * @note requires LIBCORO_ENABLE_TRACE
 *
 * @ingroup trace
 */
#define LIBCORO_TRACE_ON_CREATE(ptr,size)
///Record destruction of an coroutine
/**
 * @param ptr pointer to coroutine (handle.address())
 *
 * @note requires LIBCORO_ENABLE_TRACE
 *
 * @ingroup trace
 */
#define LIBCORO_TRACE_ON_DESTROY(ptr)
///Record resumption of an coroutine
/**
 * Use this macro before h.resume() is called
 * @param ptr pointer to coroutine (handle.address())
 *
 * @note requires LIBCORO_ENABLE_TRACE
 *
 * @ingroup trace
 */
#define LIBCORO_TRACE_ON_RESUME(h)
///Record switch (symmetric transfer) from one coroutine to other
/**
 * Use this macro before returning from await_suspend.
 * @param from coroutine being suspended
 * @param to coroutine being resumed (nullptr for none)
 *
 * @note requires LIBCORO_ENABLE_TRACE
 *
 * @ingroup trace
 */

#define LIBCORO_TRACE_ON_SWITCH(from, to)
/// Macro declares await_transform function to capture all co_await events
/**
 * Declare inside promise body
 * @note requires LIBCORO_ENABLE_TRACE
 *
 * @ingroup trace
 */
#define LIBCORO_TRACE_AWAIT


///Record co_yield
/**
 * Use macro inside of yield_value() function
 *
 * @param h handle of coroutine
 * @param arg argument to yield
 *
 * @note requires LIBCORO_ENABLE_TRACE
 *
 * @ingroup trace
 */
#define LIBCORO_TRACE_YIELD(h, arg)

///Record custom user value
/**
 * This macro can be used anywhere to put a custom data to trace log
 *
 * @param ... any count of arguments to report into the trace log. All argumnets
 * must support stream operator <<;
 *
 * @note requires LIBCORO_ENABLE_TRACE
 *
 * @ingroup trace
 */
#define LIBCORO_TRACE_LOG(...)

///Set coroutine name
/**
 * Use macro at the beginning of coroutine to specify name and location of the coroutine
 *
 * @param ... any count of arguments to report into the trace log. All argumnets
 * must support stream operator <<. Useful to report coroutine's arguments
 *
 * @note requires LIBCORO_ENABLE_TRACE
 *
 * @ingroup trace
 */
#define LIBCORO_TRACE_SET_NAME(...)
///Record co_await operation (manually)
/**
 * @param h handle
 * @param awaiter pointer to an awaiter
 * @param type string type of awaiter (typeid(awaiter).name())
 *
 * @note requires LIBCORO_ENABLE_TRACE
 *
 * @ingroup trace
 */
#define LIBCORO_TRACE_AWAIT_ON(h, awaiter, type)

///Insert separator to trace log with a title
/**
 * @param text title of separator
 *
 * @code
 * LIBCORO_TRACE_SEPARATOR("server start");
 * ...
 * ...
 * LIBCORO_TRACE_SEPARATOR("server exit");
 * @endcode
 *
 * @note requires LIBCORO_ENABLE_TRACE
 *
 * @ingroup trace
 */

#define LIBCORO_TRACE_SEPARATOR(text)
///Sets coroutine type
/**
 * @param h handle
 * @param name typeid(coroutine_type).name()
 *
 * @note requires LIBCORO_ENABLE_TRACE
 *
 * @ingroup trace
 */
#define LIBCORO_TRACE_SET_CORO_TYPE(h,name)


///Report link between coroutines (when one coroutine awaits to other)
/**
 * @param from_ptr pointer to coroutine (must be pointer, not handle)
 * @param to_ptr pointer to object which should be inside of waiting coroutine (awaiter)
 *
 * Not every link can be visualised. The vistrace engine must be able to
 * detect both sides of the link
 *
 * @note requires LIBCORO_ENABLE_TRACE
 *
 * @ingroup trace
 */
#define LIBCORO_TRACE_LINK(from_ptr, to_ptr)

namespace coro {

    ///replaces std::suspend_always for purpose of tracing
    /** This correctly reports suspend_always awaiter
     * use only for purpose of initial or final suspend
     */
    using suspend_always = std::suspend_always;
}


#endif

