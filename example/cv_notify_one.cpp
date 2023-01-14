#include "co_context/co/mutex.hpp"
#include "co_context/co/condition_variable.hpp"
#include "co_context/io_context.hpp"
#include "co_context/task.hpp"
#include <array>
#include <chrono>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <random>

using namespace co_context;
using namespace std::literals;

co_context::mutex m;
co_context::condition_variable cv;
std::string data;
bool ready = false;
bool processed = false;

task<void> worker_thread() {
    // Wait until main() sends data
    co_await m.lock();
    co_await cv.wait(m, [] { return ready; });

    // after the wait, we own the lock.
    std::cout << "Worker thread is processing data\n";
    data += " after processing";

    // Send data back to main()
    processed = true;
    std::cout << "Worker thread signals data processing completed\n";

    // Manual unlocking is done before notifying, to avoid waking up
    // the waiting thread only to block again (see notify_one for details)
    m.unlock();
    cv.notify_one();
}

task<> main_thread() {
    data = "Example data";
    // send data to the worker thread
    {
        co_await m.lock();
        ready = true;
        std::cout << "main() signals data ready for processing\n";
    }
    cv.notify_one(); // fake notify

    char s[4];
    printf("input a char to continue:");
    [[maybe_unused]] int _ = scanf("%s", s);
    m.unlock();
    cv.notify_one(); // real notify

    // wait for the worker
    {
        auto lk = co_await m.lock_guard();
        co_await cv.wait(m, [] { return processed; });
    }
    std::cout << "Back in main(), data = " << data << '\n';
}

int main() {
    io_context ctx;
    ctx.co_spawn(worker_thread());
    ctx.co_spawn(main_thread());

    ctx.start();
    ctx.join();
    return 0;
}
