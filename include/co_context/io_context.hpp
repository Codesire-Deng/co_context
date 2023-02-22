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

#include "co_context/config.hpp"
#include "co_context/detail/io_context_meta.hpp"
#include "co_context/detail/thread_meta.hpp"
#include "co_context/detail/thread_safety.hpp"
#include "co_context/detail/worker_meta.hpp"
#include "co_context/task.hpp"
#include <sys/types.h>
#include <thread>

namespace co_context {

using config::cache_line_size;

class [[nodiscard]] io_context final {
  public:
    explicit io_context() noexcept;

    void co_spawn(task<void> &&entrance) noexcept;

    template<safety is_thread_safe>
    void co_spawn(task<void> &&entrance) noexcept;

    void can_stop() noexcept { will_stop = true; }

    // start a standalone thread to run.
    void start();

    void join() {
        if (host_thread.joinable()) {
            host_thread.join();
        }
    }

    ~io_context() noexcept = default;

    /**
     * ban all copying or moving
     */
    io_context(const io_context &) = delete;
    io_context(io_context &&) = delete;
    io_context &operator=(const io_context &) = delete;
    io_context &operator=(io_context &&) = delete;

  private:
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

  private:
    void init();

    // run on the current thread.
    void run();

    friend void co_spawn(task<void> &&entrance) noexcept;

    friend void co_spawn_unsafe(task<void> &&entrance) noexcept;

    void do_submission_part() noexcept;

    void do_completion_part() noexcept;

    void do_worker_part();

  private:
    friend struct detail::worker_meta;
    friend class co_context::mutex;
    friend class co_context::condition_variable;
    friend class co_context::counting_semaphore;
    friend class co_context::detail::lazy_resume_on;
}; // class io_context

inline void io_context::co_spawn(task<void> &&entrance) noexcept {
    this->co_spawn<safety::safe>(std::move(entrance));
}

template<safety is_thread_safe>
inline void io_context::co_spawn(task<void> &&entrance) noexcept {
    auto handle = entrance.get_handle();
    entrance.detach();
    if constexpr (is_thread_safe) {
        worker.co_spawn_auto(handle);
    } else {
        worker.co_spawn_unsafe(handle);
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
    detail::this_thread.worker->co_spawn_unsafe(handle);
}

namespace detail {
    inline void co_spawn_handle(std::coroutine_handle<> handle) noexcept {
        assert(
            detail::this_thread.ctx != nullptr
            && "Can not co_spawn() on the thread "
               "without a running io_context!"
        );
        detail::this_thread.worker->co_spawn_unsafe(handle);
    }
} // namespace detail

inline void io_context_stop() noexcept {
    detail::this_thread.ctx->can_stop();
}

inline auto &this_io_context() noexcept {
    assert(detail::this_thread.ctx != nullptr);
    return *detail::this_thread.ctx;
}

} // namespace co_context
