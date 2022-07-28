#include "co_context/net.hpp"
#include <string_view>

// TODO 性能受 swap_capacity 影响明显，需分析原因

int write_n(int fd, const void *buf, int length) {
    int written = 0;
    // while (written < length)
    {
        int nw = ::write(
            fd, static_cast<const char *>(buf) + written, length - written
        );
        // if (nw > 0) {
        // written += nw;
        // } else if (nw == 0) {
        //     break; // EOF
        // } else if (errno != EINTR) {
        //     perror("write");
        //     break;
        // }
    }
    return written;
}

co_context::task<int>
send_all(co_context::socket &sock, std::span<const char> buf) {
    int written = 0;
    while (written < buf.size()) {
        int nw = co_await sock.send(buf, 0);
        if (nw > 0)
            written += nw;
        else
            break;
    }
    co_return written;
}

char buf[8192];

// co_context::task<> run(co_context::socket peer) {
//     using namespace co_context;
//     int nr = 0;

//     peer.eager_recv(buf, 0).detach();

//     co_spawn(run(std::move(peer)));
//     co_await eager::nop();
// }

co_context::task<> run(co_context::socket peer) {
    using namespace co_context;
    // char buf[8192];
    int nr = 0;

    while (true) {
        for (int i = 0; i < 10; ++i)
            peer.eager_recv(buf, 0).detach();
        std::this_thread::sleep_for(std::chrono::microseconds(1));
    }

    peer.eager_close().detach();
    ::exit(0);
}

co_context::task<> server(uint16_t port) {
    using namespace co_context;
    acceptor ac{inet_address{port}};
    // 不断接受 client，每个连接生成一个 worker 协程
    for (int sockfd; (sockfd = co_await ac.eager_accept()) >= 0;) {
        co_spawn(run(co_context::socket{sockfd}));
    }
}

co_context::task<> client(std::string_view hostname, uint16_t port) {
    using namespace co_context;
    inet_address addr;
    if (inet_address::resolve(hostname, port, addr)) {
        co_context::socket sock{co_context::socket::create_tcp(addr.family())};
        // 连接一个 server
        co_await sock.eager_connect(addr);
        // 生成一个 worker 协程
        co_spawn(run(std::move(sock)));
    } else {
        printf("Unable to resolve %s\n", hostname.data());
    }
}

int main(int argc, const char *argv[]) {
    if (argc < 3) {
        printf("Usage:\n  %s hostname port\n  %s -l port\n", argv[0], argv[0]);
        return 0;
    }

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
