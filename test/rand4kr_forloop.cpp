#include "co_context/io_context.hpp"
#include "co_context/lazy_io.hpp"
#include <atomic>
#include <chrono>
#include <ctime>
#include <fcntl.h>
#include <filesystem>
#include <random>

using namespace co_context;

int file_fd;
size_t file_size;
int times;
std::atomic_int remain;

constexpr size_t BLOCK_LEN = 4096;
constexpr unsigned threads = config::workers_number;
constexpr int MAX_ON_FLY = threads * 2;

alignas(512) char buf[MAX_ON_FLY][BLOCK_LEN];

std::mt19937_64 rng(time(nullptr));

task<> run(const uint32_t idx) {
restart:
    const size_t off = (rng() % file_size) & ~(BLOCK_LEN - 1);
    int nr = co_await lazy::read(file_fd, buf[idx], off);
    if (nr < 0) {
        perror("read err");
    }

    int ref = remain.fetch_sub(1);
    if (ref <= 0) [[unlikely]] {
        if (ref == 0) {
            printf("All done\n");
            io_context_stop();
            ::close(file_fd);
            ::exit(0);
        }
    } else [[likely]] {
        goto restart;
    }
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        printf("Usage:\n  %s file times [file size]\n", argv[0]);
        return 0;
    }

    file_fd = ::open(argv[1], O_RDONLY);
    if (file_fd < 0) {
        throw std::system_error{errno, std::system_category(), "open"};
    }

    times = atoi(argv[2]);
    // file_size = argc == 4 ? atoll(argv[3]) : 60'000'000;
    file_size = argc == 4 ? atoll(argv[3]) : 15'000'000'000ULL;

    co_context::io_context context;

    const int concur = std::min<int>(MAX_ON_FLY, times);
    remain.store(times - concur);

    for (int i = 0; i < concur; ++i) {
        context.co_spawn(run(i));
    }

    context.start();
    context.join();

    return 0;
}