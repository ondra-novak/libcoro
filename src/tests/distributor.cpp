#include "check.h"
#include "../coro/coro_single.h"

coro::async<void> test_coro(int id, coro::distributor<int> &dist, std::queue<std::pair<int,int > > &r) {

    while(1) {
        coro::future<int> f = dist.subscribe();
        if ( co_await !f) break;
        int val = f;
        r.push({id, val});
    }
}

coro::async<void> test_coro2(int id, coro::distributor<int> &dist, std::queue<std::pair<int,int > > &r) {

    try {
        while (true) {
            coro::future<int> f = dist.subscribe();
            int val = co_await f;
            r.push({id, val});
        }
    } catch (const coro::broken_promise_exception &) {

    }
}

coro::async<void> test_coro_queued(int id, coro::distributor<int> &dist, std::queue<std::pair<int,int > > &r) {

    coro::distributor<int>::queue q(dist);

    while(1) {
        coro::future<int> f = q.pop();
        if ( co_await !f) break;
        int val = f;
        r.push({id, val});
    }
}


int main() {
    coro::distributor<int> d;
    std::queue<std::pair<int,int> > results;
    test_coro(1,d,results).detach();
    test_coro2(2,d,results).detach();
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
