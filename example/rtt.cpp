#include <algorithm>
#include <cmath>
#include <co_context/all.hpp>
#include <iostream>

using namespace co_context;

namespace server {

task<> session(int sockfd) {
    co_context::socket sock{sockfd};
    sock.set_tcp_no_delay(true);
    char buf[8192];
    int nr = co_await sock.recv(buf);

    while (nr > 0) {
        nr = co_await (sock.send({buf, (size_t)nr}) && sock.recv(buf));
    }

    sock.close().detach();
}

task<> server(const uint16_t port) {
    acceptor ac{inet_address{port}};
    for (int sock; (sock = co_await ac.accept()) >= 0;) {
        co_spawn(session(sock));
    }
}

} // namespace server

namespace client {

constexpr int repeat = 10000;
constexpr size_t payload_lengths[] = {
    4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192,
};
//   16384, 32768, 65536, 1000000};
using duration_type = std::chrono::microseconds;

constexpr std::string_view host = "127.0.0.1";

void collect(
    std::vector<duration_type> &round_trips, size_t payload_length
) noexcept {
    // for (auto &i:round_trips) {
    //     printf("%lu,", i.count());
    // }
    // char b[500];
    // scanf("%s", b);
    const int num = int(round_trips.size());
    if (num < 100) {
        log::e("Too few samples: %d\n", num);
        return;
    }
    std::sort(round_trips.begin(), round_trips.end());
    log::i(
        "size =%5lu B,\t min =%3lu us,\t p50 =%3lu us,\t p99 =%3lu us,\t max =%4lu us,\t ",
        payload_length, round_trips[0].count(), round_trips[num / 2].count(),
        round_trips[(size_t)std::trunc(num * 0.99)].count(),
        round_trips[num - 1].count()
    );
    auto total = std::accumulate(
        round_trips.begin(), round_trips.end(), duration_type{0}
    );
    log::i("avg =%6.2f us\n", double(total.count()) / num);
}

task<> session(
    co_context::socket sock,
    std::span<const char> payload,
    std::vector<duration_type> &round_trips
) {
    std::vector<char> buf;
    buf.resize(payload.size());

    auto start_time = std::chrono::steady_clock::now();
    for (int round = 0; round < repeat; ++round) {
        int nr = co_await (sock.send(payload) && sock.recv(buf));
        if (size_t(nr) != payload.size()) [[unlikely]] {
            log::e(
                "recv length mismatch: %d (%lu expected)", nr, payload.size()
            );
            sock.close().detach();
            co_return;
        }
    }
    auto now = std::chrono::steady_clock::now();

    for (int i = 0; i < repeat; ++i) {
        round_trips.push_back(std::chrono::duration_cast<duration_type>(
            (now - start_time) / repeat
        ));
    }
    sock.close().detach();
    co_return;
}

task<> client(
    const uint16_t port,
    std::span<const char> payload,
    std::vector<duration_type> &round_trips
) {
    using co_context::socket;
    inet_address addr;
    if (inet_address::resolve(host, port, addr)) {
        socket sock{socket::create_tcp(addr.family())};
        int res = co_await sock.connect(addr);
        if (res < 0) {
            log::e("%s\n", strerror(-res));
            co_return;
        }
        sock.set_tcp_no_delay(true);
        co_await session(std::move(sock), payload, round_trips);
    }
}

task<> perf_client(const uint16_t port) {
    std::vector<duration_type> round_trips;
    std::vector<char> payload;
    for (auto len : payload_lengths) {
        log::i("perf_client payload=%lu\n", len);
        payload.resize(len);
        std::fill_n(payload.begin(), len, '.');
        round_trips.clear();
        round_trips.reserve(repeat);
        co_await client(port, payload, round_trips);
        collect(round_trips, len);
    }
    log::i("end\n");
}

} // namespace client

int main(int argc, const char **argv) {
    if (argc != 2) {
        log::e(
            "[ERROR]   Too few/many arguments.\n"
            "[EXAMPLE] co_tcp 0      (client)\n"
            "[EXAMPLE] co_tcp 1      (server)\n"
            "[EXAMPLE] co_tcp 2      (server & client on the same io_context)\n"
            "[EXAMPLE] co_tcp 3      (server & client on different io_context)\n"
        );
    }
    io_context ctx;
    if (argv[1][0] == '0') {
        ctx.co_spawn(client::perf_client(1234));
    } else if (argv[1][0] == '1') {
        ctx.co_spawn(server::server(1234));
    } else if (argv[1][0] == '2') {
        ctx.co_spawn(server::server(1234));
        ctx.co_spawn(client::perf_client(1234));
    } else if (argv[1][0] == '3') {
        ctx.co_spawn(server::server(1234));
        io_context ctx_client;
        ctx_client.co_spawn(client::perf_client(1234));
        ctx.start();
        ctx_client.start();
        ctx.join();
        return 0;
    } else {
        log::e("[ERROR]   arguments error\n");
    }
    ctx.start();
    ctx.join();
    return 0;
}
