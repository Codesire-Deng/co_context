#include "co_context/io_context.hpp"
#include "co_context/lazy_io.hpp"
#include "co_context/net/acceptor.hpp"

using namespace co_context;

constexpr uint16_t port = 6379;

main_task reply(co_context::socket sock) {
    char recv_buf[512];
    while (true) {
        int n = co_await sock.recv(recv_buf);
        if (n <= 0) co_return;
        co_await sock.send({"+OK\r\n", 5});
    }
}

main_task server() {
    acceptor ac{inet_address{port}};
    for (int sockfd; (sockfd = co_await ac.accept()) >= 0;) {
        co_spawn(reply(co_context::socket{sockfd}));
    }
}

int main() {
    io_context ctx{64};
    ctx.co_spawn(server());
    ctx.run();
    return 0;
}