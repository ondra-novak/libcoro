#include <iostream>
#include "../coro/future.h"
#include "../coro/async.h"
#include "../coro/thread_pool.h"
#include "check.h"

std::vector<int> results = {
    0,10,20,30,40,
    1,11,21,31,41,
    2,12,22,32,42,
    3,13,23,33,43,
    4,14,24,34,44,
};

static auto iter = results.begin();

coro::async<void> test_task(int id, coro::scheduler &sch) {
    for (int j = 0; j < 5; j++) {
        int v = id*10+j;
        CHECK_EQUAL(v, *iter);
        ++iter;
        co_await sch;
    }
}


coro::future<void> test_cooperative(coro::scheduler &sch) {
    //cooperative mode need to be initialized in a coroutine.
    //The cooperative execution starts once coroutine exits
       for (int i = 0; i < 5; i++) {
           test_task(i,sch).detach();
       }
    while (!sch.is_idle()) {
        co_await sch;
    }
}


int main(int, char **) {
    coro::scheduler sch(0);
    sch.await(test_cooperative(sch));

}
