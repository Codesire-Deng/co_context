#include "co_context/all.hpp"
#include <iostream>
#include <tuple>
#include <type_traits>
using namespace co_context;

task<int> f1() {
    co_return 1;
}

task<const char *> f2() {
    co_return "NB!";
}

task<void> f3() {
    printf("ok\n");
    co_return;
}

task<> run() {
    auto a = co_await f1();
    std::cout << "a = " << a << std::endl;
    
    auto [b] = co_await all(f2(), f3());
    std::cout << "b = " << b << std::endl;
}

int main() {
    io_context ctx;
    ctx.co_spawn(run());
    ctx.start();
    ctx.join();
    return 0;
}