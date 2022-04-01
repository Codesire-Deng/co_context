// #include <mimalloc-new-delete.h>
#include "co_context.hpp"
#include <filesystem>
#include <random>
#include <fcntl.h>
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>
#include <ctime>

int file_fd;
size_t file_size;
int times;
volatile std::atomic_int remain, buf_idx = 0;

constexpr size_t BLOCK_LEN = 4096;
constexpr unsigned threads = 4;

alignas(512) char buf[threads][BLOCK_LEN];

std::mt19937_64 rng(time(nullptr));

void run(const uint32_t idx) {
restart: 
    const size_t off = (rng() % file_size) & ~(BLOCK_LEN - 1);
    (void)buf_idx.fetch_add(1);
    int nr = ::pread(file_fd, buf[idx], BLOCK_LEN, off);
    if (nr < 0) { perror("read err"); }

    int ref = remain.fetch_sub(1);
    if (ref <= 0) [[unlikely]] {
        if (ref == 0) {
            printf("All done\n");
            ::close(file_fd);
            ::exit(0);
        }
        return;
    } else [[likely]] {
        goto restart;
    }
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        printf("Usage:\n  %s file times [file size]\n", argv[0]);
        return 0;
    }

    file_fd = ::open(argv[1], O_RDONLY, O_DIRECT);
    if (file_fd < 0) {
        throw std::system_error{errno, std::system_category(), "open"};
    }

    times = atoi(argv[2]);
    // file_size = argc == 4 ? atoll(argv[3]) : 60'000'000;
    file_size = argc == 4 ? atoll(argv[3]) : 15'000'000'000ULL;

    volatile co_context::io_context context{256};

    const int concur = std::min<int>(threads, times);
    remain.store(times - concur);

    std::vector<std::thread> ths;

    for (int i = 0; i < concur; ++i) { ths.emplace_back(run, i); }

    for (int i = 0; i < concur; ++i) { ths[i].join(); }
  
    return 0;
}