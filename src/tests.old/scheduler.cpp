#include "../coro.old/scheduler.h"

#include <../coro.old/async.h>
#include <../coro.old/future.h>
#include <../tests.old/check.h>

#include <iostream>


coro::future<int> co_test(coro::scheduler &pool) {

    std::thread::id id1, id2, id3, id4, id5;
    auto example_fn = [&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        id3 = std::this_thread::get_id();
        return 42;
    };
    id1 = std::this_thread::get_id();
    co_await pool;
    id2 = std::this_thread::get_id();
    auto f = pool.run(example_fn);;
    id4 = std::this_thread::get_id();
    CHECK_EQUAL(id2,id4);
    int r = co_await f;
    CHECK_NOT_EQUAL(id1,id2);
    id5 = std::this_thread::get_id();
    CHECK_EQUAL(id5,id3);
    auto t1 = std::chrono::system_clock::now();
    co_await pool.sleep_for(std::chrono::milliseconds(200));
    auto t2 = std::chrono::system_clock::now();
    CHECK_GREATER_EQUAL(std::chrono::duration_cast<std::chrono::milliseconds>(t2-t1).count(), 200);

    co_return r;
}

coro::future<void> co_test2(typename coro::future<int>::promise &p, int w, std::thread::id id, bool eq) {
    coro::future<int> f;
    p = f.get_promise();
    int v = co_await f;
    CHECK_EQUAL(v,w);
    auto id1 = std::this_thread::get_id();
    if (eq) {
        CHECK_EQUAL(id, id1);
    } else {
        CHECK_NOT_EQUAL(id, id1);
    }
}

coro::async<std::thread::id> get_id_coro() {
    co_return std::this_thread::get_id();
}

coro::async<void> cancel_coro(coro::scheduler &pool, void *id) {
    co_await pool.sleep_for(std::chrono::seconds(2), id);
}

int main(int, char **) {
    coro::future<void> ff;
    {
        coro::scheduler pool(5);
        int r = co_test(pool).get();
        CHECK_EQUAL(r,42);

        {
            typename coro::future<int>::promise p;
            auto f = co_test2( p, 12,std::this_thread::get_id(), true);
            p(12);
            f.wait();
        }

        {
            typename coro::future<int>::promise p;
            auto f = co_test2( p, 34,std::this_thread::get_id(), false);
            pool >>  p(34);
            f.wait();
        }

        {
            auto id1 = pool.execute(get_id_coro()).get();
            auto id2 = std::this_thread::get_id();
            CHECK_NOT_EQUAL(id1,id2);
        }

        {
            coro::future<int> f;
            auto p = f.get_promise();
            auto t1 = std::chrono::system_clock::now();
            pool.schedule_after(std::chrono::milliseconds(100), [&]{
                p(42);
            });
            CHECK_EQUAL(f.get(), 42);
            auto t2 = std::chrono::system_clock::now();
            CHECK_GREATER_EQUAL(std::chrono::duration_cast<std::chrono::milliseconds>(t2-t1).count(), 100);
        }
        {
            coro::future<void> z = cancel_coro(pool, &pool);
            pool.cancel(&pool);
            CHECK_EXCEPTION(coro::broken_promise_exception, z.get());
        }
        {
            ff << [&]{return cancel_coro(pool, nullptr).start();};
        }
    }
    CHECK_EXCEPTION(coro::broken_promise_exception, ff.get());
}
