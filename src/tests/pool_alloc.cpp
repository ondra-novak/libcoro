#include "../coro/pool_alloc.h"
#include "../coro/async.h"
#include "../coro/future.h"
#include "../coro/thread_pool.h"
#include "../coro/generator.h"
#include "check.h"


template<typename AllocFn>
void test1(AllocFn fn) {
    std::vector<void *> _ptrs;

    for (int i = 1; i < 100; ++i) {
        void *p = fn(i*16);
        _ptrs.push_back(p);
    }
    for (int i = 1; i < 100; ++i) {
        coro::pool_alloc::dealloc(_ptrs[i-1], i*16);
    }
    for (int i = 1; i < 100; ++i) {
        void *p = fn(i*16);
        CHECK(p == _ptrs[i-1]);
        coro::pool_alloc::dealloc(p, i*16);
    }

}

void *sync_alloc(std::size_t sz) {
    return coro::pool_alloc::alloc(sz);
}

coro::future<void *> async_alloc(coro::thread_pool &tpool, std::size_t sz) {
    return [&](auto promise) {
        tpool.enqueue([sz, promise = std::move(promise)]() mutable {
            promise(coro::pool_alloc::alloc(sz));
        });
    };
}

auto make_sync_alloc() {
    return [](std::size_t sz) {
        return sync_alloc(sz);
    };
}

auto make_async_alloc(coro::thread_pool &tpool) {
    return [&tpool](std::size_t sz) {
        return async_alloc(tpool, sz);
    };
}

coro::generator<int, coro::pool_alloc> fibo(int count) {
    unsigned int a = 1;
    unsigned int b = 1;

    for (int i = 0; i < count; ++i) {
        co_yield a;
        int c = a+b;
        a = b;
        b = c;
    }

}


coro::async<int,coro::pool_alloc> recursive_fibonacci(int n) {
    if (n <= 1) {
        co_return n;
    } else {
        int res = co_await recursive_fibonacci( n - 2) + co_await recursive_fibonacci(n - 1);
        co_return res;
    }
}



int main() {
    test1(make_sync_alloc());
    coro::thread_pool pool(1);
    test1(make_async_alloc(pool));

    int results[] = {1,1,2,3,5,8,13,21,34,55};

    auto iter = std::begin(results);
    for (int v: fibo(10)) {
        int x = *iter;
        ++iter;
        CHECK_EQUAL(x,v);
    }

    int r = recursive_fibonacci(10);
    CHECK_EQUAL(r, 55);
}
