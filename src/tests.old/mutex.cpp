#include "../coro.old/mutex.h"

#include <../coro.old/async.h>
#include <../coro.old/future.h>
#include <../tests.old/check.h>

#include <thread>



coro::async<void> mutex_test(coro::mutex &mx, bool &out) {
    auto own = co_await mx;
    out = true;
}



int main() {
    coro::mutex mx;

    bool t1_ok = false;

    auto own = mx.try_lock();
    CHECK(own != nullptr);
    auto own2 = mx.try_lock();
    CHECK(own2 == nullptr);

    coro::mutex::ownership own3;

    mx.register_target(coro::target_callback_activation<coro::mutex::target_type>([&](coro::mutex::ownership &&){
        t1_ok = true;
    }));
    mx.register_target(coro::target_callback_activation<coro::mutex::target_type>([&](coro::mutex::ownership &&own){
        own3 = std::move(own);
    }));

    CHECK(!t1_ok);
    CHECK(own3 == nullptr);
    own.reset();
    CHECK(t1_ok);
    CHECK(own3 != nullptr);

    bool t2_ok = false;

    coro::future<void> f = mutex_test(mx, t2_ok);
    CHECK(!t2_ok);
    own3.reset();
    CHECK(t2_ok);




}
