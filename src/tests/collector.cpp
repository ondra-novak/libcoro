#include "check.h"
#include "../coro/collector.h"
#include "../coro/scheduler.h"
#include <sstream>

coro::collector<std::string_view, std::string> sync_string_builder() {
    std::ostringstream buff;
    std::string_view data = co_yield nullptr;
    while (!data.empty()) {
        buff << data;
        data = co_yield nullptr;
    }
    co_return buff.str();
};

coro::collector<std::string_view, std::string> async_string_builder(coro::scheduler &sch) {
    std::ostringstream buff;
    std::string_view data = co_yield nullptr;
    while (!data.empty()) {
        buff << data;
        co_await sch.sleep_for(std::chrono::milliseconds(1));
        data = co_yield nullptr;
    }
    co_return buff.str();
};

coro::async<std::string> test_sync_builder_coro() {
    bool r;
    auto builder = sync_string_builder();
    r = co_await builder("Hello");
    CHECK(!r);
    r = co_await builder(" ");
    CHECK(!r);
    r = co_await builder("World");
    CHECK(!r);
    r = co_await builder("!");
    CHECK(!r);
    r = co_await builder("");
    CHECK(r);
    co_return builder.get();
}

coro::async<std::string> test_async_builder_coro(coro::scheduler &sch) {
    bool r;
    auto builder = async_string_builder(sch);
    r = co_await builder("Hello");
    CHECK(!r);
    r = co_await builder(" ");
    CHECK(!r);
    r = co_await builder("World");
    CHECK(!r);
    r = co_await builder("!");
    CHECK(!r);
    r = co_await builder("");
    CHECK(r);
    co_return builder.get();
}

std::string test_sync_builder() {
    bool r;
    auto builder = sync_string_builder();
    r = builder("Hello");
    CHECK(!r);
    r = builder(" ");
    CHECK(!r);
    r = builder("World");
    CHECK(!r);
    r = builder("!");
    CHECK(!r);
    r = builder("");
    CHECK(r);
    return builder.get();
}

int main() {
    std::string res = test_sync_builder_coro().run();
    CHECK_EQUAL(res, "Hello World!");
    std::string res2 = test_sync_builder_coro().run();
    CHECK_EQUAL(res2, "Hello World!");
    coro::scheduler sch;
    std::string res3 = sch.run(test_async_builder_coro(sch).start());
    CHECK_EQUAL(res3, "Hello World!");
}
