#include "../coro/async.h"
#include "../coro/future.h"
#include "check.h"

int test_var = 0;

coro::async<int> int_coro(int x) {
    test_var = x;
    co_return x;
}

coro::async<int> await_coro(int x) {
    co_return co_await int_coro(x);
}

struct TwoVal {
    int a;
    int b;
    TwoVal(int a, int b):a(a),b(b) {}
    TwoVal(const TwoVal &) = delete;
    TwoVal &operator=(const TwoVal &) = delete;
};

coro::async<TwoVal> await_coro2(int x) {
    int i =  co_await int_coro(x);
    co_return coro::construct([i]{return TwoVal(i,i+10);});
}

struct destruct {
    void operator()(int *x) {
        test_var = *x;
        delete x;
    }
};

coro::async<int> int_coro2(std::unique_ptr<int, destruct> x) {
    co_return *x;
}

int main() {
    int_coro(1).detach();
    CHECK(test_var == 1);
    CHECK(int_coro(2) == 2);
    CHECK(int_coro(3).start().get() == 3);
    CHECK(await_coro(4) == 4);
    coro::future<int> v;
    int_coro(5).start(v.get_promise());
    CHECK(v.get() == 5);
    {
        //will not execute
        auto c = int_coro(6);
    }
    CHECK(test_var == 5);
    {
        //test whether destructor of variable will be called
        auto c = int_coro2(std::unique_ptr<int, destruct>(new int(10)));
    }
    CHECK(test_var == 10);
    {
        //test whether destructor of variable will be called
        auto c = int_coro2(std::unique_ptr<int, destruct>(new int(20)));
        c.detach();
    }
    CHECK(test_var == 20);
    coro::future<TwoVal> f = await_coro2(2);
    const TwoVal &z = f.get();
    CHECK(z.a == 2);
    CHECK(z.b == 12);
}
