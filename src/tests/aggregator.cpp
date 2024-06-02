#include "../coro/aggregator.h"

#include "../coro/async.h"
#include "../coro/scheduler.h"
#include "check.h"

#include <iostream>
#include <sstream>
#include <set>

template<typename Alloc>
coro::generator<int, Alloc> fibo(Alloc &, int count) {
    LIBCORO_TRACE_SET_NAME(count);
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
     LIBCORO_TRACE_SET_NAME(count," ",sleep);
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
     LIBCORO_TRACE_SET_NAME();
    auto aggr = coro::aggregator(async_fibo(sch, 8, 5), async_fibo(sch,12,6), async_fibo(sch,3,7));
    std::ostringstream sout;
    std::set<int> res;
    auto r = aggr();
    while (co_await !!r) {
        int x = r;
        res.emplace(x);
        LIBCORO_TRACE_LOG("value:", x);
        r = aggr();
    }
    auto values = {5,6,7,10,12,15,14,18,25,30,40,65,48,105,78,126,204,330,534,864};
    for (int c:values ) {
        CHECK_PRINT(res.find(c) != res.end(),c);
    }

}

coro::future<void> test_async_fibo_intr(coro::scheduler &sch) {
    LIBCORO_TRACE_SET_NAME();

    auto aggr = coro::aggregator(async_fibo(sch, 8, 5), async_fibo(sch,12,6), async_fibo(sch,3,7));
    std::ostringstream sout;
    std::set<int> res;
    auto r = aggr();
    while (co_await !!r) {
        int x = r;
        res.emplace(x);
        LIBCORO_TRACE_LOG("value:", x);
        if (x == 65)
            break;
        r = aggr();
    }
    auto values = {5,6,7,10,12,15,14,18,25,30,40,65};
    for (int c:values ) {
        CHECK_PRINT(res.find(c) != res.end(),c);
    }

}


int main() {

    LIBCORO_TRACE_SEPARATOR("synchronous");
    coro::scheduler sch;
    std::ostringstream sout;
    {
        coro::reusable_allocator reuse_alloc;
        coro::std_allocator std_alloc;
        coro::reusable_allocator gen_alloc;

        auto aggr = coro::aggregator<int,coro::std_allocator, coro::reusable_allocator>(gen_alloc, fibo(reuse_alloc, 8), fibo(std_alloc, 12), fibo(std_alloc, 3));

        for (int x: aggr) {
            LIBCORO_TRACE_LOG("value:", x);
            sout << x << ',';
        }

        CHECK_EQUAL(sout.str(), "1,1,1,1,1,1,2,2,2,3,3,5,5,8,8,13,13,21,21,34,55,89,144,");
    }

    LIBCORO_TRACE_SEPARATOR("asynchronous");
    sch.run(test_async_fibo(sch));
    LIBCORO_TRACE_SEPARATOR("asynchronous with interrupt");
    sch.run(test_async_fibo_intr(sch));



    return 0;
}
