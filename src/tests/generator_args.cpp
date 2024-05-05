#include "check.h"
#include "../coro/generator.h"
#include "../coro/scheduler.h"
#include "../coro/async.h"


coro::generator<int(int, int)> sum_test(int mult) {
    do {
        auto [a,b] = co_yield coro::fetch_args;
        co_yield (a+b)*mult;
    } while (true);
}

coro::generator<int(int, int)> sum_test_async(coro::scheduler &sch, int mult) {
    do {
        auto [a,b] = co_yield coro::fetch_args;
        co_await sch.sleep_for(std::chrono::milliseconds(1));
        co_yield (a+b)*mult;
    } while (true);
}


void test1() {
    auto g = sum_test(10);
    int r = g(2,3);
    CHECK_EQUAL(r, 50);
    r = g(5,-4);
    CHECK_EQUAL(r, 10);
}

coro::future<void> async_test1(coro::scheduler &sch) {
    auto g = sum_test_async(sch, 5);
    int r = co_await g(2,3);
    CHECK_EQUAL(r, 25);
    r = co_await g(5,-4);
    CHECK_EQUAL(r, 5);
}

int main() {
    test1();
    coro::scheduler sch;
    sch.run(async_test1(sch));
}
