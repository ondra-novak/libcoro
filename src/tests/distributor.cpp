#include "check.h"
#include "../coro/coro_single.h"

coro::async<void> test_coro(int id, coro::distributor<int> &dist, std::queue<std::pair<int,int > > &r) {

    coro::future<int> f = dist.subscribe();
    while (co_await f.has_value()) {
        int val = f;
        r.push({id, val});
        dist.subscribe(f.get_promise());
    }
}

coro::async<void> test_coro_queued(int id, coro::distributor<int> &dist, std::queue<std::pair<int,int > > &r) {

    coro::distributor<int>::queue q(dist);

    coro::future<int> f = q.pop();
    while (co_await f.has_value()) {
        int val = f;
        r.push({id, val});
        q.pop(f.get_promise());
    }
}


int main() {
    coro::distributor<int> d;
    std::queue<std::pair<int,int> > results;
    test_coro(1,d,results).detach();
    test_coro(2,d,results).detach();
    test_coro_queued(3,d,results).detach();

    d.publish(10);
    d.publish(20);
    d.publish(30);

    for (int i = 1; i <= 3; i++) {
        for (int j = 1; j <= 3; j++) {
            auto x = results.front();
            results.pop();
            CHECK_EQUAL(x.first,j);
            CHECK_EQUAL(x.second,i*10);
        }
    }

    return 0;


}
