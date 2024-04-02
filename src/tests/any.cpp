#include "check.h"
#include "../coro/function.h"
#include "../coro/construct.h"

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


int main() {
    {
        coro::any x;
        CHECK(x.empty());
        coro::any<> y = TestSubject();
        CHECK(!y.empty());
        CHECK(y.get_ptr<TestSubject>() != nullptr);
        CHECK(y.get_ptr<int>() == nullptr);
        x = std::move(y);
        CHECK_EXCEPTION(std::bad_cast, {x.get<int>();});
        y = 123;
        CHECK(y.contains<int>());
        CHECK(x.contains<TestSubject>());
        CHECK(!x.contains<int>());
        CHECK(!y.contains<TestSubject>());
        CHECK_EQUAL(y.get<int>(),123);

        std::optional<coro::any<>> z;
        z.emplace(coro::construct_using([](){return coro::any<>(1.20);}));

        CHECK(z->contains<double>());
    }

#ifdef _MSC_VER
    CHECK_EQUAL(constructor, 1);
    CHECK_EQUAL(destructor, 5);
    CHECK_EQUAL(move, 4);
#else
    CHECK_EQUAL(constructor, 1);
    CHECK_EQUAL(destructor, 4);
    CHECK_EQUAL(move, 3);
#endif


}
