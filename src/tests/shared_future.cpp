#include "../coro/async.h"
#include "../coro/future.h"
#include "../coro/queue.h"
#include "check.h"


coro::async<void> test_coro(int id, coro::shared_future<int> fut, std::queue<std::pair<int,int > > &r) {
    int val = co_await fut;
    r.push({id, val});
}


int main() {

    std::allocator<int> alloc;
    coro::shared_future<int> f;
    std::queue<std::pair<int,int > > q;
    auto p = f.get_promise();
    coro::future<void> c1 = test_coro(1,f,q);
    coro::shared_future<void> c2 = test_coro(2,f,q).shared_start();
    coro::shared_future<void> c3 (alloc, [&]{return test_coro(3,f,q).start();});

    c2.reset(); //this should crash if hold reference on shared promise doesn't work

    bool called = false;

    auto cc = f;
    cc.then([&]{
        called = true;
    });


    p(42);

    CHECK(called);

    for (int i = 1; i <= 3; ++i) {
        auto z = q.front();
        q.pop();
        CHECK_EQUAL(z.first, i);
        CHECK_EQUAL(z.second, 42);
    }
    CHECK(q.empty());

    coro::future<void> c4 = test_coro(4,f,q);

    {
        auto z = q.front();
        q.pop();
        CHECK_EQUAL(z.first, 4);
        CHECK_EQUAL(z.second, 42);
    }

    CHECK(q.empty());

    coro::shared_future<int> g;
    g.get_promise()(56);
    coro::future<void> c5 = test_coro(5,g,q);

    {
        auto z = q.front();
        q.pop();
        CHECK_EQUAL(z.first, 5);
        CHECK_EQUAL(z.second, 56);
    }

    CHECK(q.empty());


    return 0;
}
