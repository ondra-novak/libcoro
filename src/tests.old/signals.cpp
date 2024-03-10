#include <../coro.old/coro_linux.h>
#include <../tests.old/check.h>

coro::async<void> test1(int sig, const void *ident = nullptr) {
    co_await coro::signal_control::listen({sig}, ident);
    co_return;
}




int main() {

    int ident2;
    coro::future<void> t1 = test1(SIGUSR1);
    coro::future<void> t2 = test1(SIGUSR2, &ident2);
    coro::future<void> t3 = test1(SIGUSR1);
    raise(SIGUSR1);
    std::this_thread::sleep_for(std::chrono::seconds(1));
    CHECK(!t1.is_pending());
    CHECK(t2.is_pending());
    CHECK(!t3.is_pending());

    CHECK(t1.has_value());
    CHECK(t3.has_value());

    coro::signal_control::drop(&ident2);
    CHECK(!t2.is_pending());
    CHECK_EXCEPTION(coro::broken_promise_exception, t2.get());



}
