#pragma once


#include "common.h"
#include <mutex>
#include <string_view>
#include <source_location>
#ifdef LIBCORO_ENABLE_TRACE
#include <fstream>
#include <filesystem>
#include <atomic>
#include <typeinfo>
#include <thread>
#include <bit>

#ifdef _WIN32
extern "C" {
unsigned long __stdcall GetCurrentThreadId();
unsigned long __stdcall GetModuleFileNameA(void *, char *, unsigned long);
}
#endif

namespace coro {

namespace trace {

enum class record_type: char {
    create = 'c',
    destroy = 'x',
    resume_enter = 'e',
    resume_exit = 'r',
    sym_switch = 's',
    awaits_on = 'a',
    yield = 'y',
    user_report = 'U',
    thread = 'T',
    hr = 'H',
    coroutine_type = 't',
    link = 'l',
    proxy = 'p',
    block = 'b',
    unblock = 'u'
};

inline constexpr char separator = '\t';


class impl {
public:


    static std::string get_file_name() {
        #ifdef _WIN32
            char szFileName[1024];
            GetModuleFileNameA(NULL, szFileName, sizeof(szFileName));
            std::filesystem::path exe_path = szFileName;

        #else
            std::filesystem::path exe_path = std::filesystem::read_symlink("/proc/self/exe");
        #endif
            std::string exe_name = exe_path.stem().string();
            std::string trace_filename = exe_name + ".corotrace";
            return trace_filename;
    }




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
#ifdef _WIN32
            auto tid = GetCurrentThreadId();
#else
            auto tid = gettid();
#endif
            f << _state.id << separator << static_cast<char>(record_type::thread) << separator
                        << tid << std::endl;
            _state.is_new = false;
        }
        f  << _state.id << separator << static_cast<char>(rt) << separator;
        return f;

    }

    struct pointer: std::string_view {
        static constexpr std::string_view letters = "0123456789ABCDEF";
        pointer(const void *p): std::string_view(_txt, sizeof(_txt)) {
            std::uintptr_t val = std::bit_cast<std::uintptr_t>(p);

            for (unsigned int i = 0; i < sizeof(_txt);++i) {
                _txt[sizeof(_txt)-i-1] = letters[val & 0xF];
                val >>= 4;
            }
        }

        char _txt[sizeof(std::uintptr_t)*2];

    };

    void on_create(const void *ptr, std::size_t size) {
        std::lock_guard _(_mx);
        header(record_type::create) << pointer(ptr) << separator << size << std::endl;
        _foutput.flush();
    }
    void on_destroy(const void *ptr) {
        std::lock_guard _(_mx);
        header(record_type::destroy) << pointer(ptr) << std::endl;
    }
    void on_resume_enter(const void *ptr) {
        std::lock_guard _(_mx);
        header(record_type::resume_enter) << pointer(ptr) << std::endl;
    }
    void on_resume_exit() {
        std::lock_guard _(_mx);
        header(record_type::resume_exit) << std::endl;
    }
    void on_switch(const void *from, const void *to, const std::source_location *loc) {
        std::lock_guard _(_mx);
        if (to == std::noop_coroutine().address()) to = nullptr;
        auto &f = header(record_type::sym_switch);
        f << pointer(from) << separator << pointer(to);
        if (loc) f << separator << loc->file_name() << separator << loc->line() << separator << loc->function_name();
        f << std::endl;
    }
    void on_await_on(const void *coro, const void *on, const char *awt_name) {
        std::lock_guard _(_mx);
        header(record_type::awaits_on) << pointer(coro) << separator << awt_name << separator << pointer(on) << std::endl;
    }
    template<typename Arg>
    void on_yield(const void *coro, Arg &) {
        std::lock_guard _(_mx);
        header(record_type::yield) << pointer(coro) << separator << typeid(Arg).name() << std::endl;
    }

    void set_coroutine_type(const void *ptr, const char *type) {
        std::lock_guard _(_mx);
        header(record_type::coroutine_type) << pointer(ptr) << separator << type << std::endl;
    }

    void on_link(const void *from, const void *to, std::size_t object_size) {
        std::lock_guard _(_mx);
        header(record_type::link) << pointer(from) << separator << pointer(to) << separator << object_size << std::endl;
    }

    void hline(std::string_view text) {
        std::lock_guard _(_mx);
        header(record_type::hr) << text << std::endl;
    }

    void on_block(void *ptr, std::size_t sz) {
        std::lock_guard _(_mx);
        header(record_type::block) << pointer(ptr) << separator << sz << std::endl;
    }

    void on_unblock(void *ptr, std::size_t sz) {
        std::lock_guard _(_mx);
        header(record_type::unblock) << pointer(ptr) << separator << sz << std::endl;
    }


    template<typename ... Args>
    void user_report(Args && ... args) {
        std::lock_guard _(_mx);
        auto &f = header(record_type::user_report);
        ((f << args), ...);
        f << std::endl;
    }


    static impl _instance;
    static thread_local thread_state _state;

    ~impl() {
        _mx.lock();
        _mx.unlock();       //Windows complains when race condition
    }

protected:
    std::ofstream _foutput;
    std::mutex _mx;
};

template<typename T>
concept await_suspend_with_location = requires(T awt, std::coroutine_handle<> h, std::source_location loc) {
    {awt.await_suspend(h, loc)};
};


struct ident_awt : std::suspend_always{
    static constexpr std::coroutine_handle<> await_suspend(std::coroutine_handle<> h) noexcept {
        return h;
    }
};

inline constexpr ident_awt ident = {};


class base_promise_type {
public:


    template<typename X>
    class trace_awaiter {
    public:

        template<std::invocable<> Fn>
        trace_awaiter(Fn &&fn):_awt(fn()) {}
        bool await_ready() {return _awt.await_ready();}
        auto await_suspend(std::coroutine_handle<> h, std::source_location loc = std::source_location::current()) {
            using RetVal = decltype(_awt.await_suspend(h));
            if constexpr(!std::is_same_v<std::decay_t<X>, ident_awt>) {
                impl::_instance.on_await_on(h.address(), &_awt, typeid(X).name());
            }
            if constexpr(std::is_convertible_v<RetVal, std::coroutine_handle<> >) {
                std::coroutine_handle<> r = proxy_await_suspend(_awt,h, loc);
                impl::_instance.on_switch(h.address(), r.address(), &loc);
                return r;
            } else if constexpr(std::is_convertible_v<RetVal, bool>) {
                bool b = proxy_await_suspend(_awt,h,loc);
                if (b) impl::_instance.on_switch(h.address(), nullptr, &loc);
                return b;
            } else {
                impl::_instance.on_switch(h.address(), nullptr, &loc);
                return proxy_await_suspend(_awt,h,loc);
            }
        }
        decltype(auto) await_resume() {
            return _awt.await_resume();
        }



    protected:
        X _awt;

        static auto proxy_await_suspend(X &awt, std::coroutine_handle<> h, std::source_location loc) {
            if constexpr(await_suspend_with_location<X>) {
                return awt.await_suspend(h, loc);
            } else {
                return awt.await_suspend(h);
            }
        }
    };

    template<typename X>
    inline auto await_transform(X &&awt) {
        if constexpr(indirectly_awaitable<X>) {
            using T = decltype(awt.operator co_await());
            return trace_awaiter<T>([&]{return awt.operator co_await();});
        } else {
            return trace_awaiter<X &>([&]()->X &{return awt;});
        }
    }

};


inline impl impl::_instance;
inline std::atomic<unsigned int> impl::thread_state::counter ={0};
inline thread_local impl::thread_state impl::_state;

inline void on_create(const void *ptr, std::size_t size) {impl::_instance.on_create(ptr, size);}
inline void on_destroy(const void *ptr, std::size_t ) {impl::_instance.on_destroy(ptr);}
inline void resume(std::coroutine_handle<> h) noexcept {
    impl::_instance.on_resume_enter(h.address());
    h.resume();
    impl::_instance.on_resume_exit();
}
inline std::coroutine_handle<> on_switch(std::coroutine_handle<> from, std::coroutine_handle<> to, const std::source_location *loc = nullptr) {
    impl::_instance.on_switch(from.address(), to.address(), loc);
    return to;
}

inline bool on_switch(std::coroutine_handle<> from, bool suspend, const std::source_location *loc = nullptr) {
    if (suspend) impl::_instance.on_switch(from.address(), nullptr, loc);
    return suspend;
}
inline void on_suspend(std::coroutine_handle<> from, const std::source_location *loc = nullptr) {
    impl::_instance.on_switch(from.address(), nullptr,  loc);
}

template<std::invocable<> Fn, typename Ref>
inline void on_block(Fn &&fn, Ref &r) {
    impl::_instance.on_block(&r, sizeof(r));
    std::forward<Fn>(fn)();
    impl::_instance.on_unblock(&r, sizeof(r));
}




template<typename Arg>
inline void on_yield(std::coroutine_handle<> h, const Arg &arg) {
    impl::_instance.on_yield(h.address(), arg);
}

inline void set_class(std::coroutine_handle<> h, const char *class_name) {
    impl::_instance.set_coroutine_type(h.address(), class_name);
}

template<typename ... Args>
inline void log(const Args & ... args) {impl::_instance.user_report(args...);}


inline void awaiting_ref(ident_t source, std::coroutine_handle<> awaiting) {
    impl::_instance.on_link(source.address(), awaiting.address(), 0);
}
inline void awaiting_ref(ident_t source, const void *awaiting_obj) {
    impl::_instance.on_link(source.address(), awaiting_obj, 0);
}
template<typename T>
inline void awaiting_ref(const T &source, std::coroutine_handle<> awaiting) {
    impl::_instance.on_link(&source, awaiting.address(), sizeof(source));
}
template<typename T>
inline void awaiting_ref(const T &source, const void *awaiting_obj) {
    impl::_instance.on_link(&source, awaiting_obj, sizeof(source));
}


inline void section(std::string_view text) {::coro::trace::impl::_instance.hline(text);}

struct suspend_always : public std::suspend_always{
    static void await_suspend(std::coroutine_handle<> h) noexcept  {
        on_suspend(h,  {});
    }
};

}
}
#else

namespace coro {

     namespace trace {
     ///Record creation of an coroutine
     /**
      * @param ptr pointer to coroutine (handle.address())
      * @param size size in bytes
      *
      * @note requires LIBCORO_ENABLE_TRACE
      *
      * @ingroup trace
      */
    inline void on_create(const void *, std::size_t ) {}
    ///Record destruction of an coroutine
    /**
     * @param ptr pointer to coroutine (handle.address())
     *
     * @note requires LIBCORO_ENABLE_TRACE
     *
     * @ingroup trace
     */
    inline void on_destroy(const void *, std::size_t ) {}

    ///Record resumption of an coroutine
    /**
     * You need to call this insteaded h.resume() to record resumption action
     * @param h handle to resume
     *
     * @note requires LIBCORO_ENABLE_TRACE
     *
     * @ingroup trace
     */
    inline void resume(std::coroutine_handle<> h) noexcept {h.resume();}

    ///Record switch (symmetric transfer) from one coroutine to other
    /**
     * @param from coroutine being suspended
     * @param to coroutine being resumed
     *
     * @note requires LIBCORO_ENABLE_TRACE
     *
     * @ingroup trace
     */
    inline std::coroutine_handle<> on_switch(std::coroutine_handle<> , std::coroutine_handle<> to, const void *) {return to;}
    ///Record switch (symmetric transfer) when boolean is returned from await_suspend
    /**
     * @param from coroutine being suspended
     * @param suspend true if coroutine is suspended false if continues
     *
     * @note requires LIBCORO_ENABLE_TRACE
     *
     * @ingroup trace
     */
    inline bool on_switch(std::coroutine_handle<> , bool suspend, const void *) {return suspend;}

    ///Record suspend for coroutine
    inline void on_suspend(std::coroutine_handle<> , const void *) {}


    template<typename Arg>
    inline void on_yield(std::coroutine_handle<> , const Arg &) {}

    inline void set_class(std::coroutine_handle<>, std::string_view ) {}

    template<typename ... Args>
    inline void log(const Args & ... ) {}

    inline void awaiting_ref(ident_t , std::coroutine_handle<> ) {}
    inline void awaiting_ref(ident_t , const void *) {}
    template<typename T>
    inline void awaiting_ref(const T &, std::coroutine_handle<> ) {}
    template<typename T>
    inline void awaiting_ref(const T &, const void *) {}

    inline void section(std::string_view) {}

    ///replaces std::suspend_always for purpose of tracing
    /** This correctly reports suspend_always awaiter
     * use only for purpose of initial or final suspend
     */
    using suspend_always = std::suspend_always;

    class base_promise_type {};

    struct ident_awt : std::suspend_always{
        static constexpr std::coroutine_handle<> await_suspend(std::coroutine_handle<> h) noexcept {
            return h;
        }
    };

    inline constexpr ident_awt ident = {};

    template<std::invocable<> Fn, typename Ref>
    inline void on_block(Fn &&fn, Ref &) {
        std::forward<Fn>(fn)();
    }


}



}


#endif

