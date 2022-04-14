#include <mimalloc-2.0/mimalloc-new-delete.h>
#include "co_context/io_context.hpp"
#include "co_context/utility/set_cpu_affinity.hpp"
#include "co_context/co/semaphore.hpp"
#include "co_context/co/condition_variable.hpp"
#include "co_context/detail/eager_io_state.hpp"
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
        this->tid = thread_index;
        this->submit_swap_ptr = &context->submit_swap[thread_index];
        this->reap_swap_ptr = &context->reap_swap[thread_index];
        log::w("worker[%u] runs on %u\n", this->tid, gettid());
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

    void worker_meta::submit(task_info_ptr io_info) noexcept {
        auto &swap = *this->submit_swap_ptr;
        auto &cur = this->sharing.submit_cur;
        // std::cerr << cur.slot << " " << tid << cur.off << std::endl;
        if (submit_overflow_buf.empty() && cur.is_available()) [[likely]] {
            log::v("worker[%u] submit at [%u]\n", tid, cur.tail());
            swap.push(cur, io_info);
        } else {
            this->submit_overflow_buf.push(io_info);
            log::d("worker[%u] submit to OF (io_context is too slow)\n", tid);
        }
    }

    void worker_meta::try_clear_submit_overflow_buf() noexcept {
        auto &swap = *this->submit_swap_ptr;
        auto &cur = this->sharing.submit_cur;
        while (!this->submit_overflow_buf.empty() && cur.is_available()) {
            task_info_ptr task = this->submit_overflow_buf.front();
            this->submit_overflow_buf.pop();
            log::d("worker[%u] submit from OF_buf to [%u]\n", tid, cur.tail());
            swap.push(cur, task);
        }
    }

    inline std::coroutine_handle<> worker_meta::schedule() noexcept {
        auto &reap_swap = *this->reap_swap_ptr;
        auto &cur = this->sharing.reap_cur;

        log::v("worker[%u] scheduling\n", tid);

        while (true) {
            /**
             * handle overflowed submission.
             * This implies the io_context is too slow.
             */
            // BUG
            // try_clear_submit_overflow_buf();

            if (!cur.is_empty_load_head()) {
                // TODO judge this memory order
                const std::coroutine_handle<> handle = reap_swap[cur.head()];
                log::v("worker[%u] found [%u]\n", tid, cur.head());
                cur.pop();
                return handle;
            }
            // TODO consider tid_hint here
        }
    }

    inline void
    worker_meta::worker_run(const int thread_index, io_context *const context) {
        init(thread_index, context);
        log::v("worker[%d] run...\n", thread_index);

        while (true) {
            auto coro = this->schedule();
            log::v(
                "worker[%d] found coro %lx to run\n", thread_index,
                coro.address()
            );
            coro.resume();
            log::v("worker[%d] idle\n", thread_index);
        }
    }

} // namespace detail

void io_context::forward_task(std::coroutine_handle<> handle) noexcept {
    // TODO optimize scheduling strategy
    if (try_find_reap_worker()) [[likely]] {
        auto &cur = worker[r_cur].sharing.reap_cur;
        log::v(
            "ctx forward_task to [%u][%u]: %lx\n", r_cur, cur.tail(),
            handle.address()
        );
        reap_swap[r_cur].push(cur, handle);
        cur_next(r_cur);
    } else {
        /**
         * This means workers are too slow.
         */
        reap_overflow_buf.push(handle);
        log::d("ctx forward_task to reap_OF (workers are too slow)\n");
    }
}

} // namespace co_context

#include "co_context/detail/sqe_task_meta.hpp"
#include "co_context/detail/cv_task_meta.hpp"
#include "co_context/detail/sem_task_meta.hpp"

namespace co_context {

void io_context::handle_semaphore_release(task_info_ptr sem_release) noexcept {
    counting_semaphore &sem = *detail::as_counting_semaphore(sem_release);
    const counting_semaphore::T update =
        as_atomic(sem_release->update).exchange(0, std::memory_order_relaxed);
    if (update == 0) [[unlikely]]
        return;

    counting_semaphore::T done = 0;
    std::coroutine_handle<> handle;
    while (done < update && bool(handle = sem.try_release())) {
        log::d(
            "ctx handle_semaphore_release: forwarding %lx\n", handle.address()
        );
        forward_task(handle);
        ++done;
    }

    // TODO determine this memory order
    sem.counter.fetch_add(update, std::memory_order_acq_rel);
}

void io_context::handle_condition_variable_notify(task_info_ptr cv_notify
) noexcept {
    condition_variable &cv = *detail::as_condition_variable(cv_notify);
    const condition_variable::T notify_counter =
        as_atomic(cv_notify->notify_counter)
            .exchange(0, std::memory_order_relaxed);

    if (notify_counter == 0) [[unlikely]]
        return;

    if (cv.awaiting.load(std::memory_order_relaxed) != nullptr)
        cv.to_resume_fetch_all();

    condition_variable::T done = 0;
    const bool is_nofity_all = notify_counter & cv.notify_all_flag;
    while ((is_nofity_all || done < notify_counter)
           && cv.to_resume_head != nullptr) {
        mutex::lock_awaiter &to_awake = cv.to_resume_head->lock_awaken_handle;
        // let the coroutine get the lock
        if (!to_awake.register_awaiting())
            // lock succ, wakeup
            forward_task(to_awake.get_coroutine());
        // lock failed, just wait for another mutex.unlock()

        cv.to_resume_head = cv.to_resume_head->next;
        ++done;
    }

    if (cv.to_resume_head == nullptr) cv.to_resume_tail = nullptr;
}

bool io_context::try_find_submit_worker() noexcept {
    for (config::tid_t i = 0; i < config::worker_threads_number; ++i) {
        if (!this->worker[s_cur].sharing.submit_cur.is_empty()) return true;
        cur_next(s_cur);
    }
    return false;
}

bool io_context::try_find_reap_worker() noexcept {
    for (config::tid_t i = 0; i < config::worker_threads_number; ++i) {
        if (this->worker[r_cur].sharing.reap_cur.is_available()) return true;
        cur_next(r_cur);
    }
    return false;
}

bool io_context::try_submit(task_info_ptr task) noexcept {
    liburingcxx::SQEntry *sqe;
    using task_type = task_info::task_type;

    switch (task->type) {
        // submit to ring
        case task_type::lazy_sqe:
        case task_type::eager_sqe:
            if (ring.SQSpaceLeft() == 0) [[unlikely]] {
                log::d("ctx try_submit failed SQfull\n");
                return false; // SQRing is full
            }
            sqe = ring.getSQEntry();
            log::v("ctx get SQEntry, clone sqe...\n");
            assert(sqe != nullptr);
            sqe->cloneFrom(detail::as_sqe_task_meta(task)->sqe);
            log::v(
                "ctx ring.submit() sqe at %lx...\n",
                &detail::as_sqe_task_meta(task)->sqe
            );
            ring.submit();
            // ++requests_to_reap;
            log::v("ctx ring.submit()...OK\n");
            return true;

        case task_type::co_spawn:
            forward_task(task->handle);
            return true;

        case task_type::semaphore_release:
            handle_semaphore_release(task);
            return true;

        case task_type::condition_variable_notify:
            handle_condition_variable_notify(task);
            return true;

        default:
            log::e("task->type==%u\n", task->type);
            assert(false && "try_submit(): unknown task_type");
            return false;
    }
}

/**
 * @brief poll the submission swap zone
 * @return if submit_swap capacity might be healthy
 */
bool io_context::poll_submission() noexcept {
    // submit round
    if (!try_find_submit_worker()) { return false; }
    auto &cur = worker[s_cur].sharing.submit_cur;

    assert(!cur.is_empty());
    task_info_ptr const io_info = submit_swap[s_cur].front(cur);
    log::v("ctx poll_submission at [%u][%u]\n", s_cur, cur.head());
    cur.pop();
    cur_next(s_cur);

    if (try_submit(io_info)) [[likely]] {
        return true;
    } else {
        submit_overflow_buf.push(io_info);
        log::d("ctx poll_submission failed submit_OF (io_uring is too slow)\n");
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
    if (!try_find_reap_worker()) [[unlikely]] {
        log::d("ctx try_reap failed reap_swap is full\n");
        return false;
    }

    auto &cur = worker[r_cur].sharing.reap_cur;

    log::v("ctx try_reap at [%u][%u]\n", r_cur, cur.tail());
    reap_swap[r_cur].push(cur, handle);
    cur_next(r_cur);
    return true;
}

static bool eager_io_need_awake(detail::task_info_ptr io_info) {
    using io_state_t = eager::io_state_t;
    const io_state_t old_state =
        as_atomic(io_info->eager_io_state)
            // .fetch_or(eager::io_ready, std::memory_order_seq_cst);
            .fetch_or(eager::io_ready, std::memory_order_release);
    if (old_state & eager::io_detached)
        delete detail::as_sqe_task_meta(io_info);
    return old_state & eager::io_wait;
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
    // --requests_to_reap;

    using task_type = task_info::task_type;

    switch (io_info->type) {
        case task_type::lazy_sqe:
            [[fallthrough]];
        case task_type::co_spawn:
            break;

        case task_type::eager_sqe:
            if (eager_io_need_awake(io_info)) [[likely]] {
                break;
            } else {
                return true;
            }

        default:
            log::e("io_info->type==%u\n", io_info->type);
            assert(false && "poll_completion(): unknown task_type");
            break;
    }

    if (try_reap(io_info->handle)) [[likely]] {
        return true;
    } else {
        reap_overflow_buf.push(io_info->handle);
        log::d("ctx poll_completion failed reap_OF (workers are too slow)\n");
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
    // TODO support multiple io_context in one thread?
    detail::this_thread.ctx = this;
    detail::this_thread.worker = nullptr;
    detail::this_thread.tid = std::thread::hardware_concurrency() - 1;
    assert(submit_overflow_buf.empty());
    assert(reap_overflow_buf.empty());
    // probe();
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
    log::i("size of worker.sharing: %u\n", sizeof(worker_meta::sharing_zone));
}

inline void io_context::make_thread_pool() {
    for (config::tid_t i = 0; i < config::worker_threads_number; ++i)
        worker[i].sharing.host_thread =
            std::thread{&worker_meta::worker_run, worker + i, i, this};
}

void io_context::co_spawn(main_task entrance) {
    assert(detail::this_thread.worker == nullptr);
    forward_task(entrance.get_io_info_ptr()->handle);
}

[[noreturn]] void io_context::run() {
#ifdef USE_CPU_AFFINITY
    detail::set_cpu_affinity(detail::this_thread.tid);
#endif
    log::w("io_context runs on %d\n", gettid());
    log::v("ctx making thread pool\n");
    make_thread_pool();
    log::v("ctx make_thread_pool end\n");

    while (!will_stop) [[likely]] {
            log::v("ctx polling\n");
            // if (try_clear_submit_overflow_buf()) {
            for (uint8_t i = 0; i < config::submit_poll_rounds; ++i) {
                log::v("ctx poll_submission\n");
                if (!poll_submission()) break;
            }
            // }

            // if (try_clear_reap_overflow_buf()) {
            for (uint8_t i = 0;
                 i < config::reap_poll_rounds /*&& requests_to_reap > 0*/;
                 ++i) {
                log::v("ctx poll_completion\n");
                if (!poll_completion()) break;
            }
            // }

            // std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

    this->stop();
}

} // namespace co_context
