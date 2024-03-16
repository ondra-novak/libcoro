#include <iostream>
#include "../coro/coro.h"
#include "../coro/cooperative.h"
#include "check.h"

std::vector<int> results = {
    0,10,20,30,40,
    1,11,21,31,41,
    2,12,22,32,42,
    3,13,23,33,43,
    4,14,24,34,44,
};

static auto iter = results.begin();

coro::coro test_task(int id) {
    for (int j = 0; j < 5; j++) {
        int v = id*10+j;
        CHECK_EQUAL(v, *iter);
        ++iter;
        //switch to next task.
        co_await coro::suspend();
    }
}


coro::coro test_cooperative() {

    //initialize qswitch - transfer execution into the queue
    co_await coro::suspend();

    //now we can create tasks running in cooperative multitasking
    for (int i = 0; i < 5; i++) {
        test_task(i);
    }
    //by leaving this function, all tasks continues in execution
}


int main(int, char **) {
    test_cooperative();

}
