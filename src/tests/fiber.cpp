#include "check.h"
#include <coro_fiber.h>

void test_coro(coro::scheduler *sch) {
    coro::fiber::await(sch->sleep_for(std::chrono::milliseconds(100)));
}


std::vector<int> results = {
    0,10,20,30,40,
    1,11,21,31,41,
    2,12,22,32,42,
    3,13,23,33,43,
    4,14,24,34,44,
};

static auto iter = results.begin();

void test_task(int id) {
    for (int j = 0; j < 5; j++) {
        int v = id*10+j;
        CHECK_EQUAL(v, *iter);
        ++iter;
        //switch to next task.
        coro::fiber::await(coro::suspend());
    }
}


void test_cooperative() {

    //initialize qswitch - transfer execution into the queue
    coro::fiber::await(coro::suspend());

    //now we can create tasks running in cooperative multitasking
    for (int i = 0; i < 5; i++) {
        coro::fiber::create_detach(8192, &test_task, i);
    }
    //by leaving this function, all tasks continues in execution
}



int main() {
    coro::scheduler sch;
    auto fut = coro::fiber::create(8196, &test_coro, &sch);
    sch.run(fut);

    coro::fiber::await(coro::fiber::create(8192, &test_cooperative));

    return 0;
}
