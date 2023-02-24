#pragma once

#include "co_context/config.hpp"
#include "co_context/detail/io_context_meta.hpp"
#include "co_context/detail/poller_type.hpp"
#include "co_context/detail/spsc_cursor.hpp"
#include "co_context/detail/thread_meta.hpp"
#include "co_context/detail/user_data.hpp"
#include "co_context/log/log.hpp"
#include <coroutine>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <queue>
#include <thread>

#if CO_CONTEXT_IS_USING_EVENTFD
#include <sys/eventfd.h>
#endif

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
#ifdef USE_IO_URING
    // An instant of io_uring
    alignas(cache_line_size) uring ring;
#if CO_CONTEXT_IS_USING_EVENTFD
    uint64_t co_spawn_event_buf = 0;
#endif
#endif

#ifdef USE_EPOLL
    using epoll_event = epoll::epoll_event;
    using epoll_data_t = epoll::epoll_data_t;
    alignas(cache_line_size) epoll poller;
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
    ) std::array<std::coroutine_handle<>, config::swap_capacity> reap_swap;

    using cur_t = config::cur_t;

    spsc_cursor<cur_t, config::swap_capacity, unsafe> reap_cur;

#if CO_CONTEXT_IS_USING_EVENTFD
    // TODO replace this with fixed-sized queue.
    std::queue<std::coroutine_handle<>> co_spawn_local_queue;
#endif

    // number of I/O tasks running inside io_uring
    int32_t requests_to_reap = 0;

    // if there is at least one entry to submit to io_uring
    uint32_t requests_to_submit = 0;

    // if there is at least one task newly spawned or forwarded
    [[nodiscard]]
    bool has_task_ready() const noexcept {
        return !reap_cur.is_empty();
    }

#ifdef USE_IO_URING
    liburingcxx::sq_entry *get_free_sqe() noexcept;

    [[nodiscard]]
    bool is_ring_need_enter() const noexcept;
#endif

#if CO_CONTEXT_IS_USING_EVENTFD
    void listen_on_co_spawn() noexcept;
#endif

    [[nodiscard]]
    cur_t number_to_schedule() const noexcept {
        const auto &cur = this->reap_cur;
        return cur.size();
    }

    void init(unsigned io_entries);

    void co_spawn_unsafe(std::coroutine_handle<> handle) noexcept;

#if CO_CONTEXT_IS_USING_MSG_RING
    void co_spawn_safe_msg_ring(std::coroutine_handle<> handle) const noexcept;
#else
    void co_spawn_safe_eventfd(std::coroutine_handle<> handle) noexcept;
#endif

    void co_spawn_auto(std::coroutine_handle<> handle) noexcept;

    void work_once();

    void check_submission_threshold() noexcept;

    /**
     * @brief poll the submission swap zone
     */
    void poll_submission() noexcept;

    /**
     * @brief poll the uring completion queue (Nonblocking)
     *
     * @return number of cqes/events handled by worker
     */
    uint32_t poll_completion() noexcept;

    /**
     * @brief poll the uring completion queue (Blocking)
     *
     * @return number of cqes/events handled by worker
     */
    uint32_t wait_and_poll_completion() noexcept;

    explicit worker_meta() noexcept;
    ~worker_meta() noexcept;

  private:
    /**
     * @brief forward a coroutine to the reap_swap.
     * @warning The reap_swap will not be checked for full.
     */
    void forward_task(std::coroutine_handle<> handle) noexcept;

    /**
     * @brief Get a coroutine to run. Guarantee to be non-null.
     */
    [[nodiscard]]
    std::coroutine_handle<> schedule() noexcept;

#ifdef USE_IO_URING
    /**
     * @brief handle an non-null cq_entry from the cq of io_uring
     */
    void handle_cq_entry(const liburingcxx::cq_entry *) noexcept;
#endif

#ifdef USE_EPOLL
    void handle_ep_event(const epoll_event *) noexcept;
#endif

    void handle_reserved_user_data(uint64_t user_data) noexcept;

#if CO_CONTEXT_IS_USING_EVENTFD
    void handle_co_spawn_events() noexcept;
#endif

#ifdef USE_IO_URING
    [[nodiscard]]
    bool check_init(unsigned expect_sqring_size) const noexcept;
#endif
};

inline void worker_meta::co_spawn_unsafe(std::coroutine_handle<> handle
) noexcept {
    log::v("worker[%u] co_spawn_unsafe coro(%lx)\n", ctx_id, handle.address());
    forward_task(handle);
}

#if CO_CONTEXT_IS_USING_EVENTFD
inline void worker_meta::co_spawn_safe_eventfd(std::coroutine_handle<> handle
) noexcept {
    log::v(
        "coro(%lx) is pushing to worker[%u] by co_spawn_safe_eventfd() \n",
        handle.address(), ctx_id
    );
    {
        std::lock_guard lg{co_spawn_mtx};
        co_spawn_queue.push(handle);
    }
    ::eventfd_write(co_spawn_event_fd, 1);
}
#endif

#if CO_CONTEXT_IS_USING_MSG_RING
inline void worker_meta::co_spawn_safe_msg_ring(std::coroutine_handle<> handle
) const noexcept {
    worker_meta &from = *this_thread.worker;
    log::v(
        "coro(%lx) is pushing to worker[%u] from worker[%u] "
        "by co_spawn_safe_msg_ring() \n",
        handle.address(), ctx_id, from.ctx_id
    );
    auto *const sqe = from.get_free_sqe();
    auto user_data = reinterpret_cast<uint64_t>(handle.address())
                     | uint8_t(user_data_type::coroutine_handle);
    sqe->prep_msg_ring(ring_fd, 0, user_data, 0);
    sqe->set_data(uint64_t(reserved_user_data::nop));
    sqe->set_cqe_skip();
    --from.requests_to_reap;
}
#endif

inline void worker_meta::co_spawn_auto(std::coroutine_handle<> handle
) noexcept {
    // MT-unsafe in some scenes (for meta.ready_count == 0)
    // before calling io_context::start(), this_thread.worker is nullptr.
    if (detail::this_thread.worker == this
        || io_context_meta.ready_count == 0) {
        this->co_spawn_unsafe(handle);
    } else {
#if CO_CONTEXT_IS_USING_MSG_RING
        this->co_spawn_safe_msg_ring(handle);
#else
        this->co_spawn_safe_eventfd(handle);
#endif
    }
}

inline uint32_t worker_meta::poll_completion() noexcept {
#ifdef USE_IO_URING
    using cq_entry = liburingcxx::cq_entry;
    return ring.for_each_cqe([this](const cq_entry *cqe) noexcept {
        this->handle_cq_entry(cqe);
    });
#endif
#ifdef USE_EPOLL
    // non-blocking epoll_wait
    return poller.wait_and_for_each_event(
        [this](const epoll_event *e) noexcept { this->handle_ep_event(e); }, 0
    );
#endif
}

inline uint32_t worker_meta::wait_and_poll_completion() noexcept {
#ifdef USE_IO_URING
    [[maybe_unused]] const liburingcxx::cq_entry *_;
    ring.wait_cq_entry(_);
    return poll_completion();
#endif
#ifdef USE_EPOLL
    // TODO add timer
    return poller.wait_and_for_each_event(
        [this](const epoll_event *e) noexcept { this->handle_ep_event(e); }, -1
    );
#endif
}

} // namespace co_context::detail
