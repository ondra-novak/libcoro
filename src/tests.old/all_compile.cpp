#include <../coro.old/coro_single.h>

template class coro::queue<int>;

union test_union {
    coro::target<int> t1;
    coro::target<double> t2;
    coro::target<std::unique_ptr<int> > t3;
};

template class coro::stackful<>;

int main() {

}
