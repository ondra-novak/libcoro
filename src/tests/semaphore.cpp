#include "../coro/semaphore.h"
#include "../coro/coro.h"
#include "check.h"


coro::coroutine test_coro(coro::semaphore &sem, int &i) {
    co_await sem;
    ++i;
}



int main() {

    coro::semaphore sem(1);
    int val = 0;
    test_coro(sem, val);
    CHECK_EQUAL(val, 1);
    test_coro(sem, val);

    CHECK_EQUAL(val, 1);
    CHECK_EQUAL(sem.get(),-1);
    sem.release();
    CHECK_EQUAL(val, 2);
    CHECK_EQUAL(sem.get(),0);
    sem.release();
    CHECK_EQUAL(sem.get(),1);
    CHECK(sem.try_acquire());
    CHECK_EQUAL(sem.get(),0);
    test_coro(sem, val);
    test_coro(sem, val);
    test_coro(sem, val);
    test_coro(sem, val);
    CHECK_EQUAL(sem.get(),-4);
    sem.release();
    sem.release();
    sem.release();
    sem.release();
    CHECK_EQUAL(val, 6);

}
