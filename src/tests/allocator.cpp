#include "check.h"
#include "../coro/coro_single.h"

template<typename Alloc>
coro::generator<int, Alloc> fibo(Alloc , int count) {
    int a = 1;
    int b = 1;

    for (int i = 0; i < count; ++i) {
        co_yield a;
        int c = a+b;
        a = b;
        b = c;
    }
}

static_assert(coro::coro_optional_allocator<coro::reusabe_allocator *>);

int main() {
    coro::reusabe_allocator alloc;

    int results[] = {1,1,2,3,5,8,13,21,34,55};

    auto iter = std::begin(results);
    for (int v: fibo(&alloc, 10)) {
        int x = *iter;
        ++iter;
        CHECK_EQUAL(x,v);
    }

    CHECK_GREATER(alloc.get_alloc_size(),0);

}
