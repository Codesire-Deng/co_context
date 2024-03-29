#include <co_context/net/acceptor.hpp>
#include <liburing.h>

using namespace std;
using namespace co_context;

io_uring ring;
const int entries = 8;

int res;

int peer;
char recv_buf[8192];

void handle_peer();

void handle_recv() {
    const int nr = res;
    res = peer;
    recv_buf[std::min(nr, 8191)] = '\0';
    printf("%s", recv_buf);
    handle_peer();
}

void handle_peer() {
    peer = res;
    auto *sqe = io_uring_get_sqe(&ring);
    io_uring_prep_recv(sqe, peer, recv_buf, 8192, 0);
    io_uring_sqe_set_data(sqe, (void *)handle_recv);
    io_uring_submit(&ring);
}

void event_loop() {
    while (true) {
        io_uring_cqe *cqe;
        io_uring_wait_cqe(&ring, &cqe);
        if (cqe->res < 0) {
            fprintf(stderr, "Async request failed: %s\n", strerror(-cqe->res));
            printf("flag = %u\n", cqe->flags);
            exit(1);
        }

        res = cqe->res;
        auto *handler = reinterpret_cast<void (*)()>(cqe->user_data);
        io_uring_cqe_seen(&ring, cqe);

        handler();
    }
}

void server(uint16_t port) {
    acceptor ac{inet_address{port}};
    auto *sqe = io_uring_get_sqe(&ring);
    io_uring_prep_accept(sqe, ac.listen_fd(), nullptr, nullptr, 0);
    io_uring_sqe_set_data(sqe, (void *)handle_peer);
    io_uring_submit(&ring);

    event_loop();
}

void client(std::string_view hostname, uint16_t port) {
    inet_address addr;
    if (inet_address::resolve(hostname, port, addr)) {
        co_context::socket sock{co_context::socket::create_tcp(addr.family())};
        // 连接一个 server
        auto *sqe = io_uring_get_sqe(&ring);
        io_uring_prep_connect(
            sqe, sock.fd(), addr.get_sockaddr(), addr.length()
        );
        io_uring_sqe_set_data(sqe, (void *)handle_peer);
        io_uring_submit(&ring);
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

    io_uring_queue_init(entries, &ring, 0);

    const int port = atoi(argv[2]);
    if (strcmp(argv[1], "-l") == 0) {
        server(port);
    } else {
        client(argv[1], port);
    }
    return 0;
}
