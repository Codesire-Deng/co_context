#include "uring/uring.hpp"
#include "co_context/net/acceptor.hpp"

using namespace std;
using namespace liburingcxx;
using namespace co_context;

using uring = liburingcxx::URing<0>;

uring ring{8};

int res;

int peer;
char recv_buf[8192];

void handle_peer();

void handle_recv() {
    int nr = res;
    res = peer;
    recv_buf[nr] = '\0';
    printf("%s", recv_buf);
    handle_peer();
}

void handle_peer() {
    peer = res;
    auto sqe = ring.getSQEntry();
    sqe->prepareRecv(peer, recv_buf, 0);
    sqe->setData((uintptr_t)handle_recv);
    ring.appendSQEntry(sqe);
    ring.submit();
}

void event_loop() {
    while (true) {
        auto cqe = ring.waitCQEntry();
        if (cqe->getRes() < 0) {
            fprintf(
                stderr, "Async request failed: %s\n", strerror(-cqe->getRes())
            );
            printf("flag = %u\n", cqe->getFlags());
            exit(1);
        }
        res = cqe->getRes();

        reinterpret_cast<void (*)()>(cqe->getData())();
    }
}

void server(uint16_t port) {
    acceptor ac{inet_address{port}};
    auto sqe = ring.getSQEntry();
    sqe->prepareAccept(ac.listen_fd(), nullptr, nullptr, 0);
    sqe->setData((uintptr_t)handle_peer);
    ring.appendSQEntry(sqe);
    ring.submit();

    event_loop();
}

void client(std::string_view hostname, uint16_t port) {
    inet_address addr;
    if (inet_address::resolve(hostname, port, addr)) {
        co_context::socket sock{co_context::socket::create_tcp(addr.family())};
        // 连接一个 server
        auto sqe = ring.getSQEntry();
        sqe->prepareConnect(sock.fd(), addr.get_sockaddr(), addr.length());
        sqe->setData((uintptr_t)handle_peer);
        ring.appendSQEntry(sqe);
        ring.submit();
    } else {
        printf("Unable to resolve %s\n", hostname.data());
        ::exit(0);
    }
    event_loop();
}

int main(int argc, const char *argv[]) {
    if (argc < 3) {
        printf("Usage:\n  %s hostname port\n  %s -l port\n", argv[0], argv[0]);
        return 0;
    }

    int port = atoi(argv[2]);
    if (strcmp(argv[1], "-l") == 0) {
        server(port);
    } else {
        client(argv[1], port);
    }
    return 0;
}
