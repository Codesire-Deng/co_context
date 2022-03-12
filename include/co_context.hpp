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

#include <iostream>
#include <syncstream>

namespace co_context {

using config::cache_line_size;

class io_context;
class worker_meta;

namespace detail {
    using liburingcxx::SQEntry;
    using liburingcxx::CQEntry;

    struct [[nodiscard]] task_info {
        union {
            SQEntry *sqe;
            CQEntry *cqe;
        };
        std::coroutine_handle<> handle;
        int tid_hint;

        static task_info nop() {
            return task_info{
                .sqe = nullptr,
                .handle{nullptr},
                .tid_hint = -1,
            };
        }
    };

    struct alignas(cache_line_size) thread_meta {
        io_context *ctx;
        worker_meta *worker;
        int tid;
    };

    inline static thread_local thread_meta this_thread;

    struct alignas(cache_line_size) worker_meta {
        enum class worker_state : uint8_t { running, idle, blocked };
        /**
         * @brief sharing zone with main thread
         */
        struct alignas(cache_line_size) {
            worker_state state; // TODO atomic?
            int temp;
        };
        struct swap_cur {
            uint16_t slot = 0;
            uint16_t off = 0;
            void next() noexcept;
        } submit_cur, reap_cur;
        std::queue<detail::task_info *> submit_overflow_buf;

        inline void init(const int thread_index, io_context *const context) {
            assert(submit_overflow_buf.empty());
            detail::this_thread.ctx = context;
            detail::this_thread.tid = thread_index;
#ifdef USE_CPU_AFFINITY
            const unsigned logic_cores = std::thread::hardware_concurrency();
            if constexpr (config::use_hyper_threading) {
                if (thread_index * 2 < logic_cores) {
                    detail::set_cpu_affinity(thread_index * 2);
                } else {
                    detail::set_cpu_affinity(
                        thread_index * 2 % logic_cores + 1);
                }
            } else {
                detail::set_cpu_affinity(thread_index);
            }
#endif
        }

        void hello_world() noexcept {
            {
                using namespace std;
                osyncstream synced_out{cout};
                synced_out << "Hello, world from " << detail::this_thread.tid
                           << endl;
            }
            std::this_thread::yield();
        }

        void run(const int thread_index, io_context *const context) {
            init(thread_index, context);

            hello_world();
            // while (true)
            // std::this_thread::sleep_for(std::chrono::seconds(10));
            // while (true) {}
        }

        void run_test_swap(
            const int thread_index, io_context *const context) noexcept;
    };

} // namespace detail

inline constexpr uint16_t task_info_number_per_cache_line =
    cache_line_size / sizeof(detail::task_info *);

class [[nodiscard]] io_context final {
  public:
    static constexpr unsigned io_uring_flags = config::io_uring_flags;

    static constexpr unsigned total_threads_number =
        config::total_threads_number;

    static constexpr bool low_latency_mode = config::low_latency_mode;

    static constexpr uint16_t swap_slots =
        config::swap_capacity / task_info_number_per_cache_line;

    static constexpr uint16_t task_info_number_per_thread =
        task_info_number_per_cache_line * swap_slots;

    static constexpr bool is_SQPOLL = io_uring_flags & IORING_SETUP_SQPOLL;

    /**
     * One thread for io_context submit/reap. Another thread for io_uring (if
     * needed).
     */
    static constexpr unsigned worker_threads_number =
        total_threads_number - 1 - is_SQPOLL;

    static_assert(worker_threads_number >= 1);

    using uring = liburingcxx::URing<io_uring_flags>;

    using task_info = detail::task_info;

    using swap_zone = task_info
        *[swap_slots][worker_threads_number][task_info_number_per_cache_line];

    // Not necessary to read/write atomicly, since there is no order consistency
    // problem.
    /* using swap_zone =
        std::atomic<task_info *>[swap_slots][worker_threads_number]
                                [task_info_number_per_cache_line]; */

  private:
    alignas(cache_line_size) uring ring;
    alignas(cache_line_size) swap_zone submit_swap;
    alignas(cache_line_size) swap_zone reap_swap;
    // std::counting_semaphore<1> idle_worker_quota{1};

  public:
    using worker_meta = detail::worker_meta;
    friend worker_meta;

  private:
    alignas(cache_line_size) worker_meta worker[worker_threads_number];

    union alignas(cache_line_size) {
        std::thread worker_threads[worker_threads_number];
    };

  public:
    struct ctx_swap_cur {
        uint16_t slot = 0;
        unsigned tid = 0;
        uint16_t off = 0;

        inline void next() {
            if (++off == task_info_number_per_cache_line) [[unlikely]] {
                off = 0;
                if (++tid == worker_threads_number) [[unlikely]] {
                    tid = 0;
                    if (++slot == swap_slots) [[unlikely]] { slot = 0; }
                }
            }
        }
    };

  private:
    const unsigned ring_entries;
    int temp = 0;

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
    }

    void probe() const {
        using namespace std;
        cout << "number of logic cores: " << std::thread::hardware_concurrency()
             << endl;
        cout << "size of io_context: " << sizeof(io_context) << endl;
        cout << "size of uring: " << sizeof(uring) << endl;
        cout << "size of single swap_zone: " << sizeof(swap_zone) << endl;
        cout << "size of single atomic_ptr: " << sizeof(submit_swap[0][0][0])
             << endl;
        // cout << "swap_zone is_lock_free: "
        //      << submit_swap[0][0][0].is_lock_free() << endl;
        cout << "number of worker_threads: " << worker_threads_number << endl;
        cout << "size of single swap_zone per thread: "
             << swap_slots * cache_line_size << endl;
        cout << "length of fixed swap_queue: "
             << swap_slots * task_info_number_per_cache_line << endl;
        cout << "size of single worker_meta: " << sizeof(worker_meta) << endl;
    }

    void make_thread_pool() {
        for (int i = 0; i < worker_threads_number; ++i)
            std::construct_at(
                worker_threads + i, &worker_meta::run, worker + i, i, this);
    }

    void make_test_thread_pool() {
        for (int i = 0; i < worker_threads_number; ++i)
            std::construct_at(
                worker_threads + i, &worker_meta::run_test_swap, worker + i, i,
                this);
    }

    void run_test_swap() {
        ctx_swap_cur cur;
        for (int64_t recv = 0; recv < test::swap_tot / worker_threads_number
                                          * worker_threads_number;) {
            while (submit_swap[cur.slot][cur.tid][cur.off] == nullptr)
                cur.next();
            submit_swap[cur.slot][cur.tid][cur.off] = nullptr;
            ++recv;
            cur.next();
        }
        printf("io_context::run(): done\n");
    }

    void run() {
#ifdef USE_CPU_AFFINITY
        detail::set_cpu_affinity(std::thread::hardware_concurrency() - 1);
#endif
    }

    ~io_context() noexcept {
        for (std::thread &t : worker_threads) {
            t.join();
            std::destroy_at(&t);
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

namespace detail {

    inline void worker_meta::swap_cur::next() noexcept {
        if (++off == task_info_number_per_cache_line) {
            off = 0;
            if (++slot == io_context::swap_slots) { slot = 0; }
        }
    }

    inline void worker_meta::run_test_swap(
        const int thread_index, io_context *const context) noexcept {
        init(thread_index, context);
        auto &submit_swap = context->submit_swap;
        const int tid = detail::this_thread.tid;

        for (int64_t send = 0;
             send < test::swap_tot / io_context::worker_threads_number;) {
            swap_cur &cur = submit_cur;
            while (submit_swap[cur.slot][tid][cur.off] != nullptr) cur.next();
            submit_swap[cur.slot][tid][cur.off] = (detail::task_info *)this;
            ++send;
            cur.next();
        }
    }

} // namespace detail

} // namespace co_context
