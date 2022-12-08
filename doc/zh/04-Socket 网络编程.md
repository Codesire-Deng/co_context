[上一节](/doc/zh/02-协程.md)

# 04-Socket 网络编程

这一节，我们实现一个 echo_server。

## 服务器骨架

```cpp
#include "co_context/all.hpp"
using namespace co_context;

task<> session(int sockfd) {}

task<> server(const uint16_t port) {}

int main() {
    io_context ctx;
    ctx.co_spawn(server(1234));
    ctx.start();
    ctx.join();
    return 0;
}
```

一个服务器至少包含两部分：监听端口和处理连接。我们将在 `server(port)` 中监听 1234 端口，并将创建的连接交给 `session(sockfd)` 来处理。

## 监听端口

co_context 提供了 `acceptor` 抽象，只需指定地址和端口即可用于监听：

```cpp
acceptor ac{inet_address{port}};
```

`acceptor` 具有成员函数 `accept()`，调用它时，协程将会**阻塞**，直到返回一个 socket fd。绝大多数情况下，阻塞函数前都应该加上 `co_await`。于是，`server(port)` 的实现如下：

```cpp
task<> server(const uint16_t port) {
    acceptor ac{inet_address{port}};
    for (int sock; (sock = co_await ac.accept()) >= 0;) {
        co_spawn(session(sock));
    }
}
```

在循环中，变量 `sock` 持有 socket fd，我们想将它转交给 `session(sockfd)` 协程，同时不能打断 `server(port)` 协程的继续运行，所以又使用了 `co_spawn` 来向调度器提交新的协程。这里没有标明 `ctx.co_spawn(...)`，是因为「`session` 和 `server` 共享了同一个 io_context」。

> 协程的**阻塞**意味着暂停和切换协程，但不会导致当前线程阻塞。

> 协程在运行时蕴含着调度器，此时 `co_spawn()` 和 `ctx.co_spawn()` 是等价的。另外，你甚至可以指定不同的调度器。

接下来我们实现 `session(sockfd)` 协程。

## 处理连接

在 echo_server 中，对每一个连接，只要重复两件事：

1. 读取 socket 中的内容到某个 buffer。
2. 将 1. 中的 buffer 的内容发送到 socket。

所以，我们先定义一个 buffer，然后依次调用 `recv` 和 `send` 接口即可：

```cpp
task<> session(int sockfd) {
    co_context::socket sock{sockfd};
    char buf[1000];

    while (true) {
        int nr = co_await sock.recv(buf);
        co_await sock.send({buf, (size_t)nr});
    }
}
```

先包装一个 `socket` 对象，方便使用协程式的成员函数。我们直接在 `task<void>` 中申请了 1000 字节的 buffer，看上去好像是栈内存。但其实不是——这个 buffer 是在**堆内存**上的。

> 协程中的局部变量通常是在堆内存上申请的。
> 程序员无需特别关心这些堆内存，它们仍满足 RAII，协程会自动回收这些堆内存。

有了 buffer，我们在死循环中调用 `recv` 和 `send`，这就完成了 socket 的读取和发送。你可以在 `man recv.3` 等手册中找到这些系统调用的细节。

### buffer 传参

在 co_context 中，buffer 的传参是用 `std::span<char>` 和 `std::span<const char>` 来完成的。因此在 `recv` 中，你可以不指定 buffer 的长度（C++会智能地识别出 `buf` 数组的长度为 1000），但在 `send` 调用中，你必须指定数据的长度。

### 错误处理

当 socket 被关闭或发生其它错误时，系统调用会返回 0 或负数。健壮的程序应该检查这些返回值。考虑本文档并非专门的网络编程教程，这里没有设计错误处理。

### 并发能力

虽然只是单线程，但这个程序是支持高并发的。协程之间的**独立运行**和**快速切换**造就了高并发。参考并发量是 50k ~ 400k QPS。请留意 buffer 的大小可能会影响性能，建议设置为 8192。

## 最终代码

本案例（单线程版本）：见 [example/echo_server.cpp](/example/echo_server.cpp)。

另附（负载均衡、多线程版本）：见 [example/echo_server_MT.cpp](/example/echo_server_MT.cpp)。

[下一节（未完成）](./04-Socket%20网络编程.md)