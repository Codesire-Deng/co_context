#include "co_context/net.hpp"

using namespace co_context;

constexpr uint16_t port = 6379;

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
}

task<> server() {
    acceptor ac{inet_address{port}};
    for (int sockfd; (sockfd = co_await ac.accept()) >= 0;) {
        co_spawn(reply(co_context::socket{sockfd}));
    }
}

int main() {
    io_context ctx;
    ctx.co_spawn(server());
    ctx.start();
    ctx.join();
    return 0;
}
