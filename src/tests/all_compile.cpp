
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
#include "../coro/collector.h"
#include "../coro/future_variant.h"


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
template class coro::collector<long, int>;
template class coro::queue<int>;
template class coro::distributor<int>;
template class coro::function<int(int) noexcept>;
template class coro::function<int(int)>;
template class coro::future_variant<int, double, void, std::string, char *, std::nullptr_t>;

static_assert(coro::function<int(int) noexcept>::is_noexcept);
static_assert(!coro::function<int(int)>::is_noexcept);


struct X {
coro::async<int, coro::reusable_allocator> test(coro::reusable_allocator &) {
    co_return 42;
}
};

int main() {
    coro::reusable_allocator a;
    X x;
    coro::shared_future<int> val = x.test(a);
    val.get();

}
