#include "../coro/coro_single.h"

template class coro::queue<int>;
template class coro::aggregator<int>;

union test_union {
    coro::target<int> t1;
    coro::target<double> t2;
    coro::target<std::unique_ptr<int> > t3;
};

int main() {

}
