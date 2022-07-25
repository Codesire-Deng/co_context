// #include <mimalloc-new-delete.h>
#include "co_context/io_context.hpp"
#include "co_context/net/acceptor.hpp"
#include "co_context/utility/buffer.hpp"
#include "co_context/lazy_io.hpp"
#include <filesystem>
#include <random>
#include <fcntl.h>
#include <atomic>
#include <chrono>

using namespace co_context;

int file_fd;
size_t file_size;
int times;
std::atomic_int alive, finish;

constexpr size_t BLOCK_LEN = 4096;
constexpr int MAX_ON_FLY = 12; // 3 worker thread
// constexpr int MAX_ON_FLY = 2; // 1 worker thread
char zero[BLOCK_LEN];
char buf[BLOCK_LEN];
std::mt19937_64 rng(0);

void gen(char (&buf)[BLOCK_LEN]) {
    static std::mt19937 rng(0);
    for (int i = 0; i < BLOCK_LEN - 1; ++i) {
        buf[i] = rng() % 26 + 'a';
    }
    buf[BLOCK_LEN - 1] = '\n';
}

task<> run() {
    log::d("r4kw at ?? run()\n");
    const uint32_t tid = co_get_tid();
    const int32_t pid = ::gettid();
    log::d("r4kw at [%u](%d) run()\n", tid, pid);

    const size_t idx = alive.fetch_add(1);
    log::d("r4kw at [%u] write()\n", tid);
    const size_t off = (rng() % file_size) & ~(BLOCK_LEN - 1);
    // const size_t off = (idx % file_size) & ~(BLOCK_LEN - 1);
    int nw = co_await lazy::write(file_fd, buf, off);
    // log::d("r4kw at [%u] fsync()\n", tid);
    // int res = co_await lazy::fsync(file_fd, IORING_FSYNC_DATASYNC);
    if (nw < 0) {
        perror("write err");
    }

    int now = finish.fetch_add(1) + 1;
    if (now == times) [[unlikely]] {
        printf("All done\n");
        co_context_stop();
        ::close(file_fd);
        ::exit(0);
    } else if (idx + 1 < times) [[likely]] {
        log::d("r4kw at [%u](%d) callback\n", tid, pid);
        auto t = run();
        log::d("r4kw at [%u](%d) spawn ready\n", tid, pid);
        co_spawn(std::move(t));
        log::d("r4kw at [%u](%d) spawn end\n", tid, pid);
    }
    log::d("r4kw at [%u](%d) end\n", tid, pid);
}

int main(int argc, char *argv[]) {
    memset(zero, 'x', sizeof(zero));
    gen(buf);

    finish.store(0);
    if (argc < 3) {
        printf("Usage:\n  %s file times [file size]\n", argv[0]);
        return 0;
    }

    file_fd = ::open(argv[1], O_WRONLY);
    if (file_fd < 0) {
        throw std::system_error{errno, std::system_category(), "open"};
    }

    times = atoi(argv[2]);
    // file_size = argc == 4 ? atoll(argv[3]) : 60'000'000;
    file_size = argc == 4 ? atoll(argv[3]) : 1'000'000'000;

    io_context context{32768};

    for (int i = 0; i < std::min(MAX_ON_FLY, times); ++i)
        context.co_spawn(run());

    context.run();

    return 0;
}