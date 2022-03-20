#include <mimalloc-new-delete.h>
#include "co_context.hpp"
#include "co_context/net/acceptor.hpp"
#include "co_context/buffer.hpp"

#include <filesystem>
#include <random>
#include <fcntl.h>
#include <atomic>
#include <chrono>

using namespace co_context;

int file_fd;
size_t file_size;
int times;
std::atomic_int finish;

constexpr size_t BLOCK_LEN = 4096;

main_task run(size_t offset) {
    char buf[BLOCK_LEN];

    auto log = [](std::string_view tag, uint32_t x) {
#ifndef NDEBUG
        printf("%s: %08x", tag.data(), x);
        for (auto c : as_buf(&x)) { printf(" %hhx", c); }
        printf("\n");
#endif
    };

    int nr;
    // nr = co_await lazy::read(file_fd, buf, offset);
    nr = ::pread(file_fd, buf, BLOCK_LEN, offset);
    co_await eager::nop();

    int now = finish.fetch_add(1) + 1;
    // printf("now = %d\n", now);
    if (now == times) {
        printf("All done\n");
        ::exit(0);
    }

}

int main(int argc, char *argv[]) {
    finish.store(0);
    if (argc < 3) {
        printf("Usage:\n  %s file times [file size]\n", argv[0]);
        return 0;
    }

    file_fd = ::open(argv[1], O_RDONLY);
    if (file_fd < 0) {
        throw std::system_error{errno, std::system_category(), "open"};
    }

    times = atoi(argv[2]);
    file_size = argc == 4 ? atoll(argv[3]) : 1'000'000'000;

    io_context context{256};

    context.co_spawn([]() -> main_task {
        std::mt19937_64 rng(0);
        for (int i = 0; i < times; ++i) {
            const size_t offset = (rng() % file_size) & ~(BLOCK_LEN - 1);
            co_spawn(run(offset));
        }
        co_await eager::nop();
    }()); 

    context.run();

    return 0;
}