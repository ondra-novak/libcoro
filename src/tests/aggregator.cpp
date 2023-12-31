#include "check.h"
#include "../coro/aggregator.h"
#include "../coro/scheduler.h"
#include "../coro/async.h"

#include <iostream>
#include <sstream>
#include <set>

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
        co_yield a*sleep;
        int c = a+b;
        a = b;
        b = c;
    }

}

coro::future<void> test_async_fibo(coro::scheduler &sch) {
    auto aggr = coro::aggregator(async_fibo(sch, 8, 5), async_fibo(sch,12,6), async_fibo(sch,3,7));
    std::ostringstream sout;
    std::set<int> res;
    auto r = aggr();
    while (co_await !!r) {
        int x = r;
        res.emplace(x);
        r = aggr();
    }
    auto values = {5,6,7,10,12,15,14,18,25,30,40,65,48,105,78,126,204,330,534,864};
    for (int c:values ) {
        CHECK_PRINT(res.find(c) != res.end(),c);
    }

}

coro::future<void> test_async_fibo_intr(coro::scheduler &sch) {
    auto aggr = coro::aggregator(async_fibo(sch, 8, 5), async_fibo(sch,12,6), async_fibo(sch,3,7));
    std::ostringstream sout;
    std::set<int> res;
    auto r = aggr();
    while (co_await !!r) {
        int x = r;
        res.emplace(x);
        if (x == 65) break;
        r = aggr();
    }
    auto values = {5,6,7,10,12,15,14,18,25,30,40,65};
    for (int c:values ) {
        CHECK_PRINT(res.find(c) != res.end(),c);
    }

}


int main() {

    std::ostringstream sout;
    coro::scheduler sch(0);

    auto aggr = coro::aggregator(fibo(8), fibo(12), fibo(3));

    for (int x: aggr) {
        sout << x << ',';
    }

    CHECK_EQUAL(sout.str(), "1,1,1,1,1,1,2,2,2,3,3,5,5,8,8,13,13,21,21,34,55,89,144,");

    sch.await(test_async_fibo(sch));
    sch.await(test_async_fibo_intr(sch));



    return 0;
}
