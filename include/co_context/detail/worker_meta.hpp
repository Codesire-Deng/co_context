#pragma once

#include "co_context/config.hpp"
#include "co_context/detail/submit_info.hpp"
#include "co_context/detail/thread_meta.hpp"
#include "co_context/detail/uring_type.hpp"
#include "co_context/detail/user_data.hpp"
#include "co_context/lockfree/spsc_cursor.hpp"
#include "co_context/log/log.hpp"
#include "co_context/task.hpp"
#include <coroutine>
#include <cstdint>
#include <mutex>
#include <queue>
#include <sys/eventfd.h>
#include <thread>

namespace co_context {

class io_context;

} // namespace co_context

namespace co_context::detail {

using config::cache_line_size;

struct worker_meta final {
    /**
     * ---------------------------------------------------
     * Data sharing with the ring
     * ---------------------------------------------------
     */
    // An instant of io_uring
    alignas(cache_line_size) uring ring;

#if CO_CONTEXT_IS_USING_EVENTFD
    uint64_t co_spawn_event_buf = 0;
#endif

    /**
     * ---------------------------------------------------
     * read-only sharing data
     * ---------------------------------------------------
     */

    alignas(cache_line_size)
#if CO_CONTEXT_IS_USING_EVENTFD
        int co_spawn_event_fd = -1;
#else
        int ring_fd;
#endif

    config::ctx_id_t ctx_id;

    /**
     * ---------------------------------------------------
     * read-writable sharing data
     * ---------------------------------------------------
     */

#if CO_CONTEXT_IS_USING_EVENTFD
    alignas(cache_line_size) std::mutex co_spawn_mtx;
    // TODO replace this with spsc fixed-sized queue.
    std::queue<std::coroutine_handle<>> co_spawn_queue;
#endif

    /**
     * ---------------------------------------------------
     * Thread-local read/write data
     * ---------------------------------------------------
     */

    alignas(cache_line_size
    ) std::array<detail::submit_info, config::swap_capacity> submit_swap;

    std::array<std::coroutine_handle<>, config::swap_capacity> reap_swap;

    using cur_t = config::cur_t;

    spsc_cursor<cur_t, config::swap_capacity, false> submit_cur;

    spsc_cursor<cur_t, config::swap_capacity, false> reap_cur;

#if CO_CONTEXT_IS_USING_EVENTFD
    // TODO replace this with fixed-sized queue.
    std::queue<std::coroutine_handle<>> co_spawn_local_queue;
#endif

    // number of I/O tasks running inside io_uring
    int32_t requests_to_reap = 0;

    // if there is at least one entry to submit to io_uring
    bool need_ring_submit = false;

    // if there is at least one task newly spawned or forwarded
    [[nodiscard]] bool has_task_ready() const noexcept {
        return !reap_cur.is_empty();
    }

    liburingcxx::sq_entry *get_free_sqe() noexcept;

    void submit_sqe() const noexcept;

    void submit_non_sqe(uintptr_t typed_task) noexcept;

    [[nodiscard]] bool is_ring_need_enter() const noexcept;

#if CO_CONTEXT_IS_USING_EVENTFD
    void listen_on_co_spawn() noexcept;
#endif

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

#if CO_CONTEXT_IS_USING_MSG_RING
    void co_spawn_safe_msg_ring(std::coroutine_handle<> entrance
    ) const noexcept;
#else
    void co_spawn_safe_eventfd(std::coroutine_handle<> entrance) noexcept;
#endif

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
     * @brief handle an non-null cq_entry from the cq of io_uring
     */
    void handle_cq_entry(const liburingcxx::cq_entry *) noexcept;

    void handle_reserved_user_data(uint64_t user_data) noexcept;

#if CO_CONTEXT_IS_USING_EVENTFD
    void handle_co_spawn_events() noexcept;
    explicit worker_meta() noexcept;
    ~worker_meta() noexcept;
#else
    explicit worker_meta() noexcept = default;
    ~worker_meta() noexcept = default;
#endif

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

#if CO_CONTEXT_IS_USING_EVENTFD
inline void worker_meta::co_spawn_safe_eventfd(std::coroutine_handle<> entrance
) noexcept {
    log::v(
        "coro(%lx) is pushing to worker[%u] by co_spawn_safe_eventfd() \n",
        entrance.address(), ctx_id
    );
    {
        std::lock_guard lg{co_spawn_mtx};
        co_spawn_queue.push(entrance);
    }
    ::eventfd_write(co_spawn_event_fd, 1);
}
#endif

#if CO_CONTEXT_IS_USING_MSG_RING
inline void worker_meta::co_spawn_safe_msg_ring(std::coroutine_handle<> entrance
) const noexcept {
    worker_meta &from = *this_thread.worker;
    log::v(
        "coro(%lx) is pushing to worker[%u] from worker[%u] "
        "by co_spawn_safe_msg_ring() \n",
        entrance.address(), ctx_id, from.ctx_id
    );
    auto *const sqe = from.get_free_sqe();
    auto user_data = reinterpret_cast<uint64_t>(entrance.address())
                     | uint8_t(user_data_type::coroutine_handle);
    sqe->prep_msg_ring(ring_fd, 0, user_data, 0);
    sqe->set_data(uint64_t(reserved_user_data::nop));
#if LIBURINGCXX_IS_KERNEL_REACH(5, 17)
    sqe->set_cqe_skip();
    --from.requests_to_reap;
#endif
    from.submit_sqe();
}
#endif

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
