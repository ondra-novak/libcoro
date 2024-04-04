#include "check.h"
#include "../coro/async.h"
#include "../coro/future.h"

struct destroy_me: std::suspend_always {
    void await_suspend(std::coroutine_handle<> h) {
        h.destroy();
    }
};


coro::future<int> destroyed() {
    co_await destroy_me();
    co_return  42;
}



int main() {
    CHECK_EXCEPTION(coro::await_canceled_exception, {destroyed().get();});
}
