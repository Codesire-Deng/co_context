/*
 *  An io_context library aimed at low-latency io, based on
 * [liburingcxx](https://github.com/Codesire-Deng/liburingcxx).
 *
 *  Copyright (C) 2022 Zifeng Deng
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU Affero General Public License as published
 *  by the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Affero General Public License for more details.
 *
 *  You should have received a copy of the GNU Affero General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#pragma once

#include <thread>
#include <vector>
#include <queue>
#include "uring/uring.hpp"
#include "co_context/config.hpp"
#include "co_context/detail/task_info.hpp"
#include "co_context/detail/submit_info.hpp"
#include "co_context/detail/reap_info.hpp"
#include "co_context/task.hpp"
#include "co_context/detail/thread_meta.hpp"
#include "co_context/detail/worker_meta.hpp"

namespace co_context {

using config::cache_line_size;

class io_context;

class [[nodiscard]] io_context final {
  private:
    using uring = liburingcxx::URing<config::io_uring_flags>;

    using task_info = detail::task_info;

    friend class detail::worker_meta;
    using worker_meta = detail::worker_meta;

  private:
    /**
     * ---------------------------------------------------
     * Multi-thread sharing data
     * ---------------------------------------------------
     */

    // An instant of io_uring
    alignas(cache_line_size) uring ring;

    // The meta data of worker(s)
    alignas(cache_line_size) worker_meta worker[config::workers_number];

    /**
     * ---------------------------------------------------
     * local read/write, high frequency data
     * ---------------------------------------------------
     */

    // cursor pointing to the submit worker
    alignas(cache_line_size) config::tid_t s_cur = 0;

    // cursor pointing to the reap worker
    config::tid_t r_cur = 0;

    // number of I/O tasks running inside io_uring
    int32_t requests_to_reap = 0;

    // if there is at least one entry to submit to io_uring
    bool need_ring_submit = false;

    // should io_context stop
    bool will_stop = false;

    // buffer to hold the exceeded submission
    /*
    std::queue<task_info *> submit_overflow_buf;
    */

    // buffer to hold the exceeded completion
    std::queue<detail::reap_info> reap_overflow_buf;

    /**
     * ---------------------------------------------------
     * read-only sharing data
     * ---------------------------------------------------
     */

    // SQ capacity in the io_uring instant
    alignas(cache_line_size) const unsigned sqring_entries;

  private:
    /**
     * @brief `cur = (cur + 1) % workers_number`
     */
    inline static void cur_next(config::tid_t &cur) noexcept {
        if constexpr (config::workers_number > 1)
            cur = (cur + 1) % config::workers_number;
    }

  private:
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
     * @brief find a worker who has a submission.
     * @note use relaxed memory order.
     *
     * @return true if there is a worker. `s_cur` will point to it.
     * false if that does not exist. `s_cur` remains unchanged.
     */
    bool try_find_submit_worker_relaxed() noexcept;

    /**
     * @brief same as `try_find_submit_worker_relaxed()`, except for memory
     * order
     * @note use acquire memory order.
     */
    [[deprecated]] bool try_find_submit_worker_acquire() noexcept;

    /**
     * @brief find a worker who can undertake a completion.
     * @note use relaxed memory order.
     *
     * @return true if there is a worker. `r_cur` will point to it.
     * false if that does not exist. `r_cur` remains unchanged.
     */
    bool try_find_reap_worker_relaxed() noexcept;

    /**
     * @brief same as `try_find_reap_worker_relaxed()`, except for memory
     * order
     * @note use acquire memory order.
     */
    [[deprecated]] bool try_find_reap_worker_acquire() noexcept;

    /**
     * @brief handle the submission from the worker.
     */
    void try_submit(detail::submit_info &info) noexcept;

    /**
     * @brief poll the submission swap zone
     *
     * @return if submit_swap capacity might be healthy
     */
    bool poll_submission() noexcept;

    /*
    bool try_clear_submit_overflow_buf() noexcept;
    */

    /**
     * @brief forward the completion from the io_uring to a worker. If failed
     * (because workers are full), do nothing.
     *
     * @return true if a worker undertake this completion. false otherwise.
     */
    bool try_reap(detail::reap_info info) noexcept;

    /**
     * @brief forward the completion from the io_uring to a worker. If failed
     * (because workers are full), forward to the overflow buffer.
     */
    void reap_or_overflow(detail::reap_info info) noexcept;

    /**
     * @brief poll the completion inside the io_uring
     */
    void poll_completion() noexcept;

    /**
     * @brief forward the completion waiting in the overflow buffer.
     *
     * @return true if the overflow buffer is cleared. false otherwise.
     */
    bool try_clear_reap_overflow_buf() noexcept;

    /**
     * @brief exit(0)
     */
    [[noreturn]] void stop() noexcept {
        log::i("ctx stopped\n");
        ::exit(0);
    }

  private:
    void init() noexcept;

    void make_thread_pool();

    void co_spawn(std::coroutine_handle<> entrance) noexcept;
    
    friend void co_spawn(task<void> &&entrance) noexcept;

  public:
    io_context(unsigned io_uring_entries, uring::Params &io_uring_params)
        : ring(io_uring_entries, io_uring_params)
        , sqring_entries(io_uring_entries) {
        init();
    }

    io_context(unsigned io_uring_entries)
        : ring(io_uring_entries), sqring_entries(io_uring_entries) {
        init();
    }

    io_context(unsigned io_uring_entries, uring::Params &&io_uring_params)
        : io_context(io_uring_entries, io_uring_params) {}

    void probe() const;

    void co_spawn(task<void> &&entrance) noexcept;

    void can_stop() noexcept { will_stop = true; }

    [[noreturn]] void run();

    inline uring &io() noexcept { return ring; }

    ~io_context() noexcept {
        for (int i = 0; i < config::worker_threads_number; ++i) {
            std::thread &t = worker[i].host_thread;
            if (t.joinable()) t.join();
        }
    }

    /**
     * ban all copying or moving
     */
    io_context(const io_context &) = delete;
    io_context(io_context &&) = delete;
    io_context &operator=(const io_context &) = delete;
    io_context &operator=(io_context &&) = delete;
};

inline void co_spawn(task<void> &&entrance) noexcept {
    if (detail::this_thread.worker != nullptr) [[likely]]
        detail::this_thread.worker->co_spawn(entrance.get_handle());
    else
        detail::this_thread.ctx->co_spawn(entrance.get_handle());
    entrance.detach();
}

inline void co_context_stop() noexcept {
    detail::this_thread.ctx->can_stop();
}

inline config::tid_t co_get_tid() noexcept {
    return detail::this_thread.tid;
}

} // namespace co_context
