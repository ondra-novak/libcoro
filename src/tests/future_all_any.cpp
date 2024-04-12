#include "check.h"
#include "../coro/future.h"
#include "../coro/future_list.h"
#include "../coro/scheduler.h"
#include "../coro/async.h"
#include "../coro/coroutine.h"
#include <list>


coro::future<void> test_all(coro::future<void> &f1, coro::future<void> &f2, coro::future<void> &f3) {
    co_await coro::all_of({f1,f2,f3});
}

coro::future<unsigned int> test_any(coro::future<unsigned int> &f1, coro::future<unsigned int> &f2, coro::future<unsigned int> &f3) {
    unsigned int x = co_await coro::any_of({f1,f2,f3});
    co_return x;
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
    coro::promise<unsigned int> p1,p2,p3;
    coro::future<unsigned int> f1([&](auto prom){p1 = std::move(prom);});
    coro::future<unsigned int> f2([&](auto prom){p2 = std::move(prom);});
    coro::future<unsigned int> f3([&](auto prom){p3 = std::move(prom);});
    auto f = test_any(f1,f2,f3);
    f.start();
    p2(2);
    CHECK(!f.is_pending());
    p1(1);
    p3(3);
    CHECK_EQUAL(f.get() ,2);
}

void test4() {
    coro::promise<unsigned int> p1,p2,p3;
    coro::future<unsigned int> f1([&](auto prom){p1 = std::move(prom);});
    coro::future<unsigned int> f2([&](auto prom){p2 = std::move(prom);});
    coro::future<unsigned int> f3([&](auto prom){p3 = std::move(prom);});
    auto f = test_any(f1,f2,f3);
    p3(3);
    p2(2);
    p1(1);
    f.start();
    CHECK(!f.is_pending());
    CHECK_EQUAL(f.get() ,1); //why? because all is done when coro started, and 1 is first on list
}

void tests() {
    std::vector<coro::future<int> *> x1;
    coro::any_of y1(x1);
    std::vector<std::unique_ptr<coro::future<int>  > > x2;
    coro::any_of y2(x2);

}

coro::async<int> delayed_async(coro::scheduler &sch, int id, unsigned int delay) {
    co_await sch.sleep_for(std::chrono::milliseconds(delay));
    co_return id;
}

coro::future<void> delay_async_test(coro::scheduler &sch) {
    coro::future<int> f1 = delayed_async(sch, 1, 40);
    coro::future<int> f2 = delayed_async(sch, 2, 10);
    coro::future<int> f3 = delayed_async(sch, 3, 30);
    coro::future<int> f4 = delayed_async(sch, 4, 50);
    int results[] = {2,3,1,4,-1};
    auto iter = std::begin(results);
    for (auto &fut: coro::when_each({f1,f2,f3,f4})) {
        int r = co_await fut;
        CHECK_EQUAL(*iter, r);
        ++iter;
    }
}

coro::future<void> delay_async_test2(coro::scheduler &sch) {
    coro::future<int> f1 = delayed_async(sch, 1, 40);
    coro::future<int> f2 = delayed_async(sch, 2, 10);
    coro::future<int> f3 = delayed_async(sch, 3, 30);
    coro::future<int> f4 = delayed_async(sch, 4, 50);
    {
        auto e = coro::when_each({f1,f2,f3,f4});
        int r = co_await *e.begin();
        CHECK_EQUAL(r,2);
    }
    co_await coro::all_of({f1,f2,f3,f4});
}


coro::future<void> task_list_test(coro::scheduler &sch) {
    coro::task_list<coro::future<int> > list;
    for (int i = 0; i < 100; ++i) {
        list.push_back(delayed_async(sch, i, i));
    }
    int i = 0;
    for (auto &fut: coro::when_each(list)) {
        int r = co_await fut;
        CHECK_EQUAL(i, r);
        ++i;
    }
}

int main() {
    test1();
    test2();
    test3();
    test4();

    coro::scheduler sch;
    sch.run(delay_async_test(sch));
    sch.run(delay_async_test2(sch));
    sch.run(task_list_test(sch));


}


