#include <array>
#include <chrono>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include "co_context/co/mutex.hpp"
#include <random>
#include "co_context/co/condition_variable.hpp"
#include "co_context/io_context.hpp"
#include "co_context/task.hpp"
#include "co_context/eager_io.hpp"

co_context::condition_variable cv;
co_context::mutex cv_m; // This mutex is used for three purposes:
                        // 1) to synchronize accesses to i
                        // 2) to synchronize accesses to std::cerr
                        // 3) for the condition variable cv
int i = 0;

using namespace co_context;

task<> waits() {
    auto lk = co_await cv_m.lock_guard();
    std::cerr << "Waiting... \n";
    co_await cv.wait(cv_m, [] { return i == 1; });
    std::cerr << "...finished waiting. i == 1\n";
}

task<> signals() {
    std::this_thread::sleep_for(std::chrono::seconds(1));
    {
        auto lk = co_await cv_m.lock_guard();
        std::cerr << "Notifying...\n";
    }
    cv.notify_all();

    std::this_thread::sleep_for(std::chrono::seconds(1));

    {
        auto lk = co_await cv_m.lock_guard();
        i = 1;
        std::cerr << "Notifying again...\n";
    }
    cv.notify_all();
}

int main() {
    io_context ctx;
    ctx.co_spawn(waits());
    ctx.co_spawn(waits());
    ctx.co_spawn(waits());
    ctx.co_spawn(signals());
    ctx.run();

    return 0;
}