// #include <mimalloc-new-delete.h>
#include "co_context/io_context.hpp"
#include <thread>

#include <string.h>
#include <unistd.h>

#include "co_context/net/inet_address.hpp"
#include "co_context/net/acceptor.hpp"
#include "co_context/net/socket.hpp"
#include "co_context/task.hpp"

#include <string_view>
#include <chrono>
using namespace std::literals;

co_context::task<> run(co_context::socket peer) {
    printf("run: Running\n");
    using namespace co_context;
    char buf[8192];
    int nr = co_await timeout(peer.recv(buf, 0), 3s);
    // 不断接收字节流
    // while ((nr = ::recv(peer.fd(), buf, 8192, 0)) > 0) {}
    while (nr > 0) {
        co_await lazy::write(STDOUT_FILENO, {buf, (size_t)nr}, 0);
        nr = co_await timeout(peer.recv(buf, 0), 3s);
    }
    printf("nr = %d\n", nr);
    perror(strerror(-nr));
    co_await lazy::yield();
    ::exit(0);
}

co_context::task<> server(uint16_t port) {
    using namespace co_context;
    acceptor ac{inet_address{port}};
    // 限时，只接受一个 client
    int sockfd = co_await timeout(ac.accept(), 2s);
    if (sockfd >= 0) {
        co_spawn(run(co_context::socket{sockfd}));
    } else {
        printf("err = %d", -sockfd);
    }
}

co_context::task<> client(std::string_view hostname, uint16_t port) {
    using namespace co_context;
    inet_address addr;
    if (inet_address::resolve(hostname, port, addr)) {
        co_context::socket sock{co_context::socket::create_tcp(addr.family())};
        // 连接一个 server
        int res = co_await sock.connect(addr);
        if (res < 0) {
            printf("res=%d: %s\n", res, strerror(-res));
            ::exit(0);
        }
        // 生成一个 worker 协程
        co_spawn(run(std::move(sock)));
    } else {
        printf("Unable to resolve %s\n", hostname.data());
        ::exit(0);
    }
}

int main(int argc, const char *argv[]) {
    if (argc < 3) {
        printf("Usage:\n  %s hostname port\n  %s -l port\n", argv[0], argv[0]);
        return 0;
    }

    using namespace co_context;
    io_context context{32};

    int port = atoi(argv[2]);
    if (strcmp(argv[1], "-l") == 0) {
        context.co_spawn(server(port));
    } else {
        context.co_spawn(client(argv[1], port));
    }

    context.run();

    return 0;
}
