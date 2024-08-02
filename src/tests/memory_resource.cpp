#include "../coro/coro_single.h"
#include "check.h"
#include <memory_resource>

using shared_pmr = coro::pmr_allocator<std::shared_ptr<std::pmr::memory_resource> >;

coro::async<int,shared_pmr> fibonacci(shared_pmr mr, int n) {
    if (n <= 1) {
        co_return n;
    } else {
        int res = co_await fibonacci(mr, n - 2) + co_await fibonacci(mr, n - 1);
        co_return res;
    }
}


int main() {

    int r = fibonacci(std::make_shared<std::pmr::monotonic_buffer_resource>(), 10);
    CHECK_EQUAL(r, 55);


}
