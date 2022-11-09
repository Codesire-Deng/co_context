#include "co_context/net.hpp"
#include <string_view>
using namespace std::literals;
using namespace co_context;
using Socket = co_context::socket;

void log_error(int err) {
    switch (err) {
        case ECANCELED:
            log::e("timeout\n");
            break;
        default:
            log::e("%s\n", strerror(err));
            break;
    }
}

task<> session(Socket sock) {
    char buf[8192];
    int nr = co_await timeout(sock.recv(buf), 3s);

    // 不断接收字节流并打印到stdout
    while (nr > 0) {
        co_await lazy::write(STDOUT_FILENO, {buf, (size_t)nr});
        nr = co_await timeout(sock.recv(buf), 3s);
    }

    if (nr < 0) {
        log_error(-nr);
    }
}

task<> server(uint16_t port) {
    acceptor ac{inet_address{port}};
    // 限时2s，只接受一个 client
    int sockfd = co_await timeout(ac.accept(), 2s);
    if (sockfd >= 0) {
        co_spawn(session(Socket{sockfd}));
    } else {
        log_error(-sockfd);
    }
}

task<> client(std::string_view hostname, uint16_t port) {
    inet_address addr;
    if (inet_address::resolve(hostname, port, addr)) {
        Socket sock{Socket::create_tcp(addr.family())};
        // 连接一个 server
        int res = co_await sock.connect(addr);
        if (res < 0) {
            printf("res=%d: %s\n", res, strerror(-res));
        }
        // 生成一个 worker 协程
        co_spawn(session(std::move(sock)));
    } else {
        printf("Unable to resolve %s\n", hostname.data());
    }
}

int main(int argc, const char *argv[]) {
    if (argc < 3) {
        printf("Usage:\n  %s hostname port\n  %s -l port\n", argv[0], argv[0]);
        return 0;
    }

    io_context context;

    int port = atoi(argv[2]);
    if (strcmp(argv[1], "-l") == 0) {
        context.co_spawn(server(port));
    } else {
        context.co_spawn(client(argv[1], port));
    }

    context.start();
    context.join();

    return 0;
}
