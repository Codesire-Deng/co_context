[上一节](./03-你好，协程.md)

# 04-Socket 网络编程

这一节，我们实现一个 echo_server。

## 服务器骨架

```cpp
#include <co_context/all.hpp>
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

`acceptor` 具有成员函数 `accept()`，调用它时，协程将会**阻塞**，直到返回一个 socket fd。阻塞函数前都应该加上 `co_await`。于是，`server` 的实现如下：

```cpp
task<> server(const uint16_t port) {
    acceptor ac{inet_address{port}};
    for (int sock; (sock = co_await ac.accept()) >= 0;) {
        co_spawn(session(sock));
    }
}
```

在循环中，变量 `sock` 持有 socket fd，我们想将它转交给 `session(sockfd)` 协程，同时不能打断 `server(port)` 协程的继续运行，所以又使用了 `co_spawn` 来向调度器生成新的协程。这里没有标明调度器 `ctx`，是因为「`session` 和 `server` 共享了同一个 io_context」。

> 协程的**阻塞**意味着暂停和切换协程，但不会导致当前线程阻塞。

> 协程在运行时蕴含着调度器，此时 `co_spawn()` 和 `ctx.co_spawn()` 是等价的。当然，你可以指定不同的调度器。

接下来我们实现 `session(sockfd)` 协程。

## 处理连接

在 echo_server 中，对每一个连接，只要重复两件事：

1. 读取 socket 中的内容到某个 buffer。
2. 将 1. 中的 buffer 的内容发送到 socket。

所以，我们先定义一个 buffer，然后依次调用 `recv` 和 `send` 接口即可：

```cpp
task<> session(int sockfd) {
    co_context::socket sock{sockfd};
    char buf[8192];

    while (true) {
        int nr = co_await sock.recv(buf);
        co_await sock.send({buf, (size_t)nr});
    }
}
```

先包装一个 `socket` 对象，方便使用协程式的成员函数。

随后申请 8192 字节的 buffer，在死循环中调用 `recv` 和 `send`，这就完成了 socket 的读取和发送。你可以在 `man recv.3` 等手册中找到这些系统调用的细节。

<details>
<summary>进阶：堆内存</summary>

`char buf[8192]` 看上去好像是栈内存，但其实这个 buffer 位于**堆内存**。

> 协程中的局部变量通常是在堆内存上申请的。
> 程序员无需特别关心这些堆内存，它们仍满足 RAII，协程会自动回收这些堆内存。
</details>

<details>
<summary>进阶：buffer 传参</summary>

在 co_context 中，buffer 的传参是用 `std::span` 来完成的。因此在 `recv` 中，你可以不指定 buffer 的长度（C++会智能地识别出 `buf` 数组的长度为 8192。但在 `send` 调用中，你仍需指定数据的长度，以免发送多余的数据。
</details>

<details>
<summary>进阶：错误处理</summary>

当 socket 被关闭或发生其它错误时，系统调用会返回 0 或负数。健壮的程序应该检查这些返回值。考虑本文档并非专门的网络编程教程，这里没有设计错误处理。
</details>

## 高并发

虽然只是**单线程**随便写写，但这个程序是高并发的。协程之间的**独立运行**和**快速切换**造就了高并发。参考并发量是 50k ~ 400k QPS。请留意 buffer 的大小可能会影响性能，建议设置为 8192。

## 最终代码

本案例（单线程版本）：见 [example/echo_server.cpp](/example/echo_server.cpp)。

另附（负载均衡、多线程版本）：见 [example/echo_server_MT.cpp](/example/echo_server_MT.cpp)。

不止于网络，[下一节](./05-IO%20%E6%8E%A5%E5%8F%A3.md)将介绍更多 I/O 接口。