#include "../coro/async.h"
#include "../coro/future.h"
#include "check.h"
#include "../coro/make_awaitable.h"


int int_normal(int x) {
    return x;
}

coro::future<int> int_coro(int x) {
    co_return x;
}

coro::async<int> int_indirectly_coro(int x) {
    co_return x;
}

static_assert(coro::make_awaitable<coro::function<int()> >::is_async == false);
static_assert(coro::make_awaitable<coro::function<coro::future<int>()> >::is_async == true);
static_assert(coro::make_awaitable<coro::function<coro::async<int>()> >::is_async == true);


coro::async<void> test_coro() {
    int r1 = co_await coro::make_awaitable([&]{return int_normal(10);});
    CHECK_EQUAL(r1, 10);
    int r2 = co_await coro::make_awaitable([&]{return int_coro(25);});
    CHECK_EQUAL(r2, 25);
    int r3 = co_await coro::make_awaitable([&]{return int_indirectly_coro(42);});
    CHECK_EQUAL(r3, 42);
}

int main() {
    test_coro().run();
}
