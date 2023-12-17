#pragma once

#include <system_error>
#include <bitset>
#include <mutex>
#include <thread>
#include <csignal>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>

namespace coro {

///Enables to coroutine to co_await on specific linux signal(s)
/**
 * This object is global, you can use it through static (global) functions. The implementation
 * uses a worker thread in which context all awaiting coroutines are resumed - so coroutines
 * are never resumed in context of signal handler.
 */
class signal_control {
public:

    ///Identification of awaiting coroutine. This identification is optional.
    using Ident = const void *;

    ///Listen for signal(s)
    /**
     * @param signals list of signals to listen. This is always one-shot operation. Once the
     * signal is captured, other signals are removed from listening.
     * @param ident optional identification, which can be used to drop operation.
     * @return waitable future, it carries number of the signal triggering the wakeup
     * @exception broken_promise_exception listen operation has been dropped (see drop() )
     *
     * @code
     *  int signum = co_await signal_control::listen({SIGINT, SIGTERM});
     * @endcode
     *
     * @note if your coroutine re-registers itself immediatelly after it is resumed, it
     * shouldn't miss any other pending signal.
     *
     */
    static future<int> listen(std::initializer_list<int> signals, Ident ident = {}) {
        return instance().do_listen(signals, ident);
    }

    ///cancel listen operation
    /**
     * @param ident identification of listen operation.
     * @return returns pending_notify of associated future. You can check this value for
     * boolean to find out, whether there was listening coroutine under given ident. By
     * destroying the returned value actual resumption is performed, so you can schedule
     * optimal resption of dropped coroutine. By discarding this value causes resumption
     * immediately after return.
     *
     * Dropped listen operation is returned as broken_promise_exception
     */
    static future<int>::pending_notify drop(Ident ident) {
        return instance().do_drop(ident);
    }

    ///drop all listening coroutines
    static void drop_all() {
        instance().do_drop_all();
    }



protected:

    using SignalMask = std::bitset<NSIG>;

    static signal_control &instance() {
        static signal_control inst;
        return inst;
    }



    future<int> do_listen(std::initializer_list<int> signals, Ident ident = {}) {
        return [&](auto promise) {
            std::lock_guard _(_mx);
            _regs.push_back(ListReg(signals, std::move(promise), std::move(ident)));
            install_signals(signals);
        };

    }
    future<int>::pending_notify do_drop(Ident ident) {
        std::lock_guard _(_mx);
        static constexpr unsigned int nfound = -1;
        unsigned int found = nfound;
        SignalMask s = {};
        unsigned int cnt = _regs.size();
        for (unsigned int i = 0; i < cnt; ++i) {
            if (_regs[i]._ident == ident && found == nfound) found = i;
            else s |= _regs[i]._mask;
        }
        if (found == nfound) return {};
        promise<int> prom = std::move(_regs[found]._prom);
        if (found != cnt - 1) {
            std::swap(_regs[found], _regs[cnt - 1]);
        }
        _regs.pop_back();
        uninstall_signals(s);
        return prom.drop();
    }
    void do_drop_all() {
        std::vector<future<int>::pending_notify> ntf;
        std::lock_guard _(_mx);
        SignalMask s = {};
        ntf.reserve(_regs.size());
        for (auto &r:_regs) ntf.push_back(r._prom.drop());
        _regs.clear();
        uninstall_signals(s);
    }


    signal_control() {
        if (::pipe2(_pfds, O_CLOEXEC)) {
            throw std::system_error(errno, std::system_category());
        }
        _wrk = std::thread([&]{worker();});

    }

    ~signal_control() {
        stop_worker();
        ::close(_pfds[0]);
        ::close(_pfds[1]);
    }


    struct ListReg {
        promise<int> _prom;
        Ident _ident;
        SignalMask _mask;

        ListReg(const std::initializer_list<int> &signals, promise<int> &&prom, Ident &&ident)
        :_prom(std::move(prom)), _ident(std::move(ident))
        {
            for (unsigned int x : signals) if (x < NSIG) _mask.set(x);
        }

    };

    static constexpr int write_end = 1;
    static constexpr int read_end = 0;
    int _pfds[2];
    int _last_write_error = 0;
    SignalMask _installed ={};
    std::vector<ListReg> _regs;
    std::thread _wrk;
    std::mutex _mx;

    void send_signal(int signum) {
        _last_write_error = ::write(_pfds[write_end], &signum, sizeof(signum));
    }

    static void handler(int signum) {
        signal_control &inst = instance();
        inst.send_signal(signum);

    }
    void stop_worker() {
        send_signal(0);
        _wrk.join();
    }

    void worker() {
        std::vector<future<int>::pending_notify> ntf;
        try {
            while (true) {
                int sig = 0;
                int r = ::read(_pfds[read_end], &sig, sizeof(sig));
                if (r<0) throw std::system_error(errno, std::system_category());
                if (!sig) {
                    do_drop_all();
                    break;
                }
                if (sig < NSIG) {
                    bool hit = false;
                    std::lock_guard _(_mx);
                    for (unsigned int i = 0, cnt = _regs.size(); i < cnt;) {
                        if (_regs[i]._mask.test(sig)) {
                            unsigned int end = cnt-1;
                            ntf.push_back(_regs[i]._prom(sig));
                            if (i  < end) {
                                std::swap(_regs[i], _regs[end]);
                            }
                            _regs.pop_back();
                            cnt--;
                            hit = true;
                        } else {
                            ++i;
                        }
                    }
                    if (!hit) {
                        clear_signal(sig);
                        _installed.reset(sig);
                        raise(sig);
                    }
                }
                ntf.clear();
            }

        } catch(...) {
            do_drop_all();
        }
    }

    static void set_signal(int sig, __sighandler_t hndl) {
        struct sigaction sa = {};
        sa.sa_handler = hndl;
        sa.sa_flags = 0;
        sigemptyset(&sa.sa_mask);

        if (sigaction(sig, &sa, NULL)) {
            throw std::system_error(errno, std::system_category());
        }
    }

    static void clear_signal(int sig) {
        set_signal(sig, SIG_DFL);
    }

    void install_signals(const std::initializer_list<int> &sigs) {
        for (unsigned int a: sigs) {
            if (a < NSIG && !_installed.test(a)) {
                set_signal(a, &handler);
                _installed.set(a);
            }
        }
    }

    void uninstall_signals(const SignalMask &bits) {
        auto diff = _installed & (~bits);
        for (unsigned int i = 0; i < diff.size(); ++i) {
            if (diff.test(i)) clear_signal(i);
        }
        _installed = bits;
    }


};


}
