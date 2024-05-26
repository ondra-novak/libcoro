#include "../coro/distributor.h"
#include "../coro/async.h"
#include "check.h"
#include <thread>

coro::async<void> test_coro(int id, coro::distributor<int> &dist, std::queue<std::pair<int,int > > &r) {

    while(1) {
        auto f = dist.subscribe();
        if ( co_await !f) break;
        int val = f.get();
        r.push({id, val});
    }
}

coro::async<void> test_coro2(int id, coro::distributor<int> &dist, std::queue<std::pair<int,int > > &r) {

    try {
        while (true) {
            auto f = dist.subscribe();
            int val = co_await f;
            r.push({id, val});
        }
    } catch (const coro::await_canceled_exception &) {

    }
}

coro::async<void> test_coro_queued(int id, coro::distributor<int> &dist, std::queue<std::pair<int,int > > &r) {

    coro::distributor<int>::queue<> q(dist);

    while(1) {
        coro::future<int> f = q.pop();
        if ( co_await !f) break;
        int val = f;
        r.push({id, val});
    }
}

void test_thread(int id, coro::distributor<int> &dist, std::queue<std::pair<int,int > > &r, std::atomic<bool> &unlk) {
    auto lk = dist.subscribe().init_lock(unlk);
    while (true) {
        dist.subscribe(&unlk).lock(lk);
        if (lk == nullptr) break;
        int val = *lk;
        r.push({id,val});
    }
}


int main() {
     coro::distributor<int> d;
    std::queue<std::pair<int,int> > results;
    test_coro(1,d,results).detach();
    test_coro2(2,d,results).detach();
    test_coro_queued(3,d,results).detach();
    std::atomic<bool> unlk(false);
    std::thread thr([&]{test_thread(4, d, results, unlk);});
    unlk.wait(false);

    int v = 10;
    const int w= 20;
    d.publish(v);
    d.publish(w);
    d.publish(30);

    for (int i = 1; i <= 3; i++) {
        for (int j = 1; j <= 4; j++) {
            auto x = results.front();
            results.pop();
            CHECK_EQUAL(x.first,j);
            CHECK_EQUAL(x.second,i*10);
        }
    }

    d.drop(&unlk);
    thr.join();
    return 0;


}
