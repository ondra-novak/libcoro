#include "check.h"
#include "../coro/future_variant.h"

struct TestObject {
    static int created;
    static int destroyed;

    TestObject() {created++;};
    ~TestObject() {destroyed++;}
    TestObject(const TestObject &) = delete;
    static void reset() {
        created = destroyed = 0;
    }
};

int TestObject::created = 0;
int TestObject::destroyed = 0;

void test_destroy() {
    TestObject::reset();
    coro::future_variant<TestObject> f;
    coro::promise<TestObject> p;
    auto &fut = f.get_promise(p);
    CHECK(coro::holds_alternative<coro::future<TestObject> >(f));
    CHECK(fut.is_pending());
    p();
    CHECK(!fut.is_pending());
    f.reset();
    CHECK_EQUAL(TestObject::created,1);
    CHECK_EQUAL(TestObject::destroyed,1);
}

void test_swap() {
    TestObject::reset();
    {
        coro::future_variant<TestObject, int> f;
        coro::promise<TestObject> p;
        auto &fut = f.get_promise(p);
        p();
        CHECK(!fut.is_pending());
        auto &fut2 = f << [&]{return coro::future<int>(42);};
        int v = coro::get<coro::future<int> >(f).get();;
        CHECK_EQUAL(v, 42);
        int w = fut2.get();
        CHECK_EQUAL(w, 42);
        auto &fut3 = f << [&]{return coro::future<TestObject>(std::in_place);};
        CHECK(!fut3.is_pending());
    }
    CHECK_EQUAL(TestObject::created,2);
    CHECK_EQUAL(TestObject::destroyed,2);
}


int main() {
    test_destroy();
    test_swap();
    return 0;
}