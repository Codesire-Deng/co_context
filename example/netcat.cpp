#include <co_context/net.hpp>

#include <string_view>
using namespace co_context;
using Socket = co_context::socket;

// 不断接收字节流并打印到stdout
task<> recv_session(Socket sock) {
    alignas(8192) char buf[8192];
    int nr = co_await sock.recv(buf);

    while (nr > 0) {
        nr = co_await (
            lazy::write(STDOUT_FILENO, {buf, (size_t)nr}) && sock.recv(buf)
        );
    }
}

// 将键盘输入发送给对方
task<> send_session(Socket sock) {
    alignas(8192) char buf[8192];
    int nr = co_await lazy::read(STDIN_FILENO, buf);

    while (nr > 0) {
        nr = co_await (
            sock.send({buf, (size_t)nr}) && lazy::read(STDIN_FILENO, buf)
        );
    }
}

task<> server(uint16_t port) {
    acceptor ac{inet_address{port}};
    // 不断接受 client，每个连接生成 session 协程
    for (int sockfd; (sockfd = co_await ac.accept()) >= 0;) {
        co_spawn(recv_session(Socket{sockfd}));
        co_spawn(send_session(Socket{sockfd}));
    }
}

task<> client(std::string_view hostname, uint16_t port) {
    inet_address addr;
    if (inet_address::resolve(hostname, port, addr)) {
        Socket sock{Socket::create_tcp(addr.family())};
        // 连接一个 server
        int res = co_await sock.connect(addr);
        if (res < 0) {
            printf("res=%d: %s\n", res, strerror(-res));
        }
        // 生成 session 协程
        co_spawn(recv_session(Socket{sock.fd()}));
        co_spawn(send_session(Socket{sock.fd()}));
    } else {
        printf("Unable to resolve %s\n", hostname.data());
    }
}

int main(int argc, const char *argv[]) {
    if (argc < 3) {
        printf("Usage:\n  %s hostname port\n  %s -l port\n", argv[0], argv[0]);
        return 0;
    }

    io_context context;

    int port = atoi(argv[2]);
    if (strcmp(argv[1], "-l") == 0) {
        context.co_spawn(server(port));
    } else {
        context.co_spawn(client(argv[1], port));
    }

    context.start();
    context.join();

    return 0;
}
