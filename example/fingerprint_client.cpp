#include <mimalloc-new-delete.h>
#include "co_context.hpp"
#include "co_context/net/acceptor.hpp"
#include "co_context/buffer.hpp"

#include <string_view>
#include <filesystem>
#include <fcntl.h>
#include <random>
#include <atomic>
#include <chrono>

using namespace co_context;

size_t file_size;

constexpr size_t BLOCK_LEN = 4096;
constexpr int query = 1;
int concurrency;
std::atomic_int finish;

main_task run(co_context::socket sock, const uint32_t offset) {
    uint32_t hashcode;

    auto log = [](std::string_view tag, uint32_t x) {
#ifndef NDEBUG
        printf("%s: %08x", tag.data(), x);
        for (auto c : as_buf(&x)) { printf(" %hhx", c); }
        printf("\n");
#endif
    };

    auto start = std::chrono::steady_clock::now();

    for (int i = 0; i < query; ++i) {
        log("send", offset);
        co_await sock.send(as_buf(&offset), 0);
        co_await sock.recv(as_buf(&hashcode), 0);
        log("recv", hashcode);
    }

    auto end = std::chrono::steady_clock::now();
    std::chrono::duration<double, std::milli> duration = end - start;
    // printf("Time = %g ms\n", duration.count());

    co_await sock.close();
    if (finish.fetch_add(1) + 1 == concurrency) {
        printf("done\n");
        ::exit(0);
    }
}

main_task client(std::string_view hostname, uint16_t port, int count) {
    std::mt19937 rng{0};
    finish.store(0);
    inet_address addr;
    if (inet_address::resolve(hostname, port, addr)) {
        while (count--) {
            // printf("connecting...\n");
            co_context::socket sock{
                co_context::socket::create_tcp(addr.family())};
            // 连接一个 server
            co_await sock.connect(addr);
            // 生成一个 worker 协程
            const size_t offset = (rng() % file_size) & ~(BLOCK_LEN - 1);
            co_spawn(run(std::move(sock), offset));
        }
    } else {
        printf("Unable to resolve %s\n", hostname.data());
    }
}

int main(int argc, char *argv[]) {
    if (argc < 4) {
        printf("Usage:\n  %s hostname port concurrency [file size]\n", argv[0]);
        return 0;
    }

    io_context context{128};

    const int port = atoi(argv[2]);
    concurrency = atoi(argv[3]);
    file_size = argc == 5 ? atoll(argv[4]) : 60'000'000;
    context.co_spawn(client(argv[1], port, concurrency));

    context.run();

    return 0;
}