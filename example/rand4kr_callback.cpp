// #include <mimalloc-new-delete.h>
#include "co_context.hpp"
#include "co_context/net/acceptor.hpp"
#include "co_context/buffer.hpp"
#include "co_context/lazy_io.hpp"

#include <filesystem>
#include <random>
#include <fcntl.h>
#include <atomic>
#include <chrono>

using namespace co_context;

/**
 * total_threads_number = 5;
 * swap_capacity = 16;
 * MAX_ON_FLY = 24
 * file_size = 1e9 bytes
 */

int file_fd;
size_t file_size;
int times;
std::atomic_int remain, buf_idx = 0;

constexpr size_t BLOCK_LEN = 4096;
// constexpr int MAX_ON_FLY = 24; // 6 worker thread
constexpr int MAX_ON_FLY = 24; // 3 worker thread
// constexpr int MAX_ON_FLY = 4; // 1 worker thread
constexpr unsigned threads = config::worker_threads_number;

alignas(config::cache_line_size) char buf[threads][BLOCK_LEN];

std::mt19937_64 rng(0);

main_task run() {
    // log::d("r4kr at [%u] read()\n", co_get_tid());
    const size_t off = (rng() % file_size) & ~(BLOCK_LEN - 1);
    const int idx = buf_idx.fetch_add(1) % threads;
    int nr = co_await lazy::read(file_fd, buf[idx], off);
    if (nr < 0) { perror("read err"); }

    if (remain.fetch_sub(1) == 0) [[unlikely]] {
        printf("All done\n");
        co_context_stop();
        ::close(file_fd);
        ::exit(0);
    } else [[likely]] {
        // log::d("r4kr at [%u] callback\n", co_get_tid());
        auto t = run();
        // log::d("r4kr at [%u] spawn ready\n", co_get_tid());
        co_spawn(t);
    }
    // log::d("r4kr at [%u] end\n", co_get_tid());
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
    file_size = argc == 4 ? atoll(argv[3]) : 1'000'000'000;

    io_context context{2048};

    int concur = std::min(MAX_ON_FLY, times);
    remain.store(times - concur);
    for (int i = 0; i < concur; ++i) context.co_spawn(run());

    context.run();

    return 0;
}