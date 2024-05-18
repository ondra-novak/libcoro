#include "check.h"

#include "../coro/coroutine.h"
#include "../coro/future.h"
#include "../coro/async.h"
#include <optional>


class TestSubject {
public:

    static int constructor;
    static int move ;
    static int destructor;
    static int copy;

    TestSubject() {constructor++;}
    ~TestSubject() {destructor++;}
    TestSubject(TestSubject &&) {move++;}
    TestSubject(const TestSubject &) {copy++;}

    static void reset() {
        constructor = move = destructor = copy = 0;
    }
};

int TestSubject::constructor = 0;
int TestSubject::move = 0;
int TestSubject::destructor = 0;
int TestSubject::copy = 0;


int test1() {


    TestSubject::reset();

    {
        coro::future<TestSubject> fut;
        fut.get_promise()();

        coro::future<TestSubject> fut_copy;
        fut.forward_to(fut_copy.get_promise());


        coro::future<TestSubject> fut_move;
        std::move(fut).forward_to(fut_move.get_promise());
    }

    CHECK_EQUAL(TestSubject::constructor, 1);
    CHECK_EQUAL(TestSubject::destructor, 3);
    CHECK_EQUAL(TestSubject::copy,1);
    CHECK_EQUAL(TestSubject::move,1);

    return 0;
}

int test2() {

    TestSubject::reset();

    {
        coro::promise<TestSubject> prom;

        coro::deferred_future<TestSubject> fut([&](auto promise){
            prom = std::move(promise);
        });

        CHECK(fut.is_deferred());
        CHECK(fut.is_pending());


        coro::future<TestSubject> fut_res(fut);

        CHECK(!fut.is_deferred());
        CHECK(!fut.is_pending());
        CHECK(fut_res.is_pending());
        CHECK(!fut_res.is_deferred());

        prom();

        CHECK(!fut_res.is_pending());
        CHECK(!fut_res.is_deferred());

        fut_res.await_resume();


    }

        CHECK_EQUAL(TestSubject::constructor, 1);
        CHECK_EQUAL(TestSubject::destructor, 1);
        CHECK_EQUAL(TestSubject::copy,0);
        CHECK_EQUAL(TestSubject::move,0);



    return 0;
}

int test3() {

    TestSubject::reset();
    {
        coro::promise<TestSubject> prom;

        coro::deferred_future<TestSubject> fut([&](auto promise){
            prom = std::move(promise);
        });

        CHECK(fut.is_deferred());
        CHECK(fut.is_pending());
        fut.start();
        CHECK(!fut.is_deferred());
        CHECK(fut.is_pending());
        prom();
        CHECK(!fut.is_deferred());
        CHECK(!fut.is_pending());

        coro::future<TestSubject> fut_res(fut);

        CHECK(!fut_res.is_pending());
        CHECK(!fut_res.is_deferred());


        coro::future<TestSubject> fut_move(std::move(fut));

    }
    CHECK_EQUAL(TestSubject::constructor, 1);
    CHECK_EQUAL(TestSubject::destructor, 3);
    CHECK_EQUAL(TestSubject::copy,1);
    CHECK_EQUAL(TestSubject::move,1);
    return 0;
}

int test4() {
    coro::future<double> f1(12.3456789);
    coro::future<int> f2;
    f1.convert_to(f2.get_promise(), [](double &v){return static_cast<int>(v*100);});
    CHECK_EQUAL(f2.await_resume(), 1234);
    int void_test_val = 0;
    coro::future<void> f3;
    f1.convert_to(f3.get_promise(), [&](double v){void_test_val = static_cast<int>(v*100);});
    CHECK_EQUAL(void_test_val,1234);
    return 0;
}


int main() {
    return test1()+test2()+test3()+test4();
}

