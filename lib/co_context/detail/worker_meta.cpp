#include "co_context/config.hpp"
#include "co_context/co/condition_variable.hpp"
#include "co_context/co/semaphore.hpp"
#include "co_context/detail/compat.hpp"
#include "co_context/detail/poller_type.hpp"
#include "co_context/detail/task_info.hpp"
#include "co_context/detail/thread_meta.hpp"
#include "co_context/detail/user_data.hpp"
#include "co_context/io_context.hpp"
#include "co_context/log/log.hpp"
#include "co_context/utility/as_buffer.hpp"
#include "co_context/utility/set_cpu_affinity.hpp"
#include <atomic>
#include <cerrno>
#include <coroutine>
#include <cstdint>
#include <exception>
#include <memory>
#include <mutex>
#include <thread>
#include <unistd.h>

#if CO_CONTEXT_IS_USING_EVENTFD
#include <sys/eventfd.h>
#endif

namespace co_context::detail {

thread_local thread_meta this_thread;

worker_meta::worker_meta() noexcept {
#if CO_CONTEXT_IS_USING_EVENTFD
    co_spawn_event_fd = ::eventfd(0, 0);
    if (co_spawn_event_fd == -1) [[unlikely]] {
        log::e("Errors on eventfd(). errno = %d\n", errno);
        std::terminate();
    }
#endif
}

worker_meta::~worker_meta() noexcept {
#if CO_CONTEXT_IS_USING_EVENTFD
    if (co_spawn_event_fd != -1) [[likely]] {
        ::close(co_spawn_event_fd);
    }
#endif
}

void worker_meta::init(unsigned io_entries) {
    this->ctx_id = this_thread.ctx_id;
    this_thread.worker = this;

#ifdef USE_IO_URING
    ring.init(io_entries);
#if CO_CONTEXT_IS_USING_MSG_RING
    this->ring_fd = ring.fd();
#endif
    if (!check_init(io_entries)) {
        std::terminate();
    }
#endif

#ifdef USE_EPOLL
    poller.init(int(io_entries));
#endif

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

#ifdef USE_IO_URING
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
#endif

#ifdef USE_IO_URING
liburingcxx::sq_entry *worker_meta::get_free_sqe() noexcept {
    log::v("worker[%u] get_free_sqe\n", this_thread.ctx_id);
    ++requests_to_reap; // NOTE may required no reap or required multi-reap.
    ++requests_to_submit;

    auto *sqe = this->ring.get_sq_entry();
    assert(sqe != nullptr);
    return sqe;
}
#endif

#if CO_CONTEXT_IS_USING_EVENTFD
#ifdef USE_IO_URING
void worker_meta::listen_on_co_spawn() noexcept {
    auto *const sqe = get_free_sqe();
    sqe->prep_read(co_spawn_event_fd, as_buf(&co_spawn_event_buf), 0);
    sqe->set_data(static_cast<uint64_t>(reserved_user_data::co_spawn_event));
}
#endif
#ifdef USE_EPOLL
void worker_meta::listen_on_co_spawn() noexcept {
    epoll_event e;
    e.events = EPOLLIN;
    e.data.u64 = static_cast<uint64_t>(reserved_user_data::co_spawn_event);
    [[maybe_unused]] int res = poller.add(co_spawn_event_fd, e);
    assert(res == 0);
}
#endif
#endif

std::coroutine_handle<> worker_meta::schedule() noexcept {
    auto &cur = this->reap_cur;

    log::v("worker[%u] try scheduling\n", this->ctx_id);
    assert(!cur.is_empty());

    std::coroutine_handle<> chosen_coro = reap_swap[cur.head()];
    cur.pop();
    assert(bool(chosen_coro));
    return chosen_coro;
}

void worker_meta::work_once() {
    const auto coro = this->schedule();
    log::v("worker[%u] resume %lx\n", this->ctx_id, coro.address());
    coro.resume();

    log::v("worker[%u] work_once finished\n", this->ctx_id);
}

void worker_meta::check_submission_threshold() noexcept {
#ifdef USE_IO_URING
    if constexpr (config::submission_threshold != -1) {
        if (requests_to_submit >= config::submission_threshold) {
            [[maybe_unused]] int res = ring.submit_and_get_events();
            assert(res >= 0 && "exception at uring::submit");
            requests_to_submit = 0;
        }
    }
#endif
}

void worker_meta::poll_submission() noexcept {
#ifdef USE_IO_URING
    // submit sqes
    if (requests_to_submit) [[likely]] {
        bool will_wait = !has_task_ready();
        [[maybe_unused]] int res = ring.submit_and_wait(will_wait);
        assert(res >= 0 && "exception at uring::submit_and_wait");
        requests_to_submit = 0;
    }
#endif
#ifdef USE_EPOLL
    requests_to_submit = 0;
#endif
}

void worker_meta::forward_task(std::coroutine_handle<> handle) noexcept {
    assert(bool(handle) && "forwarding an empty task");

    auto &cur = reap_cur;
    log::v(
        "worker[%u] forward_task(%lx) to [%u]\n", ctx_id, handle.address(),
        cur.tail()
    );

    reap_swap[cur.tail()] = handle;
    cur.push();
}

#ifdef USE_IO_URING
void worker_meta::handle_cq_entry(const liburingcxx::cq_entry *const cqe
) noexcept {
    --requests_to_reap;
    log::v("ctx poll_completion found, remaining=%d\n", requests_to_reap);

    uint64_t user_data = cqe->user_data;
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

    if constexpr (uint64_t(detail::reserved_user_data::none) > 0) {
        if (user_data < uint64_t(detail::reserved_user_data::none))
            [[unlikely]] {
            handle_reserved_user_data(user_data);
            return;
        }
    }

    using mux = user_data_type;
    mux selector = mux(uint8_t(user_data & 0b111));
    assert(uint8_t(selector) < uint8_t(mux::none));

    user_data &= raw_task_info_mask;
    task_info *__restrict__ const io_info =
        CO_CONTEXT_ASSUME_ALIGNED(alignof(task_info))(
            reinterpret_cast /*NOLINT*/<task_info *>(user_data)
        );

    switch (selector) {
        [[likely]] case mux::task_info_ptr:
            io_info->result = result;
            // io_info->flags = flags;
            forward_task(io_info->handle);
            break;
        case mux::coroutine_handle:
            forward_task(std::coroutine_handle<>::from_address(
                reinterpret_cast<void *>(user_data) /*NOLINT*/
            ));
            break;
        case mux::task_info_ptr__link_sqe:
            if constexpr (config::enable_link_io_result) {
                io_info->result = result;
            }
            // if link_io_result is not enabled, we can skip the
            // lazy_link_sqe.
            break;
        [[unlikely]] case mux::none:
            assert(false && "handle_cq_entry(): unknown case");
    }
}
#endif

#ifdef USE_EPOLL
void worker_meta::handle_ep_event(const epoll_event *const e_ptr) noexcept {
    --requests_to_reap;
    log::v("ctx poll_completion found, remaining=%d\n", requests_to_reap);

    const epoll_event e = *e_ptr;

    if (config::is_log_d && (e.events & ~(EPOLLIN | EPOLLOUT))) {
        log::d(
            "worker_meta::handle_ep_event: unknown events type: %u\n", e.events
        );
    }

    if constexpr (uint64_t(detail::reserved_user_data::none) > 0) {
        if (e.data.u64 < uint64_t(detail::reserved_user_data::none))
            [[unlikely]] {
            handle_reserved_user_data(e.data.u64);
            return;
        }
    }

    assert((e.data.u64 & 0b111) == 0); // no multiplexer yet
    const auto &data = *reinterpret_cast<epoll_fd_data *>(e.data.ptr);
    if (e.events & EPOLLIN) {
        // PERF check if this is necessary.
        if (data.in.handle) [[likely]] {
            forward_task(data.in.handle);
        } else {
            log::d("worker_meta::handle_ep_event: in.handler is nullptr\n");
        }
    }
    if (e.events & EPOLLOUT) {
        // NOTE the fd may be invaild due to async user code
        if (data.out.handle) [[likely]] {
            forward_task(data.out.handle);
        } else {
            log::d("worker_meta::handle_ep_event: out.handler is nullptr\n");
        }
    }
}
#endif

void worker_meta::handle_reserved_user_data(const uint64_t user_data) noexcept {
    using mux = detail::reserved_user_data;
    switch (mux(user_data)) {
        [[likely]]
#if CO_CONTEXT_IS_USING_EVENTFD
        case mux::co_spawn_event:
            handle_co_spawn_events();
            break;
#endif
        case mux::nop:
            ++requests_to_reap; // compensate this number. see handle_cqe()
            break;
        [[unlikely]] case mux::none:
            break;
    }
}

#if CO_CONTEXT_IS_USING_EVENTFD
void worker_meta::handle_co_spawn_events() noexcept {
    assert(co_spawn_local_queue.empty());

    {
        std::lock_guard lg{co_spawn_mtx};
        co_spawn_local_queue.swap(co_spawn_queue);
    }

    const size_t overflow_level = reap_cur.available_number();
    size_t num = co_spawn_local_queue.size();

    if (overflow_level < num) [[unlikely]] {
        log::e(
            "Too many co_spawn() exhausted reap_swap "
            "at worker[%u]: panding = %u, free_space = %u\n",
            this->ctx_id, num, overflow_level
        );
        std::terminate();
    }

    if constexpr (config::is_log_w) {
        const size_t warning_level = overflow_level / 4 * 3;
        if (warning_level <= num) [[unlikely]] {
            log::w(
                "Too many co_spawn(). worker[%u] is running out of "
                "reap_swap: panding = %u, free_space = %u\n",
                this->ctx_id, num, overflow_level
            );
        }
    }

    while (num--) {
        forward_task(co_spawn_local_queue.front());
        co_spawn_local_queue.pop();
    }

#ifdef USE_IO_URING
    // regenerate reading request for io_uring
    listen_on_co_spawn();
#endif
}
#endif

} // namespace co_context::detail
