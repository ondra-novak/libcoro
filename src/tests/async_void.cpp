#include "../coro/async.h"
#include "../coro/future.h"
#include "check.h"

int test_var = 0;

coro::async<void> void_coro(int x) {
    test_var = x;
    co_return;
}

coro::async<void> await_coro(int x) {
    co_return co_await void_coro(x);
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
    void_coro(1).detach();
    CHECK(test_var == 1);
    void_coro(2).run();
    CHECK(test_var == 2);
    void_coro(3).start().get();
    CHECK(test_var == 3);
    await_coro(4).run();
    CHECK(test_var == 4);
    coro::future<void> v;
    void_coro(5).start(v.get_promise());
    v.wait();
    CHECK(test_var == 5);
    {
        //will not execute
        auto c = void_coro(6);
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
}
