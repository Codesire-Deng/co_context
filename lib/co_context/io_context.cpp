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

[[deprecated]] unsigned
compress_sqe(const io_context *self, const liburingcxx::SQEntry *sqe) noexcept {
    return sqe - self->sqes_addr;
}

namespace detail {

    thread_local thread_meta this_thread;

    inline void
    worker_meta::init(const int thread_index, io_context *const context) {
        /*
        assert(submit_overflow_buf.empty());
        */
        detail::this_thread.ctx = context;
        detail::this_thread.worker = this;
        detail::this_thread.tid = thread_index;
        this->tid = thread_index;
        this->submit_swap_ptr = &context->submit_swap[thread_index];
        this->reap_swap_ptr = &context->reap_swap[thread_index];
        log::w("worker[%u] runs on %u\n", this->tid, gettid());
#ifdef CO_CONTEXT_USE_CPU_AFFINITY
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

    liburingcxx::SQEntry *worker_meta::get_free_sqe() noexcept {
        // may acquire the cur.head
        auto &swap = *this->submit_swap_ptr;
        const auto &cur = this->sharing.submit_cur;
        // TODO check this memory order
        cur.wait_for_available();
        /*
        while (!cur.is_available_load_head()) [[unlikely]] {
                log::d(
                    "worker[%u] sleep because get_free_sqe overflow! "
                    "(io_context is too slow)\n",
                    tid
                );
                std::this_thread::sleep_for(std::chrono::microseconds(1));
            }
        */
        assert(swap[local_submit_tail & cur.mask].available_sqe != nullptr);
        swap[local_submit_tail & cur.mask].address = 0UL;
        return swap[local_submit_tail++ & cur.mask].available_sqe;
    }

    void worker_meta::submit_sqe() noexcept {
        [[maybe_unused]] const auto &swap = *this->submit_swap_ptr;
        auto &cur = this->sharing.submit_cur;
        // assert(swap[cur.tail()].sqe == info.sqe);
        // cur.push(1);
        cur.store_raw_tail(local_submit_tail);
        log::v(
            "worker[%u] submit_sqe to [%u]\n", this_thread.tid,
            (local_submit_tail - 1U) & cur.mask
        );
    }

    void worker_meta::submit_non_sqe(uintptr_t typed_task) noexcept {
        auto &swap = *this->submit_swap_ptr;
        auto &cur = this->sharing.submit_cur;
        // std::cerr << cur.slot << " " << tid << cur.off << std::endl;
        cur.wait_for_available();
        /*
        while (!cur.is_available_load_head()) [[unlikely]] {
                log::d(
                    "worker[%u] sleep because submit_non_sqe overflow! "
                    "(io_context is too slow)\n",
                    tid
                );
                std::this_thread::sleep_for(std::chrono::microseconds(1));
            }
        */

        swap[cur.tail()].address = typed_task;
        cur.push(1);
        log::v(
            "worker[%u] submit_non_sqe at [%u]\n", this_thread.tid,
            local_submit_tail & cur.mask
        );
        ++local_submit_tail;
        assert(local_submit_tail == cur.raw_tail());

        /*
        if (submit_overflow_buf.empty() && cur.is_available()) [[likely]] {
            log::v("worker[%u] submit at [%u]\n", tid, cur.tail());
            swap.push(cur, info);
        } else {
            this->submit_overflow_buf.push(info);
            log::d("worker[%u] submit to OF (io_context is too slow)\n", tid);
        }
        */
    }

    /*
    void worker_meta::try_clear_submit_overflow_buf() noexcept {
        auto &swap = *this->submit_swap_ptr;
        auto &cur = this->sharing.submit_cur;
        while (!this->submit_overflow_buf.empty() && cur.is_available()) {
            submit_info info = this->submit_overflow_buf.front();
            this->submit_overflow_buf.pop();
            log::d("worker[%u] submit from OF_buf to [%u]\n", tid, cur.tail());
            swap.push(cur, info);
        }
    }
    */

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

            cur.wait_for_not_empty();

            /*
            if (!cur.is_empty_load_tail())
            */
            {
                const reap_info info = reap_swap[cur.head()];
                log::v("worker[%u] found [%u]\n", tid, cur.head());
                cur.pop();
                if (!info.is_co_spawn()) [[likely]] {
                    info.io_info->result = info.result;
                    // info.io_info->flags = info.flags;
                    if constexpr (config::enable_link_io_result) {
                        if (info.io_info->type
                            == task_info::task_type::lazy_link_sqe)
                            continue;
                    }
                    return info.io_info->handle;
                } else {
                    return info.handle;
                }
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

[[deprecated, nodiscard]] bool
io_context::is_sqe(const liburingcxx::SQEntry *suspect) const noexcept {
    return static_cast<size_t>(suspect - this->sqes_addr)
           < this->sqring_entries;
}

void io_context::forward_task(std::coroutine_handle<> handle) noexcept {
    // TODO optimize scheduling strategy
    if (try_find_reap_worker_relaxed()) [[likely]] {
        auto &cur = worker[r_cur].sharing.reap_cur;
        log::v(
            "ctx forward_task to [%u][%u]: %lx\n", r_cur, cur.tail(),
            handle.address()
        );
        reap_swap[r_cur].push(cur, detail::reap_info{handle});
        cur_next(r_cur);
    } else {
        /**
         * This means workers are too slow.
         */
        reap_overflow_buf.push(detail::reap_info{handle});
        log::d("ctx forward_task to reap_OF (workers are too slow)\n");
    }
}

} // namespace co_context

// #include "co_context/detail/sqe_task_meta.hpp" // deprecated
#include "co_context/detail/cv_task_meta.hpp"
#include "co_context/detail/sem_task_meta.hpp"

namespace co_context {

void io_context::handle_semaphore_release(task_info *sem_release) noexcept {
    counting_semaphore &sem = *detail::as_counting_semaphore(sem_release);
    const counting_semaphore::T update =
        as_atomic(sem_release->update).exchange(0, std::memory_order_relaxed);
    if (update == 0) [[unlikely]]
        return;

    counting_semaphore::T done = 0;
    std::coroutine_handle<> handle;
    while (done < update && bool(handle = sem.try_release())) {
        log::v(
            "ctx handle_semaphore_release: forwarding %lx\n", handle.address()
        );
        forward_task(handle);
        ++done;
    }

    // TODO determine this memory order
    sem.counter.fetch_add(update, std::memory_order_acq_rel);
}

void io_context::handle_condition_variable_notify(task_info *cv_notify
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

bool io_context::try_find_submit_worker_relaxed() noexcept {
    for (config::tid_t i = 0; i < config::worker_threads_number; ++i) {
        if (!this->worker[s_cur].sharing.submit_cur.is_empty()) return true;
        cur_next(s_cur);
    }
    return false;
}

[[deprecated]] bool io_context::try_find_submit_worker_acquire() noexcept {
    for (config::tid_t i = 0; i < config::worker_threads_number; ++i) {
        if (!this->worker[s_cur].sharing.submit_cur.is_empty_load_tail())
            return true;
        cur_next(s_cur);
    }
    return false;
}

[[deprecated]] bool io_context::try_find_reap_worker_acquire() noexcept {
    for (config::tid_t i = 0; i < config::worker_threads_number; ++i) {
        if (this->worker[r_cur].sharing.reap_cur.is_available_load_head())
            return true;
        cur_next(r_cur);
    }
    return false;
}

bool io_context::try_find_reap_worker_relaxed() noexcept {
    for (config::tid_t i = 0; i < config::worker_threads_number; ++i) {
        if (this->worker[r_cur].sharing.reap_cur.is_available()) return true;
        cur_next(r_cur);
    }
    return false;
}

void io_context::try_submit(detail::submit_info &info) noexcept {
    // lazy_sqe or eager_sqe
    if (info.address == 0UL) [[likely]] {
        // submit to ring
        ++requests_to_reap;
        log::v("ctx ring.submit(), requests_to_reap=%d...\n", requests_to_reap);
        need_ring_submit = true;
        ring.appendSQEntry(info.submit_sqe);
        auto *const victim_sqe = ring.getSQEntry();
        assert(victim_sqe != nullptr && "ring.getSQEntry() returns nullptr!");
        info.available_sqe = victim_sqe;
        log::v("ctx ring.submit()...OK\n");
        return;
    } else {
        using task_type = task_info::task_type;
        using submit_type = detail::submit_type;
        task_info *const io_info = detail::raw_task_info_ptr(info.address);

        // PERF May call cur.pop() to make worker start sooner.
        switch (uint32_t(info.address) & 0x7U) {
            case submit_type::co_spawn:
                forward_task(std::coroutine_handle<>::from_address(info.ptr));
                return;
            case submit_type::sem_rel:
                handle_semaphore_release(io_info);
                return;
            case submit_type::cv_notify:
                handle_condition_variable_notify(io_info);
                return;
            default:
                log::e("submit_info.address==%lx\n", info.address);
                assert(false && "try_submit(): unknown task_type");
                return;
        }
    }
}

/**
 * @brief poll the submission swap zone
 * @return if submit_swap capacity might be healthy
 */
bool io_context::poll_submission() noexcept {
    // submit round
    if (!try_find_submit_worker_relaxed()) { return false; }
    auto &cur = worker[s_cur].sharing.submit_cur;
    auto &swap = submit_swap[s_cur];

    log::v("ctx found submission start from [%u][%u]\n", s_cur, cur.head());

    auto head{cur.raw_head()};
    auto tail{cur.load_raw_tail()};
    const auto submit_count = tail - head;

    need_ring_submit = false;

    for (; head != tail; ++head) {
        const auto i{head & cur.mask};
        // log::d("ctx poll_submission at [%u][%u]\n", s_cur, i);
        try_submit(swap[i]); // always success (currently).
    }

    cur.pop_notify(submit_count);
    cur_next(s_cur);

    if (need_ring_submit) { ring.submit(); }

    return true;
}

/*
bool io_context::try_clear_submit_overflow_buf() noexcept {
    if (submit_overflow_buf.empty()) return true;
    do {
        task_info *const task<> = submit_overflow_buf.front();
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
*/

bool io_context::try_reap(detail::reap_info info) noexcept {
    if (!try_find_reap_worker_relaxed()) [[unlikely]] {
        log::d("ctx try_reap failed reap_swap is full\n");
        return false;
    }

    auto &cur = worker[r_cur].sharing.reap_cur;

    log::v("ctx try_reap at [%u][%u]\n", r_cur, cur.tail());
    reap_swap[r_cur].push_notify(cur, info);
    cur_next(r_cur);
    return true;
}

inline void io_context::reap_or_overflow(detail::reap_info info) noexcept {
    if (!try_reap(info)) [[unlikely]] {
        reap_overflow_buf.push(info);
        log::d("ctx poll_completion failed reap_OF (workers are too slow)\n");
    }
}

static bool eager_io_need_awake(detail::task_info *io_info) {
    using io_state_t = eager::io_state_t;
    const io_state_t old_state =
        as_atomic(io_info->eager_io_state)
            // .fetch_or(eager::io_ready, std::memory_order_seq_cst);
            .fetch_or(eager::io_ready, std::memory_order_release);
    if (old_state & eager::io_detached) delete io_info;
    return old_state & eager::io_wait;
}

/**
 * @brief poll the completion swap zone
 * @return if load exists and capacity of reap_swap might be healthy
 */
inline void io_context::poll_completion() noexcept {
    // reap round
    const liburingcxx::CQEntry *const polling_cqe = ring.peekCQEntry();
    if (polling_cqe == nullptr) return;

    --requests_to_reap;
    log::v("ctx poll_completion found, remaining=%d\n", requests_to_reap);

    const uint64_t user_data = polling_cqe->getData();
    const int32_t result = polling_cqe->getRes();
    [[maybe_unused]] const uint32_t flags = polling_cqe->getFlags();
    ring.SeenCQEntry(polling_cqe);
    assert(flags != detail::reap_info::co_spawn_flag);

    using task_type = task_info::task_type;

    if constexpr (config::enable_eager_io) {
        if (user_data & uint64_t(task_type::eager_sqe)) [[unlikely]] {
            task_info *const eager_io_info = reinterpret_cast<task_info *>(
                user_data ^ uint64_t(task_type::eager_sqe)
            );
            eager_io_info->result = result;
            if (eager_io_need_awake(eager_io_info)) [[likely]]
                reap_or_overflow(detail::reap_info{eager_io_info->handle});
            return;
        }
        // must be lazy_sqe or lazy_link_sqe here
    }

    if constexpr (!config::enable_link_io_result) {
        // if link_io_result is not enabled, we can skip the lazy_link_sqe.
        if (user_data & uint64_t(task_type::lazy_link_sqe)) return;
    }

    task_info *const io_info = detail::raw_task_info_ptr(user_data);
    reap_or_overflow(detail::reap_info{io_info, result, flags});
}

bool io_context::try_clear_reap_overflow_buf() noexcept {
    if (reap_overflow_buf.empty()) return true;
    do {
        const detail::reap_info info = reap_overflow_buf.front();
        // OPTIMIZE impossible for task_type::co_spawn
        if (try_reap(info)) {
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
    // assert(submit_overflow_buf.empty());
    assert(reap_overflow_buf.empty());
    assert(this->sqring_entries == ring.getSQRingEntries());
    {
        const unsigned actual_sqring_size = this->sqring_entries;
        const unsigned expect_sqring_size =
            config::worker_threads_number * config::swap_capacity * 2;
        if (actual_sqring_size < expect_sqring_size) {
            log::e(
                "io_context::init(): "
                "Entries inside the ring are not enough!\n"
                "Actual=%u, expect=%u\n",
                actual_sqring_size, expect_sqring_size
            );
            exit(1);
        }
    }
    this->sqes_addr = ring.__getSqes();
    for (unsigned i = 0; i < config::worker_threads_number; ++i) {
        for (unsigned j = 0; j < config::swap_capacity; ++j) {
            submit_swap[i][j] = detail::submit_info{
                .address = 0UL, .available_sqe = ring.getSQEntry()};
        }
    }
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
        worker[i].host_thread =
            std::thread{&worker_meta::worker_run, worker + i, i, this};
}

void io_context::co_spawn(task<void> &&entrance) {
    assert(detail::this_thread.worker == nullptr);
    forward_task(entrance.get_handle());
    entrance.detach();
}

void io_context::co_spawn(std::coroutine_handle<> entrance) {
    assert(detail::this_thread.worker == nullptr);
    forward_task(entrance);
}

[[noreturn]] void io_context::run() {
#ifdef CO_CONTEXT_USE_CPU_AFFINITY
    detail::set_cpu_affinity(detail::this_thread.tid);
#endif
    log::w("io_context runs on %d\n", gettid());
    log::v("ctx making thread pool\n");
    make_thread_pool();
    log::v("ctx make_thread_pool end\n");

    if constexpr (config::use_standalone_completion_poller)
        std::thread{[this] {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            log::w("ctx completion_poller runs on %d\n", gettid());
            while (true) {
                if (this->ring.CQReadyAcquire()) { poll_completion(); }
            }
        }}.detach();

    while (!will_stop) [[likely]] {
            // log::v("ctx polling\n");
            // if (try_clear_submit_overflow_buf()) {
            if constexpr (config::submit_poll_rounds > 1)
                for (uint8_t i = 0; i < config::submit_poll_rounds; ++i) {
                    // log::v("ctx poll_submission\n");
                    if (!poll_submission()) break;
                }
            else
                poll_submission();
            // }

            if constexpr (!config::use_standalone_completion_poller) {
                // if (requests_to_reap > 1)
                //     log::w("abnormal requests_to_reap=%d\n",
                //     requests_to_reap);
                // TODO judge the memory order (relaxed may cause bugs)
                if (requests_to_reap > 0 && ring.CQReadyRelaxed())
                    poll_completion();
            }
            // if (try_clear_reap_overflow_buf()) {
            // while (requests_to_reap != 0) {
            // for (uint8_t i = 0; i < config::reap_poll_rounds; ++i) {
            // log::v("ctx poll_completion\n");
            // if (!poll_completion()) break;
            // }
            // }
            // }
            // std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

    this->stop();
}

} // namespace co_context
