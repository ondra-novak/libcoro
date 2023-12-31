#include "../coro/coro_single.h"

#include "check.h"

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

coro::generator<int &> async_fibo(coro::scheduler &sch, int count) {
    unsigned int a = 1;
    unsigned int b = 1;

    for (int i = 0; i < count; ++i) {
        co_await sch.sleep_for(std::chrono::milliseconds(10));
        co_yield a;
        unsigned int c = a+b;
        a = b;
        b = c;
    }
}

coro::async<void> async_fibo_test(coro::scheduler &sch) {
    int results[] = {1,1,2,3,5,8,13,21,34,55};
    auto gen = async_fibo(sch,20);
    for (auto i: results) {
        int v = co_await gen();
        CHECK_EQUAL(v,i);
    }
}


int main() {
    coro::scheduler sch(1);

    int results[] = {1,1,2,3,5,8,13,21,34,55};

    auto iter = std::begin(results);
    for (int v: fibo(10)) {
        int x = *iter;
        ++iter;
        CHECK_EQUAL(x,v);
    }

    iter = std::begin(results);
    for (int v: async_fibo(sch, 10)) {
        int x = *iter;
        ++iter;
        CHECK_EQUAL(x,v);
    }

    async_fibo_test(sch).join();
}
