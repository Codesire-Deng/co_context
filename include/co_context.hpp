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

#include <new>
#include <memory>
#include <atomic>
#include <semaphore>
#include <thread>
#include <chrono>
#include <cassert>
#include <vector>
#include <queue>
#include "uring.hpp"
#include "co_context/task.hpp"
#include "co_context/config.hpp"
#include "co_context/set_cpu_affinity.hpp"
#include "co_context/task_info.hpp"
#include "co_context/main_task.hpp"

#include <iostream>
#include <syncstream>

namespace co_context {

using config::cache_line_size;

class io_context;
// class eager_io;
// class lazy_io;

namespace config {
    inline constexpr bool is_SQPOLL = io_uring_flags & IORING_SETUP_SQPOLL;

    inline constexpr unsigned worker_threads_number =
        total_threads_number - 1 - is_SQPOLL;
} // namespace config

namespace detail {
    class worker_meta;

    using swap_zone =
        task_info_ptr[config::worker_threads_number][config::swap_capacity];

    struct alignas(cache_line_size) thread_meta {
        io_context *ctx;
        worker_meta *worker;
        uint32_t tid;
    };

    inline static thread_local thread_meta this_thread;

    struct alignas(cache_line_size) worker_meta {
        enum class worker_state : uint8_t { running, idle, blocked };
        /**
         * @brief sharing zone with main thread
         */
        struct sharing_zone {
            std::thread host_thread;
            worker_state state; // TODO atomic?
            int temp;
        };

        alignas(cache_line_size) sharing_zone sharing;

        struct swap_cur {
            uint16_t off = 0;
            void next() noexcept;
            bool try_find_empty(swap_zone &swap) noexcept;
            bool try_find_exist(swap_zone &swap) noexcept;
            void release(swap_zone &swap, task_info_ptr task) const noexcept;
            void
            release_relaxed(swap_zone &swap, task_info_ptr task) const noexcept;
        };
        swap_cur submit_cur;
        swap_cur reap_cur;
        std::queue<task_info *> submit_overflow_buf;

        void submit(task_info *io_info) noexcept;

        std::coroutine_handle<> schedule() noexcept;

        void init(const int thread_index, io_context *const context);

        void co_spawn(main_task entrance) noexcept;

        void run(const int thread_index, io_context *const context) {
            init(thread_index, context);
            // printf("%d run\n", thread_index);
            std::this_thread::sleep_for(std::chrono::seconds{1});

            while (true) {
                auto coro = this->schedule();
                // printf("%d\n", thread_index);
                coro.resume();
            }
        }

        void run_test_swap(
            const int thread_index, io_context *const context) noexcept;
    };

} // namespace detail

/*
inline constexpr uint16_t task_info_number_per_cache_line =
    cache_line_size / sizeof(detail::task_info *);
*/

class [[nodiscard]] io_context final {
  public:
    /*
    static constexpr uint16_t swap_slots =
        config::swap_capacity / task_info_number_per_cache_line;

    static constexpr uint16_t task_info_number_per_thread =
        task_info_number_per_cache_line * swap_slots;
    */

    /**
     * One thread for io_context submit/reap. Another thread for io_uring (if
     * needed).
     */

    static_assert(config::worker_threads_number >= 1);

    using uring = liburingcxx::URing<config::io_uring_flags>;

    using task_info = detail::task_info;

    using task_info_ptr = detail::task_info_ptr;

    // May not necessary to read/write atomicly.
    using swap_zone =
        task_info_ptr[config::worker_threads_number][config::swap_capacity];

  private:
    alignas(cache_line_size) uring ring;
    alignas(cache_line_size) swap_zone submit_swap;
    alignas(cache_line_size) swap_zone reap_swap;

    struct ctx_swap_cur {
        uint16_t tid = 0;
        uint16_t off = 0;

        inline void next() {
            if (++tid == config::worker_threads_number) [[unlikely]] {
                tid = 0;
                off = (off + 1) % config::swap_capacity;
            }
        }

        inline bool try_find_empty(const swap_zone &swap) noexcept {
            constexpr uint32_t swap_size =
                sizeof(swap_zone) / sizeof(task_info_ptr);
            int i = 0;
            while (i < swap_size && swap[tid][off] != nullptr) {
                ++i;
                next();
            }
            if (i != swap_size) [[likely]] {
                return true;
            } else {
                next();
                return false;
            }
        }

        // inline void wait_for_empty(const swap_zone &swap) noexcept {
        //     while (swap[tid][off] != nullptr) next();
        // }

        inline bool try_find_exist(const swap_zone &swap) noexcept {
            constexpr uint32_t swap_size =
                sizeof(swap_zone) / sizeof(task_info_ptr);
            int i = 0;
            while (i < swap_size && swap[tid][off] == nullptr) {
                ++i;
                next();
            }
            if (i != swap_size) [[likely]] {
                return true;
            } else {
                next();
                return false;
            }
        }

        // inline void wait_for_exist(const swap_zone &swap) noexcept {
        //     while (swap[tid][off] == nullptr) next();
        // }

        // release the task into the swap zone
        inline void
        release(swap_zone &swap, task_info_ptr task) const noexcept {
            std::atomic_store_explicit(
                reinterpret_cast<std::atomic<task_info_ptr> *>(&swap[tid][off]),
                task, std::memory_order_release);
        }

        inline void
        release_relaxed(swap_zone &swap, task_info_ptr task) const noexcept {
            std::atomic_store_explicit(
                reinterpret_cast<std::atomic<task_info_ptr> *>(&swap[tid][off]),
                task, std::memory_order_relaxed);
        }
    };

    // place main thread's high frequency data here
    ctx_swap_cur s_cur;
    ctx_swap_cur r_cur;
    std::queue<task_info_ptr> submit_overflow_buf;
    std::queue<task_info_ptr> reap_overflow_buf;

    // std::counting_semaphore<1> idle_worker_quota{1};

  public:
    using worker_meta = detail::worker_meta;
    friend worker_meta;

  private:
    alignas(cache_line_size) worker_meta worker[config::worker_threads_number];

  private:
    const unsigned ring_entries;

  private:
    void forward_task(task_info_ptr task) noexcept {
        // TODO optimize scheduling strategy
        if (r_cur.try_find_empty(reap_swap)) [[likely]] {
            reap_swap[r_cur.tid][r_cur.off] = task;
            r_cur.next();
        } else {
            reap_overflow_buf.push(task);
        }
    }

    bool try_submit(task_info_ptr task) noexcept {
        if (task->type == task_info::task_type::co_spawn) {
            forward_task(task);
            return true;
        }

        if (ring.SQSpaceLeft() == 0) [[unlikely]] {
            return false; // SQRing is full
        }

        liburingcxx::SQEntry *sqe = ring.getSQEntry();

        sqe->cloneFrom(*task->sqe);
        ring.submit();
        return true;
    }

    /**
     * @brief poll the submission swap zone
     * @return if submit_swap capacity might be healthy
     */
    bool poll_submission() noexcept {
        // submit round
        if (!s_cur.try_find_exist(submit_swap)) return false;

        task_info_ptr const io_info = submit_swap[s_cur.tid][s_cur.off];
        submit_swap[s_cur.tid][s_cur.off] = nullptr;
        s_cur.next();

        if (try_submit(io_info)) [[likely]] {
            return true;
        } else {
            submit_overflow_buf.push(io_info);
            return false;
        }
    }

    bool try_clear_submit_overflow_buf() noexcept {
        while (!submit_overflow_buf.empty()) {
            task_info_ptr task = submit_overflow_buf.front();
            // OPTIMIZE impossible for task_type::co_spawn
            if (try_submit(task)) {
                submit_overflow_buf.pop();
            } else {
                return false;
            }
        }
        return true;
    }

    bool try_reap(task_info_ptr task) noexcept {
        if (!r_cur.try_find_empty(reap_swap)) [[unlikely]] { return false; }

        r_cur.release(reap_swap, task);
        r_cur.next();
        return true;
    }

    /**
     * @brief poll the completion swap zone
     * @return if reap_swap capacity might be healthy
     */
    bool poll_completion() noexcept {
        // reap round
        liburingcxx::CQEntry *polling_cqe = ring.peekCQEntry();
        if (polling_cqe == nullptr) return true;

        task_info_ptr io_info =
            reinterpret_cast<task_info_ptr>(polling_cqe->getData());
        io_info->result = polling_cqe->getRes();
        ring.SeenCQEntry(polling_cqe);

        if (try_reap(io_info)) [[likely]] {
            return true;
        } else {
            reap_overflow_buf.push(io_info);
            return false;
        }
    }

    bool try_clear_reap_overflow_buf() noexcept {
        while (!reap_overflow_buf.empty()) {
            task_info_ptr task = reap_overflow_buf.front();
            // OPTIMIZE impossible for task_type::co_spawn
            if (try_reap(task)) {
                reap_overflow_buf.pop();
            } else {
                return false;
            }
        }
        return true;
    }

  public:
    io_context(unsigned io_uring_entries, uring::Params &io_uring_params)
        : ring(io_uring_entries, io_uring_params)
        , ring_entries(io_uring_entries) {
        init();
    }

    io_context(unsigned io_uring_entries)
        : ring(io_uring_entries), ring_entries(io_uring_entries) {
        init();
    }

    io_context(unsigned io_uring_entries, uring::Params &&io_uring_params)
        : io_context(io_uring_entries, io_uring_params) {}

    void init() noexcept {
        memset(submit_swap, 0, sizeof(submit_swap));
        memset(reap_swap, 0, sizeof(reap_swap));
        detail::this_thread.ctx = this;
        detail::this_thread.tid = std::thread::hardware_concurrency() - 1;
    }

    void probe() const {
        using namespace std;
        using namespace co_context::config;
        cout << "number of logic cores: " << std::thread::hardware_concurrency()
             << endl;
        cout << "size of io_context: " << sizeof(io_context) << endl;
        cout << "size of uring: " << sizeof(uring) << endl;
        cout << "size of two swap_zone: " << 2 * sizeof(swap_zone) << endl;
        // cout << "swap_zone is_lock_free: "
        //      << submit_swap[0][0][0].is_lock_free() << endl;
        cout << "number of worker_threads: " << worker_threads_number << endl;
        cout << "swap_capacity per thread: " << swap_capacity << endl;
        cout << "size of single worker_meta: " << sizeof(worker_meta) << endl;
    }

    void make_thread_pool() {
        for (int i = 0; i < config::worker_threads_number; ++i)
            worker[i].sharing.host_thread =
                std::thread{&worker_meta::run, worker + i, i, this};
    }

    void co_spawn(main_task entrance) {
        const unsigned tid = detail::this_thread.tid;
        if (tid < config::worker_threads_number)
            return worker[tid].co_spawn(entrance);
        if (r_cur.try_find_empty(reap_swap)) [[likely]] {
            r_cur.release(reap_swap, entrance.get_io_info_ptr());
            r_cur.next();
        } else {
            reap_overflow_buf.push(entrance.get_io_info_ptr());
        }
    }

    [[noreturn]] void run() {
        detail::this_thread.worker = nullptr;
#ifdef USE_CPU_AFFINITY
        detail::this_thread.tid = std::thread::hardware_concurrency() - 1;
        detail::set_cpu_affinity(detail::this_thread.tid);
#endif
        make_thread_pool();

        while (true) {
            if (try_clear_submit_overflow_buf()) {
                for (uint8_t i = 0; i < config::submit_poll_rounds; ++i) {
                    if (!poll_submission()) break;
                    // poll_submission();
                }
            }
            if (try_clear_reap_overflow_buf()) {
                for (uint8_t i = 0; i < config::reap_poll_rounds; ++i) {
                    if (!poll_completion()) break;
                    // poll_completion();
                }
            }
            // std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    inline uring &io() noexcept { return ring; }

    ~io_context() noexcept {
        for (int i = 0; i < config::worker_threads_number; ++i) {
            std::thread &t = worker[i].sharing.host_thread;
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

inline void co_spawn(main_task entrance) noexcept {
    detail::this_thread.ctx->co_spawn(entrance);
}

namespace detail {

    inline void worker_meta::swap_cur::next() noexcept {
        off = (off + 1) % co_context::config::swap_capacity;
    }

    inline bool
    worker_meta::swap_cur::try_find_empty(swap_zone &swap) noexcept {
        uint16_t i = 0;
        const uint32_t tid = this_thread.tid;
        while (i < config::swap_capacity && swap[tid][off] != nullptr) {
            next();
        }
        if (i == config::swap_capacity) [[unlikely]] {
            next();
            return false;
        }
        return true;
    }

    inline bool
    worker_meta::swap_cur::try_find_exist(swap_zone &swap) noexcept {
        uint16_t i = 0;
        const uint32_t tid = this_thread.tid;
        while (i < config::swap_capacity && swap[tid][off] == nullptr) {
            next();
        }
        if (i == config::swap_capacity) [[unlikely]] {
            next();
            return false;
        }
        return true;
    }

    inline void worker_meta::swap_cur::release(
        swap_zone &swap, task_info_ptr task) const noexcept {
        const uint32_t tid = this_thread.tid;
        std::atomic_store_explicit(
            reinterpret_cast<std::atomic<task_info_ptr> *>(&swap[tid][off]),
            task, std::memory_order_release);
    }

    inline void worker_meta::swap_cur::release_relaxed(
        swap_zone &swap, task_info_ptr task) const noexcept {
        const uint32_t tid = this_thread.tid;
        std::atomic_store_explicit(
            reinterpret_cast<std::atomic<task_info_ptr> *>(&swap[tid][off]),
            task, std::memory_order_relaxed);
    }

    inline void
    worker_meta::init(const int thread_index, io_context *const context) {
        assert(submit_overflow_buf.empty());
        detail::this_thread.ctx = context;
        detail::this_thread.worker = this;
        detail::this_thread.tid = thread_index;
#ifdef USE_CPU_AFFINITY
        const unsigned logic_cores = std::thread::hardware_concurrency();
        if constexpr (config::use_hyper_threading) {
            if (thread_index * 2 < logic_cores) {
                detail::set_cpu_affinity(thread_index * 2);
            } else {
                detail::set_cpu_affinity(thread_index * 2 % logic_cores + 1);
            }
        } else {
            detail::set_cpu_affinity(thread_index);
        }
#endif
    }

    inline void worker_meta::co_spawn(main_task entrance) noexcept {
        this->submit(entrance.get_io_info_ptr());
    }

    inline void worker_meta::submit(task_info_ptr io_info) noexcept {
        const unsigned tid = this_thread.tid;
        auto &ctx = *this_thread.ctx;
        auto &cur = this->submit_cur;
        // std::cerr << cur.slot << " " << tid << cur.off << std::endl;
        if (cur.try_find_empty(ctx.submit_swap)) [[likely]] {
            cur.release(ctx.submit_swap, io_info);
            cur.next();
        } else {
            this->submit_overflow_buf.push(io_info);
        }
    }

    inline std::coroutine_handle<> worker_meta::schedule() noexcept {
        const uint32_t tid = this_thread.tid;
        auto &ctx = *this_thread.ctx;
        auto &cur = this->reap_cur;

        while (true) {
            // handle overflowed submission
            if (!this->submit_overflow_buf.empty()) {
                auto &s_cur = this->submit_cur;
                if (s_cur.try_find_empty(ctx.submit_swap)) {
                    task_info_ptr task = this->submit_overflow_buf.front();
                    this->submit_overflow_buf.pop();
                    s_cur.release_relaxed(ctx.submit_swap, task);
                    s_cur.next();
                }
            }

            if (cur.try_find_exist(ctx.reap_swap)) [[likely]] {
                const task_info_ptr io_info = ctx.reap_swap[tid][cur.off];
                ctx.reap_swap[tid][cur.off] = nullptr;
                cur.next();
                return io_info->handle;
            }
            // TODO consider tid_hint here
        }
    }

} // namespace detail

} // namespace co_context
