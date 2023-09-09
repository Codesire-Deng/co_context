#include <co_context/co/when_all.hpp>
#include <co_context/lazy_io.hpp>
#include <co_context/shared_task.hpp>
#include <co_context/utility/mpl.hpp>
#include <iostream>

using namespace co_context;

shared_task<std::string> fan_out() {
    std::cout << "fan_out(): start\n";
    co_await timeout(std::chrono::seconds{1});
    std::cout << "fan_out(): done\n";
    co_return "string from fan_out()";
}

task<size_t> mapped_task(shared_task<std::string> dependency) {
    auto result = co_await dependency;
    std::cout << "post_task(): " << result << "\n";
    co_return result.size();
}

template<typename T>
task<void> reduce_task(task<T> all_task) {
    auto results = co_await all_task;
    constexpr size_t n = std::tuple_size_v<T>;
    size_t sum_size = 0;
    mpl::static_for<0, n>([&](auto i) { sum_size += std::get<i>(results); });
    std::cout << "reduce_task(): total bytes: " << sum_size << "\n";
}

int main() {
    io_context ctx;
    {
        auto f = fan_out();
        auto maps = all(mapped_task(f), mapped_task(f), mapped_task(f));
        auto reduce = reduce_task(std::move(maps));
        ctx.co_spawn(std::move(reduce));
    }
    ctx.start();
    ctx.join();
    return 0;
}
