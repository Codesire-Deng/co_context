#include "co_context/all.hpp"
#include <iostream>
#include <variant>
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
    auto a = co_await f1();
    std::cout << "a = " << a << std::endl;

    // co_spawn f2 & f3, and wait for them
    // concurrency at thread-unsafe mode
    auto [b] = co_await all<unsafe>(f2(), f3());
    std::cout << "b = " << b << std::endl;

    // co_spawn 3 timers, and wait for them (never stop)
    // thread-safe Guaranteed
    co_await all(cycle(1, "1 sec"), cycle(3, "\t3 sec"), cycle(5, "\t\t5 sec"));
}

int main() {
    io_context ctx;
    ctx.co_spawn(run());
    ctx.start();
    ctx.join();
    return 0;
}