#include "co_context/all.hpp"
#include <iostream>
using namespace co_context;

task<int> f1() {
    co_return 1;
}

task<const char *> f2() {
    co_return "f2 Great!";
}

task<void> f3() {
    printf("f3 ok!\n");
    co_return;
}

task<> cycle(int sec, const char *message) {
    while (true) {
        co_await timeout(std::chrono::seconds{sec});
        printf("%s\n", message);
    }
}

task<> run() {
    // co_spawn f1 & f2 & f3, and wait for them
    auto [a, b] = co_await all(f1(), f2(), f3());
    std::cout << "a = " << a << std::endl;
    std::cout << "b = " << b << std::endl;
}

int main() {
    io_context ctx;
    ctx.co_spawn(run());
    ctx.start();
    ctx.join();
    return 0;
}