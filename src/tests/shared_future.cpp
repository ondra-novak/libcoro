#include "check.h"

#include "../coro/coro_single.h"


coro::async<void> test_coro(int id, coro::shared_future<int> fut, std::queue<std::pair<int,int > > &r) {
    int val = co_await fut;
    r.push({id, val});
}

coro::shared_lazy_future<int> test_coro_shared() {
    co_return 42;
}

int main() {

    coro::shared_future<int> f;
    std::queue<std::pair<int,int > > q;
    auto p = f.get_promise();
    coro::future<void> c1 = test_coro(1,f,q);
    coro::shared_future<void> c2 = test_coro(2,f,q);
    coro::future<void> c3 = test_coro(3,f,q);
    coro::shared_lazy_future<void> c4 = test_coro(4,f,q); //will not called

    c2.reset(); //this should crash if hold reference on shared promise doesn't work

    auto test_lazy_coro = test_coro_shared();

    p(42);

    for (int i = 1; i <= 3; ++i) {
        auto z = q.front();
        q.pop();
        CHECK_EQUAL(z.first, i);
        CHECK_EQUAL(z.second, 42);
    }
    CHECK(q.empty());


    CHECK_EQUAL((int)test_lazy_coro, 42);


    return 0;
}
