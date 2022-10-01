#include "co_context/io_context.hpp"
#include "co_context/utility/timing.hpp"

using namespace co_context;

struct swtch {
    std::coroutine_handle<> target{std::noop_coroutine()};

    constexpr bool await_ready() const noexcept { return false; }

    std::coroutine_handle<>
    await_suspend(std::coroutine_handle<>) const noexcept {
        return target;
    }

    constexpr void await_resume() const noexcept {}
};

constexpr uint32_t total_switch = 2e9;

uint32_t count = 0;

task<> f(const swtch &to) {
    while (count++ < total_switch)
        [[likely]] co_await to;
}

bool g() {
    return count++ < total_switch;
}

int main() {
    swtch to[2];

    task<> tasks[2] = {f(to[0]), f(to[1])};

    to[0].target = tasks[1].get_handle();
    to[1].target = tasks[0].get_handle();

    auto duration = hostTiming([&] { tasks[0].get_handle().resume(); });

    printf(
        "Avg. coro switch time = %3.3f ns.\n",
        duration.count() / total_switch * 1000
    );

    return 0;
}