#include "check.h"
#include "../coro/mutex.h"

#include "../coro/coroutine.h"
#include "../coro/async.h"
#include <thread>



coro::coroutine mutex_test(coro::mutex &mx, bool &out) {
    auto own = co_await mx;
    out = true;
}


template class coro::lock<coro::mutex, coro::mutex, coro::mutex>;


int test1() {
    coro::mutex mx;

    bool t1_ok = false;

    auto own = mx.try_lock();
    CHECK(own);
    auto own2 = mx.try_lock();
    CHECK(!own2);

    coro::mutex::ownership own3;

    auto cb1  =mx.lock();
    cb1 >> [&]{
        auto own = cb1.await_resume();
        t1_ok = true;
    };

    auto cb2 = mx.lock();
    cb2 >> [&] {
        own3 = cb2.get();
    };


    CHECK(!t1_ok);
    CHECK(!own3);
    own.release();
    CHECK(t1_ok);
    CHECK(own3);

    bool t2_ok = false;

    mutex_test(mx, t2_ok);
    CHECK(!t2_ok);
    own3.release();
    CHECK(t2_ok);



return 0;
}

int test2() {
    coro::mutex mx1;
    coro::mutex mx2;
    coro::mutex mx3;

    auto own2 = mx2.lock_sync();
    auto own3 = mx3.lock_sync();

    auto f = coro::lock(mx1,mx2,mx3);
    CHECK(f.is_pending());

    own2.release();
    CHECK(f.is_pending());
    auto own1 = mx1.lock_sync();
    own3.release();
    CHECK(f.is_pending());
    own1.release();
    CHECK(!f.is_pending());

    return 0;
}


int main() {
    test1();
    test2();
    return 0;
}


