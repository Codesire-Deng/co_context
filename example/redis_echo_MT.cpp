// multi-threaded redis_echo_server
#include "co_context/net.hpp"

using namespace co_context;

constexpr uint16_t port = 6379;
constexpr uint32_t worker_num = 6;
static_assert(worker_num > 0);
io_context worker[worker_num], balancer;

task<> reply(co_context::socket sock) {
    char recv_buf[100];
    while (true) {
        int nr = co_await sock.recv(recv_buf);
        if (nr <= 0) [[unlikely]] {
            break;
        }
        int nw = co_await (sock.send({"+OK\r\n", 5}));
        if (nw <= 0) [[unlikely]] {
            break;
        }
    }
    ::close(sock.fd());
}

task<> server() {
    acceptor ac{inet_address{port}};
    uint32_t turn = 0;
    for (int sockfd; (sockfd = co_await ac.accept()) >= 0;) {
        worker[turn].co_spawn(reply(co_context::socket{sockfd}));
        turn = (turn + 1) % worker_num;
    }
}

int main() {
    balancer.co_spawn(server());
    balancer.start();
    for (auto &ctx : worker) {
        ctx.start();
    }
    balancer.join(); // never stop
    return 0;
}
