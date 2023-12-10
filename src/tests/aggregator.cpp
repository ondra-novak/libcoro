#include "check.h"
#include "../coro/aggregator.h"
#include "../coro/scheduler.h"
#include "../coro/async.h"

#include <iostream>
#include <sstream>

coro::generator<int> fibo(int count) {
    int a = 1;
    int b = 1;

    for (int i = 0; i < count; ++i) {
        co_yield a;
        int c = a+b;
        a = b;
        b = c;
    }

}


coro::generator<int> async_fibo(coro::scheduler &sch, int count, int sleep) {
    int a = 1;
    int b = 1;

    for (int i = 0; i < count; ++i) {
        co_await sch.sleep_for(std::chrono::milliseconds(sleep));
        co_yield a;
        int c = a+b;
        a = b;
        b = c;
    }

}

coro::future<void> test_async_fibo(coro::scheduler &sch) {
    auto aggr = coro::aggregator(async_fibo(sch, 8, 5), async_fibo(sch,12,7), async_fibo(sch,3,11));
    std::ostringstream sout;
    auto r = aggr();
    while (co_await !!r) {
        int x = r;
        sout << x << ',';
        r = aggr();
    }
    CHECK_EQUAL(sout.str(), "1,1,1,1,1,2,3,2,1,5,3,8,2,5,13,21,8,13,21,34,55,89,144,");
}

int main() {

    std::ostringstream sout;
    coro::scheduler sch;

    auto aggr = coro::aggregator(fibo(8), fibo(12), fibo(3));

    for (int x: aggr) {
        sout << x << ',';
    }

    CHECK_EQUAL(sout.str(), "1,1,1,1,1,1,2,2,2,3,3,5,5,8,8,13,13,21,21,34,55,89,144,");

    sch.await(test_async_fibo(sch));


    return 0;
}
