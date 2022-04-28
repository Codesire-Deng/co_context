#include "co_context/io_context.hpp"
#include "co_context/utility/timing.hpp"
#include "co_context/lazy_io.hpp"

using namespace co_context;

constexpr uint32_t total_switch = 1e8;
int count = 0;

task<> run() {
    auto start = std::chrono::steady_clock::now();

    while (++count < total_switch) [[likely]] co_await lazy::yield();

    if (count == total_switch) [[unlikely]] {
        auto end = std::chrono::steady_clock::now();
        std::chrono::duration<double, std::micro> duration = end - start;
        printf("Host time = %7.3f us.\n", duration.count());
        printf(
            "Avg. switch time = %3.3f ns.\n",
            duration.count() / total_switch * 1000
        );
        co_context_stop();
    }
}

int main() {
    io_context ctx{128};

    for (int i = 0; i < config::swap_capacity / 2; ++i) ctx.co_spawn(run());

    ctx.run();

    return 0;
}