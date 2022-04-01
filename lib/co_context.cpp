#include <mimalloc-2.0/mimalloc-new-delete.h>
#include "co_context.hpp"
#include "co_context/utility/set_cpu_affinity.hpp"
#include <atomic>

// fold level = 3 (ctrl+a, ctrl+k, ctrl+3 in vscode)
// unfold all (ctrl+a, ctrl+k, ctrl+j in vscode)

namespace co_context {

namespace detail {

    thread_local thread_meta this_thread;

    inline void worker_meta::swap_cur::next() noexcept {
        off = (off + 1) % co_context::config::swap_capacity;
    }

    inline bool
    worker_meta::swap_cur::try_find_empty(swap_zone &swap) noexcept {
        uint16_t i = 0;
        const uint32_t tid = this_thread.tid;
        while (i < config::swap_capacity && swap[tid][off] != nullptr) {
            ++i;
            next();
        }
        if (i == config::swap_capacity) [[unlikely]] {
            next();
            return false;
        }
        return true;
    }

    inline bool
    worker_meta::swap_cur::try_find_exist(swap_zone &swap) noexcept {
        uint16_t i = 0;
        const uint32_t tid = this_thread.tid;
        while (i < config::swap_capacity && swap[tid][off] == nullptr) {
            ++i;
            next();
        }
        if (i == config::swap_capacity) [[unlikely]] {
            next();
            return false;
        }
        return true;
    }

    inline void worker_meta::swap_cur::release(
        swap_zone &swap, task_info_ptr task) const noexcept {
        const uint32_t tid = this_thread.tid;
        std::atomic_store_explicit(
            reinterpret_cast<std::atomic<task_info_ptr> *>(&swap[tid][off]),
            task, std::memory_order_release);
    }

    inline void worker_meta::swap_cur::release_relaxed(
        swap_zone &swap, task_info_ptr task) const noexcept {
        const uint32_t tid = this_thread.tid;
        std::atomic_store_explicit(
            reinterpret_cast<std::atomic<task_info_ptr> *>(&swap[tid][off]),
            task, std::memory_order_relaxed);
    }

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
        auto &ctx = *this_thread.ctx;
        auto &cur = this->submit_cur;
        // std::cerr << cur.slot << " " << tid << cur.off << std::endl;
        if (cur.try_find_empty(ctx.submit_swap)) [[likely]] {
            cur.release(ctx.submit_swap, io_info);
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
        auto &r_cur = this->reap_cur;

        log::v("worker[%u] scheduling\n", tid);

        while (true) {
            // handle overflowed submission
            if (!this->submit_overflow_buf.empty()) {
                auto &s_cur = this->submit_cur;
                if (s_cur.try_find_empty(ctx.submit_swap)) {
                    task_info_ptr task = this->submit_overflow_buf.front();
                    this->submit_overflow_buf.pop();
                    s_cur.release_relaxed(ctx.submit_swap, task);
                    log::d(
                        "worker[%u] submit from OF_buf to [%u]\n", tid,
                        s_cur.off);
                    s_cur.next();
                }
            }

            if (r_cur.try_find_exist(ctx.reap_swap)) {
                const task_info_ptr io_info = ctx.reap_swap[tid][r_cur.off];
                ctx.reap_swap[tid][r_cur.off] = nullptr;
                log::d("worker[%u] found [%u]\n", tid, r_cur.off);
                r_cur.next();
                // printf("get task!\n");
                return io_info->handle;
            }
            // TODO consider tid_hint here
        }
    }

} // namespace detail

inline void io_context::ctx_swap_cur::next() noexcept {
    if (++tid == config::worker_threads_number) [[unlikely]] {
        tid = 0;
        off = (off + 1) % config::swap_capacity;
    }
}

inline bool
io_context::ctx_swap_cur::try_find_empty(const swap_zone &swap) noexcept {
    constexpr uint32_t swap_size = sizeof(swap_zone) / sizeof(task_info_ptr);
    int i = 0;
    while (i < swap_size && swap[tid][off] != nullptr) {
        ++i;
        next();
    }
    if (i != swap_size) [[likely]] {
        return true;
    } else {
        next();
        return false;
    }
}

inline bool
io_context::ctx_swap_cur::try_find_exist(const swap_zone &swap) noexcept {
    constexpr uint32_t swap_size = sizeof(swap_zone) / sizeof(task_info_ptr);
    int i = 0;
    while (i < swap_size && swap[tid][off] == nullptr) {
        ++i;
        next();
    }
    if (i != swap_size) [[likely]] {
        return true;
    } else {
        next();
        return false;
    }
}

// release the task into the swap zone
inline void io_context::ctx_swap_cur::release(
    swap_zone &swap, task_info_ptr task) const noexcept {
    std::atomic_store_explicit(
        reinterpret_cast<std::atomic<task_info_ptr> *>(&swap[tid][off]), task,
        std::memory_order_release);
}

inline void io_context::ctx_swap_cur::release_relaxed(
    swap_zone &swap, task_info_ptr task) const noexcept {
    std::atomic_store_explicit(
        reinterpret_cast<std::atomic<task_info_ptr> *>(&swap[tid][off]), task,
        std::memory_order_relaxed);
}

void io_context::forward_task(task_info_ptr task) noexcept {
    // TODO optimize scheduling strategy
    if (r_cur.try_find_empty(reap_swap)) [[likely]] {
        reap_swap[r_cur.tid][r_cur.off] = task;
        log::v("ctx forward_task to [%u][%u]\n", r_cur.tid, r_cur.off);
        r_cur.next();
    } else {
        reap_overflow_buf.push(task);
        log::d("ctx forward_task to reap_OF\n");
    }
}

bool io_context::try_submit(task_info_ptr task) noexcept {
    if (task->type == task_info::task_type::co_spawn) {
        forward_task(task);
        return true;
    }

    if (ring.SQSpaceLeft() == 0) [[unlikely]] {
        log::d("ctx try_submit failed SQfull\n");
        return false; // SQRing is full
    }

    liburingcxx::SQEntry *sqe = ring.getSQEntry();

    sqe->cloneFrom(*task->sqe);
    ring.submit();

    log::v("ctx submit to ring\n");

    return true;
}

/**
 * @brief poll the submission swap zone
 * @return if submit_swap capacity might be healthy
 */
bool io_context::poll_submission() noexcept {
    // submit round
    if (!s_cur.try_find_exist(submit_swap)) { return false; }

    task_info_ptr const io_info = submit_swap[s_cur.tid][s_cur.off];
    submit_swap[s_cur.tid][s_cur.off] = nullptr;
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
        task_info_ptr task = submit_overflow_buf.front();
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

bool io_context::try_reap(task_info_ptr task) noexcept {
    if (!r_cur.try_find_empty(reap_swap)) [[unlikely]] {
        log::d("ctx try_reap failed reap_swap is full\n");
        return false;
    }

    r_cur.release(reap_swap, task);
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

    if (try_reap(io_info)) [[likely]] {
        return true;
    } else {
        reap_overflow_buf.push(io_info);
        log::d("ctx poll_completion failed reap_OF\n");
        return false;
    }
}

bool io_context::try_clear_reap_overflow_buf() noexcept {
    if (reap_overflow_buf.empty()) return true;
    do {
        task_info_ptr task = reap_overflow_buf.front();
        // OPTIMIZE impossible for task_type::co_spawn
        if (try_reap(task)) {
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
    memset(submit_swap, 0, sizeof(submit_swap));
    memset(reap_swap, 0, sizeof(reap_swap));
    detail::this_thread.ctx = this;
    detail::this_thread.tid = std::thread::hardware_concurrency() - 1;
}

void io_context::probe() const {
    using namespace co_context::config;
    log::i("number of logic cores: %u\n", std::thread::hardware_concurrency());
    log::i("size of io_context: %u\n", sizeof(io_context));
    log::i("size of uring: %u\n", sizeof(uring));
    log::i("size of two swap_zone: %u\n", 2 * sizeof(swap_zone));
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
    if (r_cur.try_find_empty(reap_swap)) [[likely]] {
        r_cur.release(reap_swap, entrance.get_io_info_ptr());
        log::d("ctx co_spawn at [%u][%u]\n", r_cur.tid, r_cur.off);
        r_cur.next();
    } else {
        reap_overflow_buf.push(entrance.get_io_info_ptr());
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
