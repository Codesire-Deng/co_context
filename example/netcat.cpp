#include "co_context/net.hpp"
#include <string_view>
#include <fcntl.h>
using namespace co_context;

int nullfd;

void log_error(int err) {
    switch (err) {
        case ECANCELED:
            log::e("timeout!\n");
            break;
        default:
            log::e("%s\n", strerror(err));
            break;
    }
}

co_context::task<> run(co_context::socket peer) {
    printf("run: Running\n");
    char buf[8192];
    int nr = co_await peer.recv(buf);

    // 不断接收字节流
    while (nr > 0) {
        int nw = co_await lazy::write(nullfd, {buf, (size_t)nr});
        if (nw < 0) log_error(-nw);
        nr = co_await (peer.recv(buf));
        if (nr < 0) log_error(-nr);
    }
    ::exit(0);
}

co_context::task<> server(uint16_t port) {
    using namespace co_context;
    acceptor ac{inet_address{port}};
    // 不断接受 client，每个连接生成一个 worker 协程
    for (int sockfd; (sockfd = co_await ac.accept()) >= 0;) {
        co_spawn(run(co_context::socket{sockfd}));
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

    nullfd = ::open("/dev/null", O_WRONLY);
    assert(nullfd >= 0);

    using namespace co_context;
    io_context context;

    int port = atoi(argv[2]);
    if (strcmp(argv[1], "-l") == 0) {
        context.co_spawn(server(port));
    } else {
        context.co_spawn(client(argv[1], port));
    }

    context.run();

    return 0;
}
