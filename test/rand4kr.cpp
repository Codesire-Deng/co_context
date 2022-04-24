// #include <mimalloc-new-delete.h>
#include "co_context/io_context.hpp"
#include "co_context/net/acceptor.hpp"
#include "co_context/utility/buffer.hpp"
#include "co_context/lazy_io.hpp"
#include "co_context/eager_io.hpp"
#include <filesystem>
#include <random>
#include <fcntl.h>
#include <atomic>
#include <chrono>
#include <semaphore>

using namespace co_context;

int file_fd;
size_t file_size;
int times;
std::atomic_int finish;
std::atomic_int alive;

constexpr size_t BLOCK_LEN = 4096;
constexpr int MAX_ON_FLY = 16;
std::counting_semaphore<MAX_ON_FLY> sem{MAX_ON_FLY};
char zero[BLOCK_LEN];

void gen(char (&buf)[BLOCK_LEN]) {
    static std::mt19937 rng(0);
    for (int i = 0; i < BLOCK_LEN - 1; ++i) { buf[i] = rng() % 26 + 'a'; }
    buf[BLOCK_LEN - 1] = '\n';
}

task<> run(size_t offset) {
    char buf[BLOCK_LEN];

    auto log = [](std::string_view tag, uint32_t x) {
#ifndef NDEBUG
        printf("%s: %08x", tag.data(), x);
        for (auto c : as_buf(&x)) { printf(" %hhx", c); }
        printf("\n");
#endif
    };

    // int nr = co_await lazy::read(file_fd, buf, offset);
    gen(buf);
    int nw =
        co_await lazy::write(file_fd, buf, finish.load() % 300000 * BLOCK_LEN);
    // int nw = ::write(file_fd, buf, BLOCK_LEN);
    if (nw < 0) { perror("write"); }
    // printf("%d\n", nw);
    // buf[15] = '\0';
    // printf("%s", buf);
    // printf("%lu\n", offset);
    co_await eager::nop();

    int now = finish.fetch_add(1) + 1;
    sem.release();
    // alive.fetch_sub(1);
    // printf("now = %d\n", now);
    if (now == times) {
        printf("All done\n");
        ::close(file_fd);
        ::exit(0);
    }
}

int main(int argc, char *argv[]) {
    memset(zero, 'x', sizeof(zero));

    finish.store(0);
    if (argc < 3) {
        printf("Usage:\n  %s file times [file size]\n", argv[0]);
        return 0;
    }

    file_fd = ::open(argv[1], O_RDWR | O_TRUNC);
    if (file_fd < 0) {
        throw std::system_error{errno, std::system_category(), "open"};
    }

    times = atoi(argv[2]);
    file_size = argc == 4 ? atoll(argv[3]) : 60'000'000;
    // file_size = argc == 4 ? atoll(argv[3]) : 3'000'000'000;

    io_context context{256};

    alive.store(0);
    context.co_spawn([]() -> task<> {
        std::mt19937_64 rng(0);
        for (int i = 0; i < times; ++i) {
            const size_t offset = (rng() % file_size) & ~(BLOCK_LEN - 1);
            // while (alive.load(std::memory_order_relaxed) >= MAX_ON_FLY)
            //     co_await yield();
            // alive.fetch_add(1);
            sem.acquire();
            co_spawn(run(offset));
        }
        co_await eager::nop();
    }());

    context.run();

    return 0;
}