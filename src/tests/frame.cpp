#include "check.h"
#include "../coro/frame.h"
#include "../coro/async.h"
#include "../coro/scheduler.h"


//Fake coroutine (using frame wrapper)
class TestFrame: public coro::frame<TestFrame> {
public:
    //called when fake coroutine is resumed
    void resume() {
        _called = true;
        _prom();
        set_done();
    }

    //called when fake coroutine is destroyed
    void destroy() {
        _destroyed = true;
    }

    bool _called = false;
    bool _destroyed = false;
    coro::promise<void> _prom;

    //retrieve promise to wait in resumption
    coro::future<void> completion() {
        return [&](auto promise) {
            _prom = std::move(promise);
        };
    }
};


coro::future<void> test_coro(coro::scheduler &sch) {
    co_await sch.sleep_for(std::chrono::milliseconds(100));
}


int main() {
    coro::scheduler sch;
    TestFrame tframe;

    auto fut = test_coro(sch);
    tframe.await(fut);
    //run scheduler, until tframe is completed
    sch.run(tframe.completion());
    bool done =tframe.get_handle().done();
    tframe.get_handle().destroy();


    CHECK(tframe._called);
    CHECK(tframe._destroyed);
    CHECK(done);

    return 0;
}



