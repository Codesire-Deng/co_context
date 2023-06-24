#include <benchmark/benchmark.h>
#include <co_context/io_context.hpp>
#include <co_context/lazy_io.hpp>
#include <co_context/utility/timing.hpp>

using namespace co_context;

constexpr uint32_t total_switch = 2e8;
uint32_t count;

std::chrono::steady_clock::time_point start;

task<> first() {
    start = std::chrono::steady_clock::now();
    count = 0;
    co_return;
}

task<> run() {
    while (++count < total_switch) {
        co_await lazy::yield();
    }

    if (count == total_switch) [[unlikely]] {
        auto end = std::chrono::steady_clock::now();
        std::chrono::duration<double, std::micro> duration = end - start;
        printf("Host time = %7.3f us.\n", duration.count());
        printf(
            "Avg. switch time = %3.3f ns.\n",
            duration.count() / total_switch * 1000
        );
        this_io_context().can_stop();
    }
}

void perf_lazy_yield(benchmark::State &state) {
    for (auto _ : state) {
        io_context ctx;
        ctx.co_spawn(first());

        for (int i = 0; i < config::swap_capacity / 2; ++i) {
            ctx.co_spawn(run());
        }

        ctx.start();
        ctx.join();
    }
}

BENCHMARK(perf_lazy_yield);

BENCHMARK_MAIN();
