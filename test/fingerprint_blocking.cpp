#include "co_context/all.hpp"
#include <fcntl.h>
#include <filesystem>

using namespace co_context;

int file_fd;

constexpr size_t BLOCK_LEN = 4096;

// constexpr size_t BLOCK_LEN = 4096;
// constexpr size_t BLOCK_LEN = 64;

task<> run(co_context::socket sock) {
    uint32_t as_uint[BLOCK_LEN / 4];
    uint32_t offset;

    auto log = [](std::string_view tag, uint32_t x) {
#ifndef NDEBUG
        printf("%s: %08x", tag.data(), x);
        for (auto c : as_buf(&x)) {
            printf(" %hhx", c);
        }
        printf("\n");
#endif
    };

    int nr;

    // auto start = std::chrono::steady_clock::now();
    // decltype(start) t_recv, t_send;

    while ((nr = ::recv(sock.fd(), &offset, sizeof(offset), 0)) == 4) {
        log("recv offset", offset);
        // t_recv = std::chrono::steady_clock::now();
        offset &= ~(BLOCK_LEN - 1);
        [[maybe_unused]] int res =
            ::pread(file_fd, (void *)as_uint, BLOCK_LEN, offset);
        uint32_t hashcode = as_uint[0];
        // for (int i = 1; i < BLOCK_LEN / 4; ++i) hashcode ^= as_uint[i];
        log("send hash", hashcode);
        ::send(sock.fd(), &hashcode, sizeof(hashcode), 0);
        // t_send = std::chrono::steady_clock::now();
        // printf("send done\n");
    }

    ::close(sock.fd());
    // printf("close done\n");
    co_await eager::nop();

    // auto end = std::chrono::steady_clock::now();
    // std::chrono::duration<double, std::milli> duration = end - start;
    // printf("Duration = %g ms\n", duration.count());

    // std::chrono::duration<double, std::milli> t_internal = t_send - t_recv;
    // printf("Internal Time = %g ms\n", t_internal.count());

    if (nr != 0) {
        fprintf(stderr, "short recv: %d\n", nr);
    }
}

task<> server(uint16_t port, std::filesystem::path path) {
    file_fd = ::open(path.c_str(), O_RDONLY);
    if (file_fd < 0) {
        throw std::system_error{errno, std::system_category(), "open"};
    }

    acceptor ac{inet_address{port}};

    printf("Accepting... Ctrl-C to exit\n");

    int sockfd;
    while ((sockfd = co_await ac.accept()) >= 0) {
        co_spawn(run(co_context::socket{sockfd}));
    }
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        printf("Usage:\n  %s port FILE\n", argv[0]);
        return 0;
    }

    const int port = atoi(argv[1]);
    io_context context;
    context.co_spawn(server(port, argv[2]));

    context.run();

    return 0;
}