#include "co_context/net.hpp"

using namespace co_context;

constexpr uint16_t port = 6379;

task<> reply(co_context::socket sock) {
    char recv_buf[100];
    int n = co_await sock.recv(recv_buf);
    while (n > 0) {
        n = co_await (sock.send({"+OK\r\n", 5}) && sock.recv(recv_buf));
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