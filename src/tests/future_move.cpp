#include "check.h"

#include "../coro/coroutine.h"
#include "../coro/future.h"
#include "../coro/async.h"
#include <optional>

static int constructor = 0;
static int move = 0;
static int destructor = 0;

class TestSubject {
public:
    TestSubject() {constructor++;}
    ~TestSubject() {destructor++;}
    TestSubject(TestSubject &&) {move++;}
};

coro::future<TestSubject> foo() {
    return [&](auto promise) {
        promise();
    };
}

class Unmovable {
public:
    Unmovable() = default;
    ~Unmovable() {_valid = false;}
    Unmovable(const Unmovable &) = default;
    Unmovable &operator=(const Unmovable &) = default;
    bool valid() const {return _valid;}
    bool _valid = true;

};

coro::future<Unmovable> baz() {
    co_return []{return Unmovable();};
}

coro::coroutine bar() {
    auto f = baz();
    Unmovable &&s = co_await f;
    CHECK(s.valid());
}

coro::coroutine foo2() {
    auto f = foo();
    [[maybe_unused]] TestSubject s = std::move(co_await f);

}






int main() {
    {
        TestSubject z = foo();

        auto f = foo();
        [[maybe_unused]] TestSubject &&x = f;

        bar();

        coro::future<std::string_view> s1("hello");
        std::string_view d = s1;
        CHECK_EQUAL(d,"hello");

        std::string_view d2 = (coro::future<std::string_view> ("hello"));
        CHECK_EQUAL(d2,"hello");

        auto fum = baz();
        Unmovable &&um = fum;
        CHECK(um.valid());

    }

    CHECK_EQUAL(constructor, 2);
    CHECK_EQUAL(destructor, 3);
    CHECK_EQUAL(move, 1);



}
