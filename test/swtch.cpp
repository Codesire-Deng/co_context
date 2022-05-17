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

int count = 0;

task<> f(const swtch &to) {
    while (++count < 2e9) [[likely]] co_await to;
}

int main() {
    swtch to[2];

    task<> tasks[2] = {f(to[0]), f(to[1])};

    to[0].target = tasks[1].get_handle();
    to[1].target = tasks[0].get_handle();

    hostTiming([&] { tasks[0].get_handle().resume(); });

    return 0;
}