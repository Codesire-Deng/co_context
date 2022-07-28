#include "co_context/net.hpp"
using namespace co_context;

task<> session(int sock) {
    co_context::socket sock_{sock};
    char buf[1000];

    while (true) {
        int nr = co_await sock_.recv(buf);
        co_await sock_.send({buf, (size_t)nr});
    }
}

task<> server(const uint16_t port) {
    acceptor ac{inet_address{port}};
    for (int sock; (sock = co_await ac.accept()) >= 0;) {
        co_spawn(session(sock));
    }
}

int main() {
    io_context ctx;
    ctx.co_spawn(server(1234));
    ctx.run();
    return 0;
}   