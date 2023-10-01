#include <co_context/all.hpp>

#include <iostream>
using namespace co_context;

task<int> f0() {
    printf("f0 start.\n");
    co_await timeout(std::chrono::seconds{1});
    printf("f0 done.\n");
    co_return 1;
}

shared_task<std::string> f1() {
    printf("f1 start.\n");
    printf("f1 done.\n");
    co_return "f1 Great!";
}

task<void> f2() {
    printf("f2 start.\n");
    co_await timeout(std::chrono::seconds{2});
    printf("f2 done.\n");
    co_return;
}

task<> run() {
    // co_spawn f1 & f2 & f3, and wait for them
    auto [r0, r1] = co_await all(f0(), f1(), f2());
    std::cout << "get the result of f0: " << r0 << "\n";
    std::cout << "get the result of f1: " << r1 << "\n";
}

int main() {
    io_context ctx;
    ctx.co_spawn(run());
    ctx.start();
    ctx.join();
    return 0;
}
