#include "../coro/pool_alloc.h"
#include "check.h"

#include <memory>
struct Test {
    char buff[200];
};


using TestP = coro::pool_allocated<Test,128>;

int main() {

    coro::pool_alloc<int> p;
    int *a = p.construct();
    int *b = p.construct();
    CHECK_EQUAL((std::bit_cast<std::uintptr_t>(a) & 0x1F),0);
    *a = 10;
    *b = 20;
    p.destroy(a);
    p.destroy(b);
    int *c = p.construct();
    int *d = p.construct();
    CHECK_EQUAL(c, b);
    CHECK_EQUAL(d, a);
    p.destroy(c);
    p.destroy(d);

    auto inst1 = std::make_unique<TestP>();
    auto ptr1 = inst1.get();
    CHECK_EQUAL((std::bit_cast<std::uintptr_t>(ptr1) & 0x7F),0);
    auto inst2 = std::make_unique<TestP>();
    auto inst3 = std::make_unique<TestP>();
    auto ptr2 = inst2.get();
    inst1.reset();
    inst2.reset();
    auto inst4 = std::make_unique<TestP>();
    auto ptr4 = inst4.get();
    CHECK_EQUAL(ptr4,ptr2);
    auto inst5 = std::make_unique<TestP>();
    auto ptr5 = inst5.get();
    CHECK_EQUAL(ptr5,ptr1);



}
