#include "check.h"
#include "../coro/fiber.h"
#include "../coro/scheduler.h"

void test_coro(coro::scheduler *sch) {
    coro::fiber::await(sch->sleep_for(std::chrono::milliseconds(100)));
}


int main() {
    coro::scheduler sch;
    auto fut = coro::fiber::start(8196, &test_coro, &sch);
    sch.run(fut);


    return 0;
}
