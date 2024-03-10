#include <../coro.old/coro_single.h>

#include <iostream>

using namespace std;

template<typename Alloc>
coro::async<int, Alloc> test_coro(Alloc &) {
    co_return 42;
}


coro::generator<int> gen_test() {
    for (int i = 0; i < 10; i++) {
        co_yield i;
    }
}

coro::future<void> test_gen() {
    coro::aggregator<int> gen({gen_test(), gen_test()});
    auto gen2 (std::move(gen));
    auto res = gen2();
    while (co_await !!res) {
        int v = res;
        std::cout << v << std::endl;
        res = gen2();
    }
}

struct Alloc {
    void *allocate(std::size_t sz);
    static void deallocate(void *ptr, std::size_t sz);
};
static_assert(coro::coro_optional_allocator<void>);
static_assert(coro::coro_optional_allocator<coro::standard_allocator>);

int main() {
    coro::reusabe_allocator reuse;
    auto x = test_coro(reuse);
	coro::lazy_future<int> fut = x;
	auto fut2 = std::move(fut);
	std::cout << fut2.get() << std::endl;
	auto fut3 = test_coro(reuse);

	test_gen().wait();

	return 0;
}
