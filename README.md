# co_context

An io_context library aimed at low-latency io, based on [liburingcxx](https://github.com/Codesire-Deng/liburingcxx).

**co_context** 是一个**协程**异步多线程并发框架，主要瞄准性能（倾向低延迟）。**co_context** 也致力于减轻用户的心智负担，让 C++ 初学者也能轻松写出高并发程序。

目前 **co_context** 的设计仍处于极早期的混沌状态。No timeline and no roadmap yet.

## Requirement

1. [mimalloc](https://github.com/microsoft/mimalloc)

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
5. 协程自身的缓存不友好问题（主要由 `operator new` 引起），需要借助其他工具来解决，例如 [mimalloc](https://github.com/microsoft/mimalloc)。

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

<details>

<summary>Draft</summary>

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

一种激进的 IO 协程，在 `promise.inital_suspend()` 时初始化 IO 请求并提交，随后返回 `suspend_always`。

在被 `co_await` 时，若 IO 早已完成，则无需让出。否则，需要等待 IO 完成后由调度器唤醒。

#### eager_io 的动机

1. 可以轻易部署并行化的 IO 请求，且对于 caller 协程来说是非阻塞的。还可进化出可取消的协程。
2. 尽早提交 IO 请求，可能带来更低的延迟。

#### eager_io 的缺点

涉及多线程并行，需要同步 IO 的状态（未完成、已完成）。至少要保证：调度器必须确保 「eager_io 已经知悉 IO 已完成」，否则可能丢数据。

#### eager_io 的实现

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

此实现中可能的漏洞：

1. 未反省协程发生异常时的内存模型
2. 等你来发现……

### lazy_io

一种懒惰的 IO 协程，在 `promise.inital_suspend()` 时什么都不做。

在被 `co_await` 时暂停，并发起 IO 请求，未来等待由调度器唤醒。当前线程轮询可以切入的协程。

#### lazy_io 的实现

1. lazy_io 提供的 `operator co_await` 返回一个 `awaiter`，其中的 `await_suspend` 负责主要逻辑：
   1. 提交一个 IO 请求。
   2. 找到一个已收割的 IO 请求，恢复它
2. `awaiter` 的 `await_resume` 返回特定结果。
3. 析构时，销毁协程。

</details>
