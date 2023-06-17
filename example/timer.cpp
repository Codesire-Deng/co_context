#include <co_context/io_context.hpp>
#include <co_context/lazy_io.hpp>
using namespace co_context;

task<> cycle(int sec, const char *message) {
    while (true) {
        co_await timeout(std::chrono::seconds{sec});
        printf("%s\n", message);
    }
}

task<> cycle_abs(int sec, const char *message) {
    auto next = std::chrono::steady_clock::now();
    while (true) {
        next = next + std::chrono::seconds{sec};
        co_await timeout_at(next);
        printf("%s\n", message);
    }
}

int main() {
    io_context ctx;
    ctx.co_spawn(cycle(1, "1 sec"));
    ctx.co_spawn(cycle_abs(1, "1 sec [abs]"));
    ctx.co_spawn(cycle(3, "\t3 sec"));
    ctx.start();
    ctx.join();
    return 0;
}
