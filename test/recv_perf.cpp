#include <co_context/net.hpp>
#include <string_view>
using namespace co_context;
using Socket = co_context::socket;

task<> session(Socket sock) {
    alignas(512) char buf[8192];
    int nr = co_await sock.recv(buf);

    while (nr > 0) {
        nr = co_await sock.recv(buf);
    }
}

task<> server(uint16_t port) {
    acceptor ac{inet_address{port}};
    // 不断接受 client，每个连接生成一个 worker 协程
    for (int sockfd; (sockfd = co_await ac.accept()) >= 0;) {
        co_spawn(session(Socket{sockfd}));
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
