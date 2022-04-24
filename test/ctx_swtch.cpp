#include "co_context/io_context.hpp"
#include "co_context/utility/timing.hpp"
#include "co_context/lazy_io.hpp"

using namespace co_context;

int count = 0;

task<> run() {
    auto start = std::chrono::steady_clock::now();
    while (++count < int(5e8)) [[likely]] co_await lazy::yield();
    if (count == (int)5e8) [[likely]] {
        auto end = std::chrono::steady_clock::now();
        std::chrono::duration<double, std::micro> duration = end - start;
        printf("Host Time = %7.3f us.\n", duration.count());
    }
}

int main() {
    io_context ctx{128};

    for (int i = 0; i < config::swap_capacity>>1; ++i) ctx.co_spawn(run());

    ctx.run();

    return 0;
}