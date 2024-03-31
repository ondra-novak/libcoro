#include "check.h"
#include "../coro/mutex.h"
#include "../coro/coro.h"
#include "../coro/async.h"
#include <thread>



coro::coroutine mutex_test(coro::mutex &mx, bool &out) {
    auto own = co_await mx;
    out = true;
}



int main() {
    coro::mutex mx;

    bool t1_ok = false;

    auto own = mx.try_lock();
    CHECK(own);
    auto own2 = mx.try_lock();
    CHECK(!own2);

    coro::mutex::ownership own3;

    mx.lock_callback([&](coro::mutex::ownership){
        t1_ok = true;
    });
    mx.lock_callback([&](coro::mutex::ownership own){
        own3 = std::move(own);
    });

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




}
