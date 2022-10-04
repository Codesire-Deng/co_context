#include "co_context/co/mutex.hpp"
#include "co_context/co/semaphore.hpp"
#include "co_context/io_context.hpp"
#include "co_context/lazy_io.hpp"
#include "co_context/task.hpp"
#include <array>
#include <chrono>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <random>

using namespace co_context;
using namespace std::literals;

constexpr std::size_t max_threads{10U};       // change and see the effect
constexpr std::ptrdiff_t max_sema_threads{3}; // {1} for binary semaphore
co_context::counting_semaphore sem{max_sema_threads};
constexpr auto time_tick{10ms};

unsigned rnd() {
    static std::uniform_int_distribution<unsigned> distribution{
        2U, 9U}; // [delays]
    static std::random_device engine;
    static std::mt19937 noise{engine()};
    return distribution(noise);
}

class alignas(128 /*std::hardware_destructive_interference_size*/) Guide {
    inline static co_context::mutex cout_mutex;
    inline static std::chrono::time_point<std::chrono::high_resolution_clock>
        started_at;
    unsigned delay{rnd()}, occupy{rnd()}, wait_on_sema{};

  public:
    static void start_time() {
        started_at = std::chrono::high_resolution_clock::now();
    }

    task<void> initial_delay() { co_await timeout(delay * time_tick); }

    task<void> occupy_sema() {
        wait_on_sema = static_cast<unsigned>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::high_resolution_clock::now() - started_at
                - delay * time_tick
            )
                .count()
            / time_tick.count()
        );
        co_await timeout(occupy * time_tick);
    }

    task<void> visualize(unsigned id, unsigned x_scale = 2) const {
        auto cout_n = [=](auto str, unsigned n) {
            n *= x_scale;
            while (n-- > 0) {
                std::cout << str;
            }
        };
        auto guard = co_await cout_mutex.lock_guard();
        std::cout << "#" << std::setw(2) << id << " ";
        cout_n("░", delay);
        cout_n("▒", wait_on_sema);
        cout_n("█", occupy);
        std::cout << '\n';
    }

    static void show_info() {
        std::cout
            << "\nThreads: " << max_threads
            << ", Throughput: " << max_sema_threads
            << " │ Legend: initial delay ░░ │ wait state ▒▒ │ sema occupation ██ \n"
            << std::endl;
    }
};

std::array<Guide, max_threads> guides;

task<> workerThread(unsigned id) {
    // emulate some work before sema acquisition
    co_await guides[id].initial_delay();
    // wait until a free sema slot is available
    co_await sem.acquire();
    // emulate some work while sema is acquired
    co_await guides[id].occupy_sema();
    sem.release();
    co_await guides[id].visualize(id);
}

int main() {
    io_context ctx;

    for (auto id{0U}; id != max_threads; ++id) {
        ctx.co_spawn(workerThread(id));
    }

    Guide::show_info();
    Guide::start_time();

    ctx.run();
}