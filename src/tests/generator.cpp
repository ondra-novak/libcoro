#include "../coro/generator.h"

#include "check.h"

coro::generator<int> fibo(int count) {
    int a = 1;
    int b = 1;

    for (int i = 0; i < count; ++i) {
        co_yield a;
        int c = a+b;
        a = b;
        b = c;
    }

}


int main() {
    for (int v: fibo(10)) {
        std::cout << v << std::endl;
    }
}