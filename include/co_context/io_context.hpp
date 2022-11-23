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
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <queue>
#include <sys/types.h>
#include <thread>
#include <vector>

namespace co_context {

namespace detail {
    struct io_context_meta {
        std::mutex mtx;
        std::condition_variable cv;
        config::ctx_id_t create_count; // Do not initialize this
        config::ctx_id_t ready_count;  // Do not initialize this
    };
} // namespace detail

using config::cache_line_size;

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
     * Thread-local read/write data
     * ---------------------------------------------------
     */

    // The data of worker
    alignas(cache_line_size) worker_meta worker;

    std::thread host_thread;

    __pid_t tid;

    // id among all io_contexts
    config::ctx_id_t id;

    // should io_context stop
    bool will_stop = false;

    /**
     * ---------------------------------------------------
     * read-only sharing data (No data)
     * ---------------------------------------------------
     */

    /**
     * ---------------------------------------------------
     * Static sharing read/write data
     * ---------------------------------------------------
     */

    static detail::io_context_meta meta;

  private:
    void init();

    // run on the current thread.
    void run();

    friend void co_spawn(task<void> &&entrance) noexcept;

    friend void co_spawn_unsafe(task<void> &&entrance) noexcept;

    void co_spawn(std::coroutine_handle<> entrance) noexcept;

    void co_spawn_unsafe(std::coroutine_handle<> entrance) noexcept;

    void do_submission_part() noexcept;

    void do_completion_part() noexcept;

    void do_worker_part();

  public:
    explicit io_context() noexcept {
        std::lock_guard lg{meta.mtx};
        this->id = meta.create_count++;
        log::d(
            "&meta.create_count = %lx  value = %u\n", &meta.create_count,
            meta.create_count
        );
    }

    void co_spawn(task<void> &&entrance) noexcept;

    void co_spawn_unsafe(task<void> &&entrance) noexcept;

    void can_stop() noexcept { will_stop = true; }

    // start a standalone thread to run.
    void start();

    void join() {
        if (host_thread.joinable()) {
            host_thread.join();
        }
    }

    inline uring &ring() noexcept { return worker.ring; }

    ~io_context() noexcept = default;

    /**
     * ban all copying or moving
     */
    io_context(const io_context &) = delete;
    io_context(io_context &&) = delete;
    io_context &operator=(const io_context &) = delete;
    io_context &operator=(io_context &&) = delete;
}; // class io_context

// Must be called by corresponding thread.
inline void io_context::init() {
    this->tid = ::gettid();
    detail::this_thread.ctx = this;
    detail::this_thread.ctx_id = this->id;

    this->worker.init(config::default_io_uring_entries);
}

inline void io_context::co_spawn(task<void> &&entrance) noexcept {
    auto handle = entrance.get_handle();
    entrance.detach();
    this->co_spawn(handle);
}

inline void io_context::co_spawn_unsafe(task<void> &&entrance) noexcept {
    auto handle = entrance.get_handle();
    entrance.detach();
    this->co_spawn_unsafe(handle);
}

inline void io_context::co_spawn(std::coroutine_handle<> handle) noexcept {
    // MT-unsafe in some scenes (for meta.ready_count == 0)
    // before calling io_context::start(), this_thread.ctx is nullptr.
    if (detail::this_thread.ctx == this || meta.ready_count == 0) [[unlikely]] {
        worker.co_spawn_unsafe(handle);
    } else {
        worker.co_spawn_safe_eager(handle);
    }
}

inline void io_context::co_spawn_unsafe(std::coroutine_handle<> handle
) noexcept {
    worker.co_spawn_unsafe(handle);
}

inline void io_context::do_worker_part() {
    auto num = worker.number_to_schedule();
    log::v("worker[%u] will run %u times...\n", id, num);
    while (num-- > 0) {
        worker.work_once();
    }
}

inline void io_context::do_submission_part() noexcept {
    worker.poll_submission();
}

inline void io_context::do_completion_part() noexcept {
    // NOTE in the future: if an IO generates multiple requests_to_reap，
    // it must be counted carefully
    if (worker.requests_to_reap > 0) [[likely]] {
        auto num = worker.poll_completion();

        // io_context will block itself here
        uint32_t will_not_wait =
            num | worker.has_task_ready() | worker.need_ring_submit;
        if (will_not_wait == 0) [[unlikely]] {
            worker.wait_uring();
            num = worker.poll_completion();
            if constexpr (config::is_log_w) {
                if (num == 0) [[unlikely]] {
                    log::w("wait_cq_entry() gets 0 cqe.\n");
                }
            }
        }
    } else if (!worker.has_task_ready()) [[unlikely]] {
        will_stop = true;
    }
}

inline void co_spawn(task<void> &&entrance) noexcept {
    assert(
        detail::this_thread.ctx != nullptr
        && "Can not co_spawn() on the thread "
           "without a running io_context!"
    );
    auto handle = entrance.get_handle();
    entrance.detach();
    detail::this_thread.ctx->co_spawn_unsafe(handle);
}

inline void io_context_stop() noexcept {
    detail::this_thread.ctx->can_stop();
}

inline auto &this_io_context() noexcept {
    assert(detail::this_thread.ctx != nullptr);
    return *detail::this_thread.ctx;
}

} // namespace co_context
