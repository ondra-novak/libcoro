#include "check.h"
#include "../coro/future.h"
#include "../coro/async.h"


coro::future<void> test_all(coro::future<void> &f1, coro::future<void> &f2, coro::future<void> &f3) {
    co_await coro::all_of({f1,f2,f3});
}

coro::future<unsigned int> test_any(coro::future<void> &f1, coro::future<void> &f2, coro::future<void> &f3) {
    co_return co_await coro::any_of({f1,f2,f3});
}



void test1() {
    coro::promise<void> p1,p2,p3;
    coro::future<void> f1([&](auto prom){p1 = std::move(prom);});
    coro::future<void> f2([&](auto prom){p2 = std::move(prom);});
    coro::future<void> f3([&](auto prom){p3 = std::move(prom);});
    auto f = test_all(f1,f2,f3);
    f.start();
    p1();
    p2();
    p3();
    CHECK(!f.is_pending());
}

void test2() {
    coro::promise<void> p1,p2,p3;
    coro::future<void> f1([&](auto prom){p1 = std::move(prom);});
    coro::future<void> f2([&](auto prom){p2 = std::move(prom);});
    coro::future<void> f3([&](auto prom){p3 = std::move(prom);});
    auto f = test_all(f1,f2,f3);
    p1();
    p2();
    p3();
    f.start();
    CHECK(!f.is_pending());
}

void test3() {
    coro::promise<void> p1,p2,p3;
    coro::future<void> f1([&](auto prom){p1 = std::move(prom);});
    coro::future<void> f2([&](auto prom){p2 = std::move(prom);});
    coro::future<void> f3([&](auto prom){p3 = std::move(prom);});
    auto f = test_any(f1,f2,f3);
    f.start();
    p2();
    CHECK(!f.is_pending());
    p1();
    p3();
    CHECK_EQUAL(f.get() ,1);
}

void test4() {
    coro::promise<void> p1,p2,p3;
    coro::future<void> f1([&](auto prom){p1 = std::move(prom);});
    coro::future<void> f2([&](auto prom){p2 = std::move(prom);});
    coro::future<void> f3([&](auto prom){p3 = std::move(prom);});
    auto f = test_any(f1,f2,f3);
    p3();
    p2();
    p1();
    f.start();
    CHECK(!f.is_pending());
    CHECK_EQUAL(f.get() ,0);
}

int main() {
    test1();
    test2();
    test3();
    test4();
}

