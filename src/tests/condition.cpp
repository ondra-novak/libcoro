#include "../coro/condition.h"
#include "../coro/async.h"
#include "check.h"

#include <queue>
coro::async<void> test_coro(int mult, int &shared, std::queue<int> &r) {

    int val = shared;
    while(true) {
        val = co_await coro::condition_equal(shared, val);
        if (val == -1) break;
        r.push(mult*val);

    }
}



int main() {

    std::queue<int > results1;
    std::queue<int > results2;
    std::queue<int > results3;
    int shared;
    test_coro(1,shared,results1).detach();
    test_coro(2,shared,results2).detach();
    test_coro(3,shared,results3).detach();

    shared = 10;
    coro::notify_condition(shared);
    shared = 20;
    coro::notify_condition(shared);
    shared = 30;
    coro::notify_condition(shared);
    shared = -1;
    coro::notify_condition(shared);

    for (int i = 1; i <= 3; i++) {
        CHECK_EQUAL(results1.front() ,i*10);
        results1.pop();
    }
    for (int i = 1; i <= 3; i++) {
        CHECK_EQUAL(results2.front() , i*20);
        results2.pop();
    }
    for (int i = 1; i <= 3; i++) {
        CHECK_EQUAL(results3.front() , i*30);
        results3.pop();
    }

    return 0;


}
