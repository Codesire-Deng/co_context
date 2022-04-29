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

// class eager_io;
// class lazy_io;

class [[nodiscard]] io_context final {
  private:
    using uring = liburingcxx::URing<config::io_uring_flags>;

    using task_info = detail::task_info;

    friend class detail::worker_meta;
    using worker_meta = detail::worker_meta;

  private:
    // multithread sharing
    alignas(cache_line_size) uring ring;
    alignas(cache_line_size) worker_meta worker[config::workers_number];

    // local read/write, high frequency data.
    alignas(cache_line_size) config::tid_t s_cur = 0;
    config::tid_t r_cur = 0;
    int32_t requests_to_reap = 0;
    bool need_ring_submit = false;
    bool will_stop = false;
    /*
    std::queue<task_info *> submit_overflow_buf;
    */
    std::queue<detail::reap_info> reap_overflow_buf;

    // read-only sharing
    alignas(cache_line_size) const unsigned sqring_entries;
    const liburingcxx::SQEntry *sqes_addr;

  private:
    inline static void cur_next(config::tid_t &context_cur) noexcept {
        if constexpr (config::workers_number > 1)
            context_cur = (context_cur + 1) % config::workers_number;
    }

    [[deprecated]] friend unsigned compress_sqe(
        const io_context *self, const liburingcxx::SQEntry *sqe
    ) noexcept;

    [[deprecated]] inline liburingcxx::SQEntry *
    decompress_sqe(unsigned compressed_sqe) const noexcept {
        return const_cast<liburingcxx::SQEntry *>(sqes_addr + compressed_sqe);
    }

  private:
    [[deprecated, nodiscard]] bool is_sqe(const liburingcxx::SQEntry *suspect
    ) const noexcept;

    void forward_task(std::coroutine_handle<> handle) noexcept;

    void handle_semaphore_release(task_info *sem_release) noexcept;

    void handle_condition_variable_notify(task_info *cv_notify) noexcept;

    bool try_find_submit_worker_relaxed() noexcept;

    [[deprecated]] bool try_find_submit_worker_acquire() noexcept;

    bool try_find_reap_worker_relaxed() noexcept;

    [[deprecated]] bool try_find_reap_worker_acquire() noexcept;

    void try_submit(detail::submit_info &info) noexcept;

    /**
     * @brief poll the submission swap zone
     * @return if submit_swap capacity might be healthy
     */
    bool poll_submission() noexcept;

    /*
    bool try_clear_submit_overflow_buf() noexcept;
    */

    bool try_reap(detail::reap_info info) noexcept;

    void reap_or_overflow(detail::reap_info info) noexcept;

    /**
     * @brief poll the completion swap zone
     * @return if load exists and capacity of reap_swap might be healthy
     */
    void poll_completion() noexcept;

    bool try_clear_reap_overflow_buf() noexcept;

    [[noreturn]] void stop() noexcept {
        log::i("ctx stopped\n");
        ::exit(0);
    }

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

    void init() noexcept;

    void probe() const;

    void make_thread_pool();

    void co_spawn(task<void> &&entrance);

    void co_spawn(std::coroutine_handle<> entrance);

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
