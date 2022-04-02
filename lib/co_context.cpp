#include <mimalloc-2.0/mimalloc-new-delete.h>
#include "co_context.hpp"
#include "co_context/utility/set_cpu_affinity.hpp"
#include <atomic>

// fold level = 3 (ctrl+a, ctrl+k, ctrl+3 in vscode)
// unfold all (ctrl+a, ctrl+k, ctrl+j in vscode)

namespace co_context {

namespace detail {

    thread_local thread_meta this_thread;

    inline void
    worker_meta::init(const int thread_index, io_context *const context) {
        assert(submit_overflow_buf.empty());
        detail::this_thread.ctx = context;
        detail::this_thread.worker = this;
        detail::this_thread.tid = thread_index;
#ifdef USE_CPU_AFFINITY
        const unsigned logic_cores = std::thread::hardware_concurrency();
        if constexpr (config::using_hyper_threading) {
            if (thread_index * 2 < logic_cores) {
                detail::set_cpu_affinity(thread_index * 2);
            } else {
                detail::set_cpu_affinity(thread_index * 2 % logic_cores + 1);
            }
        } else {
            detail::set_cpu_affinity(thread_index);
        }
#endif
    }

    inline void worker_meta::co_spawn(main_task entrance) noexcept {
        this->submit(entrance.get_io_info_ptr());
    }

    void worker_meta::submit(task_info_ptr io_info) noexcept {
        const unsigned tid = this_thread.tid;
        auto &swap = this_thread.ctx->submit_swap[tid];
        auto &cur = this->submit_cur;
        // std::cerr << cur.slot << " " << tid << cur.off << std::endl;
        if (swap.try_find_empty(cur)) [[likely]] {
            swap.store(cur, io_info, std::memory_order_release);
            log::v("worker[%u] submit at [%u]\n", tid, cur.off);
            cur.next();
        } else {
            this->submit_overflow_buf.push(io_info);
            log::d("worker[%u] submit to OF\n", tid);
        }
    }

    inline void
    worker_meta::run(const int thread_index, io_context *const context) {
        init(thread_index, context);
        log::v("worker[%d] run...\n", thread_index);

        while (true) {
            auto coro = this->schedule();
            log::v("worker[%d] found coro to run\n", thread_index);
            coro.resume();
            log::v("worker[%d] idle\n", thread_index);
        }
    }

    inline std::coroutine_handle<> worker_meta::schedule() noexcept {
        const uint32_t tid = this_thread.tid;
        auto &ctx = *this_thread.ctx;
        auto &submit_swap = ctx.submit_swap[tid];
        auto &reap_swap = ctx.reap_swap[tid];

        log::v("worker[%u] scheduling\n", tid);

        while (true) {
            // handle overflowed submission
            if (!this->submit_overflow_buf.empty()) {
                if (submit_swap.try_find_empty(submit_cur)) {
                    task_info_ptr task = this->submit_overflow_buf.front();
                    this->submit_overflow_buf.pop();
                    submit_swap.store(
                        submit_cur, task, std::memory_order_release);
                    log::d(
                        "worker[%u] submit from OF_buf to [%u]\n", tid,
                        submit_cur.off);
                    submit_cur.next();
                }
            }

            if (reap_swap.try_find_exist(reap_cur)) {
                // TODO judge this memory order
                const std::coroutine_handle<> handle =
                    reap_swap.load(reap_cur, std::memory_order_consume);
                reap_swap[reap_cur] = nullptr;
                log::d("worker[%u] found [%u]\n", tid, reap_cur.off);
                reap_cur.next();
                // printf("get task!\n");
                return handle;
            }
            // TODO consider tid_hint here
        }
    }

} // namespace detail

void io_context::forward_task(task_info_ptr task) noexcept {
    // TODO optimize scheduling strategy
    if (reap_swap.try_find_empty(r_cur)) [[likely]] {
        reap_swap.store(r_cur, task->handle, std::memory_order_release);
        log::v("ctx forward_task to [%u][%u]\n", r_cur.tid, r_cur.off);
        r_cur.next();
    } else {
        reap_overflow_buf.push(task->handle);
        log::d("ctx forward_task to reap_OF\n");
    }
}

void io_context::handle_semaphore_release(task_info_ptr sem_release) noexcept {
    // TODO
}

bool io_context::try_submit(task_info_ptr task) noexcept {
    liburingcxx::SQEntry *sqe;
    using kind = task_info::task_type;

    switch (task->type) {
        case kind::co_spawn:
            forward_task(task);
            return true;

        case kind::semaphore_release:

            return true;

        // submit to ring
        default:
            if (ring.SQSpaceLeft() == 0) [[unlikely]] {
                log::d("ctx try_submit failed SQfull\n");
                return false; // SQRing is full
            }
            sqe = ring.getSQEntry();
            sqe->cloneFrom(*task->sqe);
            ring.submit();

            log::v("ctx submit to ring\n");

            return true;
    }
}

/**
 * @brief poll the submission swap zone
 * @return if submit_swap capacity might be healthy
 */
bool io_context::poll_submission() noexcept {
    // submit round
    if (!submit_swap.try_find_exist(s_cur)) { return false; }

    // TODO judge this memory order
    task_info_ptr const io_info =
        submit_swap.load(s_cur, std::memory_order_consume);
    submit_swap[s_cur] = nullptr;
    log::v("ctx poll_submission at [%u][%u]\n", s_cur.tid, s_cur.off);
    s_cur.next();

    if (try_submit(io_info)) [[likely]] {
        return true;
    } else {
        submit_overflow_buf.push(io_info);
        log::d("ctx poll_submission failed submit_OF\n");
        return false;
    }
}

bool io_context::try_clear_submit_overflow_buf() noexcept {
    if (submit_overflow_buf.empty()) return true;
    do {
        task_info_ptr const task = submit_overflow_buf.front();
        // OPTIMIZE impossible for task_type::co_spawn
        if (try_submit(task)) {
            submit_overflow_buf.pop();
        } else {
            log::d("ctx try_clear_submit (partially) failed\n");
            return false;
        }
    } while (!submit_overflow_buf.empty());
    log::d("ctx try_clear_submit succ\n");
    return true;
}

bool io_context::try_reap(std::coroutine_handle<> handle) noexcept {
    if (!reap_swap.try_find_empty(r_cur)) [[unlikely]] {
        log::d("ctx try_reap failed reap_swap is full\n");
        return false;
    }

    reap_swap.store(r_cur, handle, std::memory_order_release);
    log::v("ctx try_reap at [%u][%u]\n", r_cur.tid, r_cur.off);
    r_cur.next();
    return true;
}

/**
 * @brief poll the completion swap zone
 * @return if load exists and capacity of reap_swap might be healthy
 */
bool io_context::poll_completion() noexcept {
    // reap round
    liburingcxx::CQEntry *polling_cqe = ring.peekCQEntry();
    if (polling_cqe == nullptr) return false;

    log::v("ctx poll_completion found\n");

    task_info_ptr io_info =
        reinterpret_cast<task_info_ptr>(polling_cqe->getData());
    io_info->result = polling_cqe->getRes();
    ring.SeenCQEntry(polling_cqe);

    if (try_reap(io_info->handle)) [[likely]] {
        return true;
    } else {
        reap_overflow_buf.push(io_info->handle);
        log::d("ctx poll_completion failed reap_OF\n");
        return false;
    }
}

bool io_context::try_clear_reap_overflow_buf() noexcept {
    if (reap_overflow_buf.empty()) return true;
    do {
        std::coroutine_handle<> handle = reap_overflow_buf.front();
        // OPTIMIZE impossible for task_type::co_spawn
        if (try_reap(handle)) {
            reap_overflow_buf.pop();
        } else {
            log::d("ctx try_clear_reap (partially) failed\n");
            return false;
        }
    } while (!reap_overflow_buf.empty());
    log::d("ctx try_clear_reap succ\n");
    return true;
}

void io_context::init() noexcept {
    detail::this_thread.ctx = this;
    detail::this_thread.tid = std::thread::hardware_concurrency() - 1;
}

void io_context::probe() const {
    using namespace co_context::config;
    log::i("number of logic cores: %u\n", std::thread::hardware_concurrency());
    log::i("size of io_context: %u\n", sizeof(io_context));
    log::i("size of uring: %u\n", sizeof(uring));
    log::i("size of two swap_zone: %u\n", 2 * sizeof(submit_swap));
    log::i("atomic::is_lock_free: %d\n", std::atomic<void *>{}.is_lock_free());
    log::i("number of worker_threads: %u\n", worker_threads_number);
    log::i("swap_capacity per thread: %u\n", swap_capacity);
    log::i("size of single worker_meta: %u\n", sizeof(worker_meta));
}

inline void io_context::make_thread_pool() {
    for (int i = 0; i < config::worker_threads_number; ++i)
        worker[i].sharing.host_thread =
            std::thread{&worker_meta::run, worker + i, i, this};
}

void io_context::co_spawn(main_task entrance) {
    const uint32_t tid = detail::this_thread.tid;
    if (tid < config::worker_threads_number)
        return worker[tid].co_spawn(entrance);
    if (reap_swap.try_find_empty(r_cur)) [[likely]] {
        reap_swap.store(r_cur, entrance.get_io_info_ptr()->handle, std::memory_order_release);
        log::d("ctx co_spawn at [%u][%u]\n", r_cur.tid, r_cur.off);
        r_cur.next();
    } else {
        reap_overflow_buf.push(entrance.get_io_info_ptr()->handle);
        log::d("ctx co_spawn failed reap_OF\n");
    }
}

[[noreturn]] void io_context::run() {
    detail::this_thread.worker = nullptr;
#ifdef USE_CPU_AFFINITY
    detail::this_thread.tid = std::thread::hardware_concurrency() - 1;
    detail::set_cpu_affinity(detail::this_thread.tid);
#endif
    log::v("ctx making thread pool\n");
    make_thread_pool();
    log::v("ctx make_thread_pool end\n");

    while (!will_stop) [[likely]] {
            log::v("ctx polling\n");
            if (try_clear_submit_overflow_buf()) {
                for (uint8_t i = 0; i < config::submit_poll_rounds; ++i) {
                    if (!poll_submission()) break;
                }
            }

            if (try_clear_reap_overflow_buf()) {
                for (uint8_t i = 0; i < config::reap_poll_rounds; ++i) {
                    if (!poll_completion()) break;
                }
            }

            // std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

    this->stop();
}

} // namespace co_context
