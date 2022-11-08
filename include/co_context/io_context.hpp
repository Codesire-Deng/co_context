/*
 *  A coroutine framework aimed at high-concurrency io with reasonable latency,
 *  based on liburingcxx.
 *
 *     Copyright 2022 Zifeng Deng
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */
#pragma once

#include "uring/uring.hpp"
#include "co_context/config.hpp"
#include "co_context/detail/reap_info.hpp"
#include "co_context/detail/submit_info.hpp"
#include "co_context/detail/task_info.hpp"
#include "co_context/detail/thread_meta.hpp"
#include "co_context/detail/uring_type.hpp"
#include "co_context/detail/worker_meta.hpp"
#include "co_context/task.hpp"
#include <queue>
#include <thread>
#include <vector>

namespace co_context {

using config::cache_line_size;

class io_context;

class [[nodiscard]] io_context final {
  public:
    using uring = detail::uring;

  private:
    using task_info = detail::task_info;

    friend struct detail::worker_meta;
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
    alignas(cache_line_size) worker_meta workers[config::workers_number];

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

    // if there is at least one task newly spawned or forwarded
    bool has_task_ready = false;

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
        if constexpr (config::workers_number > 1) {
            cur = (cur + 1) % config::workers_number;
        }
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
    [[deprecated]] void poll_completion() noexcept;

    /**
     * @brief handle an non-null cq_entry from the cq of io_uring
     */
    void handle_cq_entry(const liburingcxx::cq_entry *) noexcept;

    /**
     * @brief forward the completion waiting in the overflow buffer.
     *
     * @return true if the overflow buffer is cleared. false otherwise.
     */
    bool try_clear_reap_overflow_buf() noexcept;

  private:
    void init();

    void make_thread_pool();

    void co_spawn(std::coroutine_handle<> entrance) noexcept;

    friend void co_spawn(task<void> &&entrance) noexcept;

    void do_submission_part() noexcept;

    void do_completion_part() noexcept;

    void do_worker_part();

  public:
    io_context(unsigned io_uring_entries, uring::params &io_uring_params)
        : ring(io_uring_entries, io_uring_params)
        , sqring_entries(io_uring_entries) {
        init();
    }

    explicit io_context(
        unsigned io_uring_entries = config::default_io_uring_entries
    )
        : ring(io_uring_entries)
        , sqring_entries(io_uring_entries) {
        init();
    }

    io_context(unsigned io_uring_entries, uring::params &&io_uring_params)
        : io_context(io_uring_entries, io_uring_params) {}

    void probe() const;

    void co_spawn(task<void> &&entrance) noexcept;

    void can_stop() noexcept { will_stop = true; }

    void run();

    inline uring &io() noexcept { return ring; }

    ~io_context() noexcept {
        for (int i = 0; i < config::worker_threads_number; ++i) {
            std::thread &worker_thread = workers[i].host_thread;
            if (worker_thread.joinable()) {
                worker_thread.join();
            }
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
    if (detail::this_thread.worker != nullptr) [[likely]] {
        detail::this_thread.worker->co_spawn(entrance.get_handle());
    } else {
        detail::this_thread.ctx->co_spawn(entrance.get_handle());
    }
    entrance.detach();
}

inline void io_context_stop() noexcept {
    detail::this_thread.ctx->can_stop();
}

inline config::tid_t co_get_tid() noexcept {
    return detail::this_thread.tid;
}

inline auto &this_io_context() noexcept {
    assert(detail::this_thread.ctx != nullptr);
    return *detail::this_thread.ctx;
}

} // namespace co_context
