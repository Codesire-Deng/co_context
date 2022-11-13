#pragma once

#include "co_context/config.hpp"
#include "co_context/detail/reap_info.hpp"
#include "co_context/detail/submit_info.hpp"
#include "co_context/detail/thread_meta.hpp"
#include "co_context/detail/uring_type.hpp"
#include "co_context/lockfree/spsc_cursor.hpp"
#include "co_context/log/log.hpp"
#include "co_context/task.hpp"
#include <queue>
#include <thread>

namespace co_context {

class io_context;

} // namespace co_context

namespace co_context::detail {

using config::cache_line_size;

struct worker_meta final {
    // An instant of io_uring
    alignas(cache_line_size) uring ring;

    alignas(cache_line_size
    ) std::array<detail::submit_info, config::swap_capacity> submit_swap;

    std::array<detail::reap_info, config::swap_capacity> reap_swap;

    using cur_t = config::cur_t;

    spsc_cursor<cur_t, config::swap_capacity, false> submit_cur;

    spsc_cursor<cur_t, config::swap_capacity, false> reap_cur;

    // number of I/O tasks running inside io_uring
    int32_t requests_to_reap = 0;

    // if there is at least one entry to submit to io_uring
    bool need_ring_submit = false;

    // if there is at least one task newly spawned or forwarded
    [[nodiscard]] bool has_task_ready() const noexcept {
        return !reap_cur.is_empty();
    }

    config::ctx_id_t ctx_id;

    liburingcxx::sq_entry *get_free_sqe() noexcept;

    void submit_sqe() const noexcept;

    void submit_non_sqe(uintptr_t typed_task) noexcept;

    [[nodiscard]] bool is_ring_need_enter() const noexcept;

    void wait_uring() noexcept;

    [[nodiscard]] cur_t number_to_schedule() const noexcept {
        const auto &cur = this->reap_cur;
        return cur.size();
    }

    // Get a coroutine to run. May return nullptr.
    [[nodiscard]] std::coroutine_handle<> schedule() noexcept;

    void init(unsigned io_uring_entries);

    void co_spawn_unsafe(std::coroutine_handle<> entrance) noexcept;

    void co_spawn_safe_lazy(std::coroutine_handle<> entrance) noexcept;

    void co_spawn_safe_eager(std::coroutine_handle<> entrance) noexcept;

    void work_once();

    /**
     * @brief poll the submission swap zone
     */
    void poll_submission() noexcept;

    /**
     * @brief poll the uring completion queue
     *
     * @return number of cqes handled by worker
     */
    uint32_t poll_completion() noexcept;

    /**
     * @brief forward a coroutine to a random worker. If failed (because workers
     * are full), forward to the overflow buffer.
     */
    void forward_task(std::coroutine_handle<> handle) noexcept;

    /**
     * @brief handler where io_context finds a semaphore::release task
     */
    void handle_semaphore_release(task_info *sem_release) noexcept;

    /**
     * @brief handler where io_context finds a condition_variable::notify_* task
     */
    void handle_condition_variable_notify(task_info *cv_notify) noexcept;

    /**
     * @brief handle the submission from the worker.
     */
    void submit(detail::submit_info &info) noexcept;

    /**
     * @brief forward the completion from the io_uring to a worker. If failed
     * (because workers are full), do nothing.
     */
    void reap(detail::reap_info info) noexcept;

    /**
     * @brief handle an non-null cq_entry from the cq of io_uring
     */
    void handle_cq_entry(const liburingcxx::cq_entry *) noexcept;

    explicit worker_meta() = default;

  private:
    [[nodiscard]] bool check_init(unsigned expect_sqring_size) const noexcept;
};

inline void worker_meta::co_spawn_unsafe(std::coroutine_handle<> entrance
) noexcept {
    log::v(
        "worker[%u] co_spawn_unsafe coro(%lx)\n", ctx_id, entrance.address()
    );
    forward_task(entrance);
}

inline void worker_meta::co_spawn_safe_lazy(std::coroutine_handle<> entrance
) noexcept {
    assert(false && "todo");
}

inline void worker_meta::co_spawn_safe_eager(std::coroutine_handle<> entrance
) noexcept {
    assert(false && "todo");
}

inline uint32_t worker_meta::poll_completion() noexcept {
    using cq_entry = liburingcxx::cq_entry;

    uint32_t num = ring.for_each_cqe([this](const cq_entry *cqe) noexcept {
        this->handle_cq_entry(cqe);
    });

    return num;
}

inline bool worker_meta::is_ring_need_enter() const noexcept {
    return ring.is_cq_ring_need_enter();
}

inline void worker_meta::wait_uring() noexcept {
    [[maybe_unused]] const liburingcxx::cq_entry *_;
    ring.wait_cq_entry(_);
}

} // namespace co_context::detail
