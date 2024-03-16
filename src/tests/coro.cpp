#include "../coro/coro.h"
#include "../coro/future.h"
#include "check.h"

#include <thread>
coro::deferred_future<double> evaluate_async(int val) {
    return [val](auto promise){
        std::thread thr([val, promise = std::move(promise)]() mutable {
            double res = 1;
            for (int i = 2; i <= val; ++i) {
                res = res * i;
            }
            promise(res);
        });
        thr.detach();
    };
}


coro::coro test_coro(int val, coro::promise<double> result) {
    double r = co_await evaluate_async(val);
    result(r);
}


int main() {

    coro::future<double> f;
    test_coro(20, f.get_promise());
    double res = f;
    CHECK_EQUAL(res, 2432902008176640000.0);
    test_coro(10, f.get_promise());
    res = f;
    CHECK_EQUAL(res, 3628800.0);


}
