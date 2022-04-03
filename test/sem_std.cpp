#include <array>
#include <chrono>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <random>
#include <semaphore>
#include <thread>
#include <vector>
using namespace std::literals;

constexpr std::size_t max_threads{10U};       // change and see the effect
constexpr std::ptrdiff_t max_sema_threads{3}; // {1} for binary semaphore
std::counting_semaphore sem{max_sema_threads};
constexpr auto time_tick{10ms};

unsigned rnd() {
    static std::uniform_int_distribution<unsigned> distribution{
        2U, 9U}; // [delays]
    static std::random_device engine;
    static std::mt19937 noise{engine()};
    return distribution(noise);
}

class alignas(128 /*std::hardware_destructive_interference_size*/) Guide {
    inline static std::mutex cout_mutex;
    inline static std::chrono::time_point<std::chrono::high_resolution_clock>
        started_at;
    unsigned delay{rnd()}, occupy{rnd()}, wait_on_sema{};

  public:
    static void start_time() {
        started_at = std::chrono::high_resolution_clock::now();
    }

    void initial_delay() { std::this_thread::sleep_for(delay * time_tick); }

    void occupy_sema() {
        wait_on_sema = static_cast<unsigned>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::high_resolution_clock::now() - started_at
                - delay * time_tick)
                .count()
            / time_tick.count());
        std::this_thread::sleep_for(occupy * time_tick);
    }

    void visualize(unsigned id, unsigned x_scale = 2) const {
        auto cout_n = [=](auto str, unsigned n) {
            n *= x_scale;
            while (n-- > 0) { std::cout << str; }
        };
        std::lock_guard lk{cout_mutex};
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

void workerThread(unsigned id) {
    guides[id].initial_delay(); // emulate some work before sema acquisition
    sem.acquire();        // wait until a free sema slot is available
    guides[id].occupy_sema();   // emulate some work while sema is acquired
    sem.release();
    guides[id].visualize(id);
}

int main() {
    std::vector<std::jthread> threads;
    threads.reserve(max_threads);

    Guide::show_info();
    Guide::start_time();

    for (auto id{0U}; id != max_threads; ++id) {
        threads.push_back(std::jthread(workerThread, id));
    }
}