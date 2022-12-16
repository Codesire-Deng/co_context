[简中](../README.md) | English

# co_context

A C++20 **coroutine** framework offering highly-concurrent I/O with a reasonable latency, based on Linux [io_uring](https://github.com/axboe/liburing). 

> For performance reasons, we have rewritten [liburing](https://github.com/axboe/liburing), as [liburingcxx](https://github.com/Codesire-Deng/liburingcxx).

## Features

1. Syscalls like `read` `write` `accept` `timeout` which are provided by [io_uring](https://github.com/axboe/liburing), 74 in total.
2. Concurrency support: `all`, `mutex`, `semaphore`, `condition_variable`, `channel`. Provides thread-safty between coroutines. 
3. Dispatch hint: `yield`, `resume_on`

## Build

### Requirement

1. GCC 11.3.0 or Clang 14 or above.
2. [Optional] [mimalloc](https://github.com/microsoft/mimalloc). You may install it by package manager or from source.
3. **Linux kernel** >= 5.6. The newer the better.
    - Run `uname -r` to see your kernel version.
    - Since we are developing under Linux 6.0, if you have met some thing wrong, please open an [issue](https://github.com/Codesire-Deng/co_context/issues) and I would appreciate it very much.
    - **docker will inherit the Linux kernel of the host**. Therefore, docker does no help to kernel version problems.

### Build from source

```bash
cmake -B build && cmake --build build -j
build/example/timer # Run an example
```

## Example

### Basic usage

Create an `io_context` to run coroutines: 

```cpp
    using namespace co_context;
    io_context context;
```

Define a `task<>` coroutine who listens and accepts socket connections. `task<>` is just like a normal `void` function but you can use `co_await` inside.

```cpp
task<> server(uint16_t port) {
    acceptor ac{inet_address{port}};
    for (int sockfd; (sockfd = co_await ac.accept()) >= 0;) {// async accept clients
        co_spawn(session(co_context::socket{sockfd})); // generate a business coroutine for each
    }
}
```

Define another coroutine to describe business logic, e.g. receive from socket and output to stdout.

```cpp
task<> session(co_context::socket sock) {
    char buf[8192];
    int nr = co_await sock.recv(buf);

    while (nr > 0) {
        co_await lazy::write(STDOUT_FILENO, {buf, (size_t)nr});
        nr = co_await sock.recv(buf);
    }
}
```

`task<T> f()` is just like `T f()` which can be nested arbitrarily.

```cpp
task<int> the_answer() {
    co_return 42;
}
task<> universe() {
    printf("The answer is %d\n", co_await the_answer());
    co_return;
}
```

How to write `main()`:

```cpp
int main(int argc, const char *argv[]) {
    if (argc < 3) {
        printf("Usage:\n  %s hostname port\n  %s -l port\n", argv[0], argv[0]);
        return 0;
    }

    io_context context; // 1. define io_context

    int port = atoi(argv[2]);
    if (strcmp(argv[1], "-l") == 0) {
        context.co_spawn(server(port)); // 2. generate at least one coroutine.
    } else {
        context.co_spawn(client(argv[1], port));
    }

    context.start(); // 3. start the io_context in a new thread.
    context.join();  // 4. wait for that thread.

    return 0;
}

```

### More widgets

#### One second timer

```cpp
task<> my_clock() {
    for (int cnt = 0;;) {
        printf("Time = %d\n", cnt++);
        co_await timeout(1s);
    }
}
```

#### Network timeout

```cpp
task<> session(co_context::socket peer) {
    char buf[8192];
    int nr = co_await timeout(peer.recv(buf), 3s); // time limit 3s

    while (nr > 0) {
        co_await lazy::write(STDOUT_FILENO, {buf, (size_t)nr});
        nr = co_await timeout(peer.recv(buf), 3s); // time limit 3s
    }

    log_error(-nr); // check the error
}

void log_error(int err) {
    switch (err) {
        case ECANCELED:
            log::e("timeout!\n");
            break;
        default:
            log::e("%s\n", strerror(err));
            break;
    }
}
```

#### Load balancing

An `io_context` is an OS-thread. For load balancing, just dispatch those `task<>` to appropriate `io_context`.

[example: echo_server_MT.cpp](../example/echo_server_MT.cpp)

#### Generator

An awesome sequence generator which can play with `std::range`. *Requires g++*

```cpp
co_context::generator<int> gen_iota(int x) {
    while (true)
        co_yield x++;
}

int main() {
    using namespace std::views;

    for (int x : gen_iota(1) | drop(5) | take(3)) {
        std::cout << x << " ";
    }
    // Output 6 7 8

    return 0;
}
```

#### Linked IO

You can use `&&` to link I/Os. Linking I/Os will enhance performance.(But only the last result will be returned by default.)

```cpp
nr = co_await (
    peer.send({buf, (size_t)nr})
    && peer.recv(buf)
);
```

This example use link_io in an echo_server.

#### channel(experimental)

An blocking queue inspired by Golang.

[example: channel.cpp](../test/channel.cpp)


<details>

<summary>Development details(not translated yet)</summary>

## 协程方案的局限场景

由于内置动态内存分配，基于协程的异步框架可能**不是**性能的最优解，如果你正处于类似 30ns 延迟的极端性能场景，且不在乎编程复杂度，推荐关注 **sender/receiver model**，而无需尝试协程。

## 协程方案的适用场景

如果你希望异步框架能够最佳地平衡「开发、维护成本」和「项目质量、性能」，从而最大化经济效益，推荐你关注协程方案。感性理解：协程 + 内核态 I/O 的性能类似于 Redis 的网络模块。

## 关于缓存友好问题

**co_context** 竭尽所能避免缓存问题：

1. **co_context** 的主线程和任意 worker 的数据交换中没有使用互斥锁，极少使用原子变量。
2. **co_context** 的数据结构保证「可能频繁读写」的 cacheline 最多被两个线程访问，无论并发强度有多大。这个保证本身也不经过互斥锁或原子变量。（若使用原子变量，高竞争下性能损失约 33%～70%）
3. 对于可能被多于两个线程读写的 cacheline，**co_context** 保证乒乓缓存问题最多发生常数次。
4. 在 AMD-5800X，3200 Mhz-ddr4 环境下，若绕过 io_uring，**co_context** 的线程交互频率可达 1.25 GHz。
5. 在一个本地测试中（I7-8550U 移动端），**单线程**的协程切换的平均延迟为 9.4 ns，代码于 [test/ctx_swtch.cpp](test/ctx_swtch.cpp)。
5. 在一个本地测试中（R7-5800X 桌面端），**跨线程**的协程切换的平均延迟为 37 ns，代码于 [test/ctx_swtch.cpp](test/ctx_swtch.cpp)。
6. 协程自身的缓存不友好问题（主要由 `operator new` 引起），需要借助其他工具来解决，例如 [mimalloc](https://github.com/microsoft/mimalloc)。

---


## 协程存在的问题

### 弱点

1. 除非编译器优化，每个协程都需要通过 `operator new` 来分配 frame：
   - 多线程高频率动态内存分配可能引发性能问题；
   - 在嵌入式或异构（例如 GPU）环境下，缺乏动态内存分配能力，难以工作。
2. 除非编译器优化，协程的可定制点太多，需要大量间接调用/跳转（而不是内联），同样引起性能问题。
   - 目前，编译器通常难以内联协程
   - HALO 优化理论：[P0981R0](http://open-std.org/JTC1/SC22/WG21/docs/papers/2021/p2300r3.html#biblio-p0981r0)
3. **动态分配**和**间接调用**的存在，导致协程暂时无法成为异步框架的最优方法。

### 拆分子协程？

- 出于性能考虑，不要将大协程拆分为几个小协程，因为会增加动态内存分配次数。
  - 可以做 placement new 吗？

### 与异步框架高度耦合

1. 暂停和恢复都需要通过异步框架。
2. 表达式模板的潜力不如 sender/receiver 模型：
   - 协程是顺序/分支/循环结构，s/r是表达式。

## draft

- 研究 liburingcxx 如何支持多生产者，多消费者并行（线程池中每个线程同时是 IO 生产者和消费者）
- Coroutine 解决内联和动态内存分配问题
- 表达式模板解决 task `&&` `||`。
- 和 `std::execution` 能否兼容

### 线程池实现

- 一个内核线程 polling，一个主线程收集提交、收割推送I/O，其他固定 worker 线程，thread bind core
- 节能模式：信号量表示允许的 idle worker 线程数量。低延模式：每个 worker 都 polling
- 每个 worker 自带两条任务队列（一个sqe，一个cqe），固定长度，原子变量，cacheline友好。sqe放不下就放 std::queue，等有空位再放入共享cache。
- 主线程cqe推送满了就切换到提交sqe
- 主线程sqe提交满了就切换到推送cqe

### eager_io

一种激进的 IO awaiter，在构造函数中初始化 IO 请求并提交。

在被 `co_await` 时，若 IO 早已完成，则无需让出。否则，需要等待 IO 完成后由调度器唤醒。

#### eager_io 的动机

1. 可以轻易部署并行化的 IO 请求，且对于 caller 协程来说是非阻塞的。还可进化出可取消的协程。
2. 尽早提交 IO 请求，可能带来更低的延迟。

#### eager_io 的缺点

涉及多线程并行，需要同步 IO 的状态（未完成、已完成）。至少要保证：调度器必须确保 「eager_io 已经知悉 IO 已完成」，否则可能丢数据。

#### eager_io 的实现

TODO: 改用原子变量，弃用检查队列

**co_context** 假设大多数 eager_io 会陷入「等待状态」，以此为优化立足点

1. eager_io 的 coroutine state(promise) 是调度器负责决定由谁销毁（由调度器或者由协程自己）。
2. eager_io 发起 IO 前，自我标记为「初始状态」「无结果」「无权销毁」，然后发起 IO。
3. eager_io 在「初始状态」下被 `co_await`，检查结果：
   1. 为「无结果」，则自我标记为「等待状态」「有权销毁」「有结果」，让出执行权
   2. 为「有结果」，自我标记为「IO 后状态」（保持「无权销毁」），继续执行。
   3. 析构时，「有权销毁」则销毁协程，否则自我标记为「待销毁」。
4. 调度器收割 IO 时，检查协程的标记：
   1. 为「等待状态」，则将协程加入调度队列，令其自行销毁。
   2. 为「初始状态」（初始、等待叠加态），向协程标记「有结果」，随后将协程加入检查队列
5. 调度器完成一轮提交/收割后，轮询检查队列：
   1. 若协程为「等待状态」，则弹出检查队列，并加入调度队列，令其自行销毁。
   2. 若协程为「初始状态」或者「IO 后状态」，不管它。
   3. 若协程为「待销毁」，销毁它，弹出检查队列。

xxx <-> is_detached is_waiting is_ready

manager:

- ready: xx0 to xx1
  - 1x1 : manager delete task_info, do not resume.
  - 001 : worker will delete task_info, do not resume.
  - 011 : worker will delete task_info, resume

worker:

- wait: x0x to x1x
  - 11x : wait after detached, logic error
  - 010 : suspend, worker will delete task_info
  - 011 : do not suspend, worker will delete task_info
- detach: 0xx to 1xx
  - 1x1 : worker will delete task_info
  - 100 : manager will delete task_info
  - 110 : detach after waited, logic error

此实现中可能的漏洞：

1. 未反省协程发生异常时的内存模型
2. 等你来发现……

### lazy_io

一种懒惰的 IO awaiter，在，在构造函数时什么都不做。

在被 `co_await` 时暂停，并发起 IO 请求，未来等待由调度器唤醒。当前线程轮询可以切入的协程。

#### lazy_io 的实现

1. lazy_io 返回一个 `awaiter`，其中的 `await_suspend` 负责主要逻辑：
   1. 提交一个 IO 请求。
   2. 找到一个已收割的 IO 请求，恢复它
2. `awaiter` 的 `await_resume` 返回特定结果。
3. 析构时，销毁协程。

### semaphore

仅运行在用户态 co_context 的信号量

#### semaphore 的动机

限制 `co_spawn` 和同类活跃协程的并发量

#### semaphore 的实现

1. 参考 std::semaphore，优化 binary_semaphore 的原子变量
2. 链表栈模拟无锁队列，均摊O(1)
3. `acquire` 分别在栈上创建 `awaiter`，形成等待链表
4. `release` 时放出一个release请求，由io_context处理（强制单消费者），放入某个reap_swap

</details>
