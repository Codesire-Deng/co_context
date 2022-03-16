#include <mimalloc-new-delete.h>
#include "co_context.hpp"
#include <thread>

#include <string.h>
#include <unistd.h>

#include "co_context/net/inet_address.hpp"
#include "co_context/net/acceptor.hpp"
#include "co_context/net/socket.hpp"
#include "co_context/task.hpp"

#include <string_view>

int write_n(int fd, const void *buf, int length) {
    int written = 0;
    while (written < length) {
        int nw = ::write(
            fd, static_cast<const char *>(buf) + written, length - written);
        if (nw > 0) {
            written += nw;
        } else if (nw == 0) {
            break; // EOF
        } else if (errno != EINTR) {
            perror("write");
            break;
        }
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

co_context::main_task run(co_context::socket peer) {
    using namespace co_context;
    // Caution: a bad example for closing connection
    char buf[8192];
    int nr = 0;
    int cnt = 0;
    while ((nr = co_await peer.recv(buf, 0)) > 0) {
        int nw = write_n(STDOUT_FILENO, buf, nr);
        if (nw < nr) break;
    }
    ::exit(0);
}

co_context::main_task server(uint16_t port) {
    using namespace co_context;
    acceptor ac{inet_address{port}};
    for (int sockfd; (sockfd = co_await ac.accept()) >= 0;) {
        co_spawn(run(co_context::socket{sockfd}));
    }
}

co_context::main_task client(std::string_view hostname, uint16_t port) {
    using namespace co_context;
    inet_address addr;
    if (inet_address::resolve(hostname, port, addr)) {
        co_context::socket sock{co_context::socket::create_tcp(addr.family())};
        co_await sock.connect(addr);
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
