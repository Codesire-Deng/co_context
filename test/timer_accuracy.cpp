#include "co_context/io_context.hpp"
#include "co_context/lazy_io.hpp"
#include <chrono>
using namespace co_context;

task<> cycle_abs(int sec) {
    auto next = std::chrono::steady_clock::now();
    while (true) {
        next = next + std::chrono::seconds{sec};
        co_await timeout_at(next);
        auto late = std::chrono::steady_clock::now() - next;
        printf("late = %ld ns\n", late.count());
    }
}

int main() {
    io_context ctx;
    ctx.co_spawn(cycle_abs(1));
    ctx.start();
    ctx.join();
    return 0;
}
