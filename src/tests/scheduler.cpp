#include "../coro/scheduler.h"

#include "../coro/async.h"
#include "../coro/future.h"
#include "../coro/queue.h"
#include "check.h"

#include <iostream>

coro::async<void> test_cycle_coro(coro::scheduler &sch, coro::promise<void> endflag, const void *ident) {
    try {
        int pos = 0;
        while (pos < 20) {
            if (pos == 5) endflag();
            co_await sch.sleep_for(std::chrono::milliseconds(100), ident);
            ++pos;
        }
    } catch (coro::await_canceled_exception &) {
        //empty
    }
}


void test_cycle() {
    coro::future<void> wflag;
    coro::scheduler sch;
    sch.start();
    auto tm1 = std::chrono::system_clock::now();
    auto fut = test_cycle_coro(sch, wflag.get_promise(), &wflag).start();
    wflag.wait();
    auto blk = sch.cancel(&wflag);
    fut.wait();
    auto tm2 = std::chrono::system_clock::now();
    CHECK_BETWEEN(400,std::chrono::duration_cast<std::chrono::milliseconds>(tm2-tm1).count(),600);
}


coro::future<void> test_thread_pool_coro(coro::scheduler &sch,
        int id, unsigned int mswait1, unsigned int mswait2,
        coro::queue<int> &started, coro::queue<int> &finished) {

    co_await sch.sleep_for(std::chrono::milliseconds(mswait1));
    started.push(id);
    std::this_thread::sleep_for(std::chrono::milliseconds(mswait2));
    finished.push(id);


}

void test_thread_pool() {
    coro::scheduler sch;
    sch.start(coro::scheduler::thread_pool(4));
    coro::queue<int> started;
    coro::queue<int> finished;
    auto f1 = test_thread_pool_coro(sch, 1, 100, 500, started, finished);
    auto f2 = test_thread_pool_coro(sch, 2, 200, 300, started, finished);
    auto f3 = test_thread_pool_coro(sch, 3, 150, 300, started, finished);
    auto f4 = test_thread_pool_coro(sch, 4, 250, 1, started, finished);
    int started_res[] = {1,3,2,4};
    int finished_res[] = {4,3,2,1};

    f1.start();
    f2.start();
    f3.start();
    f4.start();
    f1.wait();
    f2.wait();
    f3.wait();
    f4.wait();
    for (auto &x: started_res) {
        int r = started.pop();
        CHECK_EQUAL(r,x);
    }
    for (auto &x: finished_res) {
        int r = finished.pop();
        CHECK_EQUAL(r,x);
    }

}


coro::future<int> test_signle_thread_scheduler(coro::scheduler &sch) {
    co_await sch.sleep_for(std::chrono::milliseconds(100));
    co_return 42;
}

void test_signle_thread_scheduler() {
    coro::scheduler sch;
    auto tm1 = std::chrono::system_clock::now();
    auto f = test_signle_thread_scheduler(sch);
    int res = sch.run(f);
    CHECK_EQUAL(res, 42);
    auto tm2 = std::chrono::system_clock::now();
    CHECK_BETWEEN(90,std::chrono::duration_cast<std::chrono::milliseconds>(tm2-tm1).count(),150);
}


coro::future<void> scheduler_killer(std::unique_ptr<coro::scheduler> sch) {
    co_await sch->sleep_for(std::chrono::milliseconds(100));
    sch.reset();    //killed in scheduler's thread
}

void test_kill_scheduler() {
    std::unique_ptr<coro::scheduler> sch = std::make_unique<coro::scheduler>();
    sch->start();
    auto f = scheduler_killer(std::move(sch));
    f.get();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

int main() {
    test_cycle();
    test_thread_pool();
    test_signle_thread_scheduler();
    test_kill_scheduler();

}
