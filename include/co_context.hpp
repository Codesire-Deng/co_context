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
#include <coroutine>
#include <chrono>
#include <cassert>
#include <vector>
#include <queue>
#include "uring.hpp"
#include <iostream>
#include <syncstream>

namespace co_context {

#if __cpp_lib_hardware_interference_size >= 201603
inline constexpr size_t cache_line_size =
    std::hardware_destructive_interference_size;
#else
inline constexpr size_t cache_line_size = 64;
#endif

namespace test {
    inline constexpr int64_t swap_tot = 1'250'000'000;
} // namespace test

namespace detail {
    using liburingcxx::SQEntry;
    using liburingcxx::CQEntry;

    struct task_info {
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
} // namespace detail

static constexpr uint16_t task_info_number_per_cache_line =
    cache_line_size / sizeof(detail::task_info *);

template<
    unsigned io_uring_flags,
    unsigned total_threads_number = 8,
    bool perf_mode = true,
    uint16_t swap_slots = 1024 / task_info_number_per_cache_line>
class co_context final {
  public:
    static constexpr uint16_t task_info_number_per_thread =
        task_info_number_per_cache_line * swap_slots;

    static constexpr bool is_SQPOLL = io_uring_flags & IORING_SETUP_SQPOLL;

    /**
     * One thread for co_context submit/reap. Another thread for io_uring (if
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
    std::counting_semaphore<1> idle_worker_quota{1};

    enum class worker_state : uint8_t { running, idle, blocked };

    friend class worker_meta;

    struct alignas(cache_line_size) worker_meta {
        /**
         * @brief sharing zone with main thread
         */
        struct alignas(cache_line_size) {
            worker_state state; // TODO atomic?
            co_context *ctx;
            int tid;
            int temp;
        };
        struct swap_cur {
            uint16_t slot = 0;
            uint16_t off = 0;
            inline void next() {
                if (++off == task_info_number_per_cache_line)  {
                    off = 0;
                    if (++slot == swap_slots)  { slot = 0; }
                }
            }
        } submit_cur, reap_cur;
        std::queue<task_info *> submit_overflow_buf;

        inline void
        init(const int thread_index, co_context *const context) noexcept {
            assert(submit_overflow_buf.empty());
            this->ctx = context;
            this->tid = thread_index;
        }

        void hello_world() noexcept {
            using namespace std;
            {
                osyncstream synced_out{cout};
                synced_out << "Hello, world from " << tid << endl;
            }
            this_thread::yield();
        }

        void run(const int thread_index, co_context *const context) {
            init(thread_index, context);
            auto &submit_swap = ctx->submit_swap;

            
        }

        void run_test_swap(const int thread_index, co_context *const context) {
            init(thread_index, context);
            auto &submit_swap = ctx->submit_swap;

            for (int64_t send = 0;
                 send < test::swap_tot / worker_threads_number;) {
                swap_cur &cur = submit_cur;
                while (submit_swap[cur.slot][tid][cur.off] != nullptr)
                    cur.next();
                submit_swap[cur.slot][tid][cur.off] = (task_info *)this;
                ++send;
                cur.next();
            }
        }

    } worker[worker_threads_number];

    union alignas(cache_line_size) {
        std::thread worker_threads[worker_threads_number];
    };

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
    co_context(unsigned io_uring_entries, uring::Params &io_uring_params)
        : ring(io_uring_entries, io_uring_params)
        , ring_entries(io_uring_entries) {
        memset(submit_swap, 0, sizeof(submit_swap));
        memset(reap_swap, 0, sizeof(reap_swap));
    }

    co_context(unsigned io_uring_entries)
        : ring(io_uring_entries), ring_entries(io_uring_entries) {
        memset(submit_swap, 0, sizeof(submit_swap));
        memset(reap_swap, 0, sizeof(reap_swap));
    }

    co_context(unsigned io_uring_entries, uring::Params &&io_uring_params)
        : co_context(io_uring_entries, io_uring_params) {}

    void probe() const {
        using namespace std;
        cout << "size of co_context: " << sizeof(co_context) << endl;
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
                worker_threads + i, &worker_meta::run_test_swap, worker + i, i, this);
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
        printf("co_context::run(): done\n");
    }

    void run() {
        
    }

    ~co_context() noexcept {
        for (std::thread &t : worker_threads) {
            t.join();
            std::destroy_at(&t);
        }
    }

    /**
     * ban all copying or moving
     */
    co_context(const co_context &) = delete;
    co_context(co_context &&) = delete;
    co_context &operator=(const co_context &) = delete;
    co_context &operator=(co_context &&) = delete;
};

} // namespace co_context
