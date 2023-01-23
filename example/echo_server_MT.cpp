// multi-threaded echo_server
#include "co_context/net.hpp"
using namespace co_context;

constexpr uint32_t worker_num = 4;
static_assert(worker_num > 0);
io_context worker[worker_num], balancer;

task<> session(int sockfd) {
    co_context::socket sock{sockfd};
    char buf[8192];
    int nr = co_await sock.recv(buf);

    while (nr > 0) {
        nr = co_await (sock.send({buf, (size_t)nr}) && sock.recv(buf));
    }
}

task<> server(const uint16_t port) {
    acceptor ac{inet_address{port}};
    uint32_t turn = 0;
    for (int sock; (sock = co_await ac.accept()) >= 0;) {
        worker[turn].co_spawn(session(sock));
        turn = (turn + 1) % worker_num;
    }
}

int main() {
    balancer.co_spawn(server(1234));
    balancer.start();
    for (auto &ctx : worker) {
        ctx.start();
    }
    balancer.join(); // never stop
    return 0;
}
