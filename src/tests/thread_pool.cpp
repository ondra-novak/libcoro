#include "check.h"
#include "../coro/thread_pool.h"
#include "../coro/coro.h"
#include "../coro/async.h"


coro::coroutine bg_task(coro::thread_pool &pool, std::atomic<int> &counter) {
    co_await pool;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    ++counter;
}

void test1() {
    std::atomic<int> counter = 0;
    coro::thread_pool tpool(2);
    for (int i = 0; i < 10; ++i) {
        bg_task(tpool, counter);
    }
    tpool.join().wait();
    int c = counter.load();
    CHECK_EQUAL(c, 10);
}

coro::future<void> test2() {
    std::atomic<int> counter = 0;
    coro::thread_pool tpool(2);
    for (int i = 0; i < 10; ++i) {
        bg_task(tpool, counter);
    }
    co_await tpool.join();
    int c = counter.load();
    CHECK_EQUAL(c, 10);
}


int main() {
    test1();
    test2().wait();
}
