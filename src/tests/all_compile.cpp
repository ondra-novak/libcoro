
#include "../coro/future.h"
#include "../coro/async.h"
#include "../coro/generator.h"
#include "../coro/queue.h"
#include "../coro/aggregator.h"
#include "../coro/mutex.h"
#include "../coro/thread_pool.h"
#include "../coro/distributor.h"
#include "../coro/scheduler.h"
#include "../coro/condition.h"


template class coro::future<int>;
template class coro::future<int &>;
template class coro::future<void>;
template class coro::deferred_future<int>;
template class coro::deferred_future<void>;
template class coro::promise<int>;
template class coro::promise<void>;
template class coro::shared_future<int>;
template class coro::shared_future<void>;
template class coro::async<int>;
template class coro::generator<int>;
template class coro::generator<int &>;
template class coro::queue<int>;
template class coro::distributor<int>;


struct X {
coro::async<int, coro::ReusableAllocator> test(coro::ReusableAllocator &) {
    co_return 42;
}
};

int main() {
    coro::ReusableAllocator a;
    X x;
    coro::shared_future<int> val = x.test(a);
    val.get();

}
