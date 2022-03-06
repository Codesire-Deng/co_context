# co_context

An io_context library aimed at low-latency io, based on liburingcxx.

**co_context** 是一个**协程**异步多线程并发框架，主要瞄准性能（低延迟优先，高并发其次）。**co_context** 也致力于减轻用户的心智负担。让 C++ 初学者也能轻松写出高并发程序。

目前 **co_context** 的设计仍处于极早期的混沌状态。No timeline and no roadmap yet.

如果你正在寻找 C++ 异步的终极解决方案，且不在乎编程复杂度，我推荐你学习 **sender/receiver model**，而无需尝试协程。

---

<detail>

# draft zone

- 研究 liburingcxx 如何支持多生产者，多消费者并行（线程池中每个线程同时是 IO 生产者和消费者）
- Coroutine 解决内联和动态内存分配问题
- 表达式模板解决 task `&&` `||`。
- 和 `std::execution` 能否兼容

## 线程池实现

- 一个内核线程 polling，一个主线程收集提交、收割推送I/O，其他固定 worker 线程，thread bind core
- 节能模式：信号量表示允许的 idle worker 线程数量。低延模式：每个 worker 都 polling
- 每个 worker 自带两条任务队列（一个sqe，一个cqe），固定长度，原子变量，cacheline友好。sqe放不下就放 std::queue，等有空位再放入共享cache。
- 主线程cqe推送满了就切换到提交sqe
- 主线程sqe提交满了就切换到推送cqe

## 协程存在的问题

### 弱点

1. 除非编译器优化，每个协程都需要通过 `operator new` 来分配 frame：
   - 动态内存分配可能引发性能问题；
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



</detail>
