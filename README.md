# co_context

An io_context library aimed at low-latency io, based on [liburingcxx](https://github.com/Codesire-Deng/liburingcxx).

**co_context** 是一个**协程**异步多线程并发框架，主要瞄准性能（低延迟优先，高并发其次）。**co_context** 也致力于减轻用户的心智负担，让 C++ 初学者也能轻松写出高并发程序。

目前 **co_context** 的设计仍处于极早期的混沌状态。No timeline and no roadmap yet.

## 谁不需要协程

在我创建这个项目之前，我已经知道基于协程的异步框架很可能**不是**性能最优解，如果你正在寻找 C++ 异步的终极解决方案，且不在乎编程复杂度，我推荐你学习 **sender/receiver model**，而无需尝试协程。

## 谁需要协程

如果你希望异步框架能够最佳地平衡「开发、维护成本」和「项目质量、性能」，从而最大化经济效益，我推荐你关注协程方案。


## 关于缓存友好问题

**co_context** 竭尽所能避免缓存问题：
1. **co_context** 的主线程和任意 worker 的数据交换中没有使用互斥锁或原子变量。
2. **co_context** 的数据结构保证「可能频繁读写」的 cacheline 最多被两个线程访问，无论并发强度有多大。这个保证本身也不经过互斥锁或原子变量。（若使用原子变量，高竞争下性能损失约 33%～70%）
3. 对于可能被多于两个线程读写的 cacheline，**co_context** 保证乒乓缓存问题最多发生常数次。
4. 在 AMD-5800X，3200 Mhz-ddr4 环境下，若绕过 io_uring，**co_context** 的线程交互频率可达 1.25 GHz。（线程数=6，swap_slots=256，0.980 s 内完成1.25 G 次生产消费）
5. 协程自身的缓存不友好问题（主要由 `operator new` 引起），需要借助其他工具来解决。


---

<details>

<summary>draft zone</summary>

# draft

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



</details>
