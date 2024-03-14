#include "../coro/condition.h"
#include "../coro/async.h"
#include "check.h"

#include <queue>
coro::async<void> test_coro(int mult, coro::promise<int> &shared, int &r) {

    coro::future<int> f;
    shared += f.get_promise();
    int val = co_await f;
    r = mult*val;
}



int main() {

    int r1 = 0;
    int r2 = 0;
    int r3 = 0;

    coro::promise<int> shared;
    test_coro(1,shared,r1).detach();
    test_coro(2,shared,r2).detach();
    test_coro(3,shared,r3).detach();

    shared(10);

    CHECK_EQUAL(r1 ,10);
    CHECK_EQUAL(r2 ,20);
    CHECK_EQUAL(r3 ,30);
    return 0;


}
