#include "../coro/coro_single.h"
#include "check.h"

using my_stack = coro::stackful<500>;

coro::async<int,my_stack> fibonacci(my_stack stack, int n) {
    if (n <= 1) {
        co_return n;
    } else {
        int res = co_await fibonacci(stack, n - 2) + co_await fibonacci(stack, n - 1);
        co_return res;
    }
}


int main() {

    my_stack s;
    int r = fibonacci(s, 10);
    CHECK_EQUAL(r, 55);
    CHECK_EQUAL(s.get_pending_deallocation_count(),0);
    CHECK_EQUAL(s.get_alloc_size(),0);


}
