#pragma once

#include "frame.h"

#include <atomic>
namespace coro {

namespace _details {

class WaitableFrame: public frame<WaitableFrame> {
public:

    void do_wait() {
        return _ready.wait(false);
    }


protected:

    std::atomic<bool> _ready = {false};

    void resume() {
        _ready = true;
        _ready.notify_all();
    }

    void destroy() {

    }

    friend class frame<WaitableFrame>;
};

}

template<typename Awt>
decltype(auto) sync_await(Awt &&x) {
    if constexpr(has_co_await_operator<Awt>) {
        return sync_await(x.operator co_await());
    } else {
        _details::WaitableFrame wt;
        if (wt.try_await(x)) {
            wt.do_wait();
        }
        return x.await_resume();
    }
}


}
