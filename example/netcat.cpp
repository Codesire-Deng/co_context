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

co_context::task<> run(co_context::socket peer) {
    printf("Running\n");
    using namespace co_context;
    char buf[8192];
    int nr = co_await peer.recv(buf, 0);
    // 不断接收字节流
    // while ((nr = ::recv(peer.fd(), buf, 8192, 0)) > 0) {}
    while (nr > 0) {
        nr = co_await (
            lazy::write(STDOUT_FILENO, {buf, (size_t)nr}, 0) + peer.recv(buf, 0)
        );
        // eager::write(STDOUT_FILENO, {buf, (size_t)nr}, 0).detach();

        // int nw = write_n(STDOUT_FILENO, buf, nr); // 将收到的字节全部打印到
        // stdout if (nw < nr) break;
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
        co_await sock.connect(addr);
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
    io_context context{16};

    int port = atoi(argv[2]);
    if (strcmp(argv[1], "-l") == 0) {
        context.co_spawn(server(port));
    } else {
        context.co_spawn(client(argv[1], port));
    }

    context.run();

    return 0;
}
