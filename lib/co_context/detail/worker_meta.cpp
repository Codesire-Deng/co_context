#include "co_context/config.hpp"
#include "co_context/co/condition_variable.hpp"
#include "co_context/co/semaphore.hpp"
#include "co_context/compat.hpp"
#include "co_context/detail/cv_task_meta.hpp"
#include "co_context/detail/eager_io_state.hpp"
#include "co_context/detail/sem_task_meta.hpp"
#include "co_context/detail/thread_meta.hpp"
#include "co_context/io_context.hpp"
#include "co_context/log/log.hpp"
#include "co_context/utility/set_cpu_affinity.hpp"
#include "uring/cq_entry.hpp"
#include "uring/uring_define.hpp"
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <unistd.h>

namespace co_context::detail {

thread_local thread_meta this_thread;

void worker_meta::init(unsigned io_uring_entries) {
    this->ctx_id = this_thread.ctx_id;
    this_thread.worker = this;

    ring.init(io_uring_entries);

    if (!check_init(io_uring_entries)) {
        std::exit(1);
    }

#ifdef CO_CONTEXT_USE_CPU_AFFINITY
#error TODO: this part should be refactored.
    if constexpr (config::worker_threads_number > 0) {
        const unsigned logic_cores = std::thread::hardware_concurrency();
        if constexpr (config::is_using_hyper_threading) {
            if (thread_index * 2 < logic_cores) {
                detail::set_cpu_affinity(thread_index * 2);
            } else {
                detail::set_cpu_affinity(thread_index * 2 % logic_cores + 1);
            }
        } else {
            detail::set_cpu_affinity(thread_index);
        }
    }
#endif
    log::i("io_context[%u] init a worker\n", detail::this_thread.ctx_id);
}

bool worker_meta::check_init(unsigned expect_sqring_size) const noexcept {
    const unsigned actual_sqring_size = ring.get_sq_ring_entries();

    if (actual_sqring_size < expect_sqring_size) {
        log::e(
            "worker_meta::init_check: "
            "Entries inside the ring are not enough!\n"
            "Actual=%u, expect=%u\n",
            actual_sqring_size, expect_sqring_size
        );
        return false;
    }

    if (actual_sqring_size != expect_sqring_size) {
        log::w(
            "worker_meta::init_check: "
            "sqring_size mismatch: actual = %u, expect = %u\n ",
            actual_sqring_size, expect_sqring_size
        );
    }

    return true;
}

liburingcxx::sq_entry *worker_meta::get_free_sqe() noexcept {
    log::v("worker[%u] get_free_sqe\n", this_thread.ctx_id);
    ++requests_to_reap; // NOTE may required no reap or required multi-reap.
    need_ring_submit = true;

    auto *sqe = this->ring.get_sq_entry();
    assert(sqe != nullptr);
    return sqe;
}

void worker_meta::submit_sqe() const noexcept {
    log::v("worker[%u] submit_sqe(s)\n", this_thread.ctx_id);
}

void worker_meta::submit_non_sqe(uintptr_t typed_task) noexcept {
    auto &swap = this->submit_swap;
    auto &cur = this->submit_cur;

    assert(cur.is_available());
    const auto tail = cur.tail();

    swap[tail].address = typed_task;
    log::v("worker[%u] submit_non_sqe at [%u]\n", this_thread.ctx_id, tail);
    cur.push(1);
}

std::coroutine_handle<> worker_meta::schedule() noexcept {
    auto &cur = this->reap_cur;

    log::v("worker[%u] try scheduling\n", this->ctx_id);
    assert(!cur.is_empty());

    const cur_t head = cur.head();
    const reap_info info = reap_swap[head];
    cur.pop();
    if (!info.is_co_spawn()) [[likely]] {
        info.io_info->result = info.result;
        // info.io_info->flags = info.flags;
        if constexpr (config::enable_link_io_result) {
            if (info.io_info->type == task_info::task_type::lazy_link_sqe) {
                log::v("worker[%u] found link_io, skip\n", ctx_id, head);
                return nullptr;
            }
        }
        log::v(
            "worker[%u] found io(%lx) to resume at reap_swap[%u]\n", ctx_id,
            info.io_info->handle, head
        );
        return info.io_info->handle;
    } else {
        log::v(
            "worker[%u] found co_spawn(%lx) to resume at reap_swap[%u]\n",
            ctx_id, info.handle, head
        );
        return info.handle;
    }
}

void worker_meta::work_once() {
    log::v("worker[%u] work_once...\n", this->ctx_id);

    const auto coro = this->schedule();
    if (bool(coro)) [[likely]] {
        coro.resume();
    }
    log::v("worker[%u] work_once finished\n", this->ctx_id);
}

void worker_meta::poll_submission() noexcept {
    auto &cur = submit_cur;
    auto &swap = submit_swap;

    // submit sqes
    if (need_ring_submit) [[likely]] {
        uint8_t will_wait =
            uint8_t(!has_task_ready()) & uint8_t(cur.is_empty());
        [[maybe_unused]] int res = ring.submit_and_wait(will_wait);
        assert(res >= 0 && "exception at uring::submit");
        need_ring_submit = false;
    }

    if (cur.is_empty()) {
        log::v("worker[%u] found NO non-sqe submission\n", ctx_id);
        return;
    }

    log::v(
        "worker[%u] found non-sqe submissions [%u,%u] (%u in total)\n", ctx_id,
        cur.head(), cur.tail() - 1, cur.size()
    );

    auto head{cur.raw_head()};
    auto tail{cur.raw_tail()};

    for (; head != tail; ++head) {
        const auto i{head & cur.mask};
        log::v("worker[%u] submit(swap[%d])\n", ctx_id, i);
        submit(swap[i]); // always success (currently).
    }

    cur.m_head = tail; // consume submit_cur
}

void worker_meta::submit(submit_info &info) noexcept {
    assert(info.address != 0); // sqe is impossible.

    using submit_type = detail::submit_type;
    task_info *const io_info = CO_CONTEXT_ASSUME_ALIGNED(alignof(task_info)
    )(detail::raw_task_info_ptr(info.address));

    // PERF May call cur.pop() to make worker start sooner.
    switch (uint32_t(info.address) & 0b111) {
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
            assert(false && "submit(): unknown task_type");
            return;
    }
}

void worker_meta::forward_task(std::coroutine_handle<> handle) noexcept {
    assert(bool(handle) && "forwarding an empty task");

    auto &cur = reap_cur;
    log::v(
        "worker[%u] forward_task(%lx) to [%u]\n", ctx_id, handle.address(),
        cur.tail()
    );

    reap_swap[cur.tail()] = reap_info{handle};
    cur.push();
}

void worker_meta::handle_semaphore_release(task_info *sem_release) noexcept {
    counting_semaphore &sem = *as_counting_semaphore(sem_release);
    const counting_semaphore::T update =
        as_atomic(sem_release->update).exchange(0, std::memory_order_relaxed);
    if (update == 0) [[unlikely]] {
        return;
    }

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

void worker_meta::handle_condition_variable_notify(task_info *cv_notify
) noexcept {
    condition_variable &cv = *detail::as_condition_variable(cv_notify);
    const condition_variable::T notify_counter =
        as_atomic(cv_notify->notify_counter)
            .exchange(0, std::memory_order_relaxed);

    if (notify_counter == 0) [[unlikely]] {
        return;
    }

    if (cv.awaiting.load(std::memory_order_relaxed) != nullptr) {
        cv.to_resume_fetch_all();
    }

    condition_variable::T done = 0;
    const bool is_nofity_all = notify_counter & cv.notify_all_flag;
    while ((is_nofity_all || done < notify_counter)
           && cv.to_resume_head != nullptr) {
        mutex::lock_awaiter &to_awake = cv.to_resume_head->lock_awaken_handle;
        // let the coroutine get the lock
        if (!to_awake.register_awaiting()) {
            // lock succ, wakeup
            forward_task(to_awake.get_coroutine());
        }
        // lock failed, just wait for another mutex.unlock()

        cv.to_resume_head = cv.to_resume_head->next;
        ++done;
    }

    if (cv.to_resume_head == nullptr) {
        cv.to_resume_tail = nullptr;
    }
}

inline void worker_meta::reap(detail::reap_info info) noexcept {
    auto &cur = reap_cur;
    log::v("worker[%u] reap at [%u]\n", ctx_id, cur.tail());
    reap_swap[cur.tail()] = info;
    cur.push();
}

static bool eager_io_need_awake(detail::task_info *io_info) noexcept {
    using io_state_t = eager::io_state_t;
    const io_state_t old_state =
        as_atomic(io_info->eager_io_state)
            // .fetch_or(eager::io_ready, std::memory_order_seq_cst);
            .fetch_or(eager::io_ready, std::memory_order_release);
    if (old_state & eager::io_detached) {
        delete io_info;
    }
    return old_state & eager::io_wait;
}

void worker_meta::handle_cq_entry(const liburingcxx::cq_entry *const cqe
) noexcept {
    --requests_to_reap;
    log::v("ctx poll_completion found, remaining=%d\n", requests_to_reap);

    const uint64_t user_data = cqe->user_data;
    const int32_t result = cqe->res;
    [[maybe_unused]] const uint32_t flags = cqe->flags;

    if (config::is_log_d && result < 0) {
        log::d(
            "cqe reports error: user_data=%lx, result=%d, flags=%u\n"
            "message: %s\n",
            user_data, result, flags, strerror(-result)
        );
    }

    ring.seen_cq_entry(cqe);
    assert(flags != detail::reap_info::co_spawn_flag);

    using task_type = task_info::task_type;

    if constexpr (config::enable_eager_io) {
        if ((user_data & 0b111) == uint8_t(task_type::eager_sqe)) [[unlikely]] {
            auto *const eager_io_info = CO_CONTEXT_ASSUME_ALIGNED(8
            )(reinterpret_cast /*NOLINT*/<task_info *>(
                user_data ^ uint64_t(task_type::eager_sqe)
            ));
            eager_io_info->result = result;
            if (eager_io_need_awake(eager_io_info)) [[likely]] {
                reap(detail::reap_info{eager_io_info->handle});
            }
            return;
        }
        // must be lazy_sqe or lazy_link_sqe here
    }

    if constexpr (!config::enable_link_io_result) {
        // if link_io_result is not enabled, we can skip the lazy_link_sqe.
        if ((user_data & 0b111) == uint8_t(task_type::lazy_link_sqe)) {
            return;
        }
    }

    task_info *const io_info = CO_CONTEXT_ASSUME_ALIGNED(alignof(task_info)
    )(detail::raw_task_info_ptr(user_data));
    reap(detail::reap_info{io_info, result, flags});
}

} // namespace co_context::detail
