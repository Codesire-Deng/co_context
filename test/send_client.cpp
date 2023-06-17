#include <co_context/net.hpp>
#include <string_view>
using namespace co_context;

constexpr std::string_view message = "<<<co_context>>>";
constexpr int times = 100;
constexpr std::string_view host = "127.0.0.1";
constexpr uint16_t port = 6379;

using Socket = co_context::socket;

task<> send(Socket sock) {
    // NOTE `getchar()` will block current thread
    while (getchar()) {
        for (int i = 0; i < times; ++i) {
            co_await sock.send(message);
        }
    }
}

task<> client() {
    inet_address addr;
    if (inet_address::resolve(host, port, addr)) {
        Socket sock{Socket::create_tcp(addr.family())};
        printf("connect...\n");
        int res = co_await sock.connect(addr);
        if (res < 0) {
            log::e("%s\n", strerror(-res));
            ::exit(0);
        }
        printf("connect...OK\n");
        co_await send(std::move(sock));
    }
}

int main() {
    io_context ctx;
    ctx.co_spawn(client());
    ctx.start();
    ctx.join();
    return 0;
}
