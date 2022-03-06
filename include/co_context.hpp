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
#include <thread>
#include <cassert>
#include <vector>
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

struct task_info;

static constexpr unsigned task_info_number_per_cache_line =
    cache_line_size / sizeof(task_info *);

template<
    unsigned io_uring_flags,
    unsigned total_threads_number = 8,
    bool perf_mode = true,
    unsigned swap_slots = 1024 / task_info_number_per_cache_line>
class co_context final {
  public:
    static constexpr unsigned task_info_number_per_thread =
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

    using swap_zone =
        std::atomic<task_info *>[swap_slots][worker_threads_number]
                                [task_info_number_per_cache_line];

  private:
    alignas(cache_line_size) swap_zone submit_swap;
    alignas(cache_line_size) swap_zone reap_swap;
    alignas(cache_line_size) uring ring;
    union {
        alignas(cache_line_size) std::thread
            worker_threads[worker_threads_number];
    };

  private:
    const unsigned ring_entries;

  private:
    void worker_run() {
        using namespace std;
        osyncstream synced_out{cout};
        synced_out << "Hello, world from " << this_thread::get_id() << endl;
    }

  public:
    co_context(unsigned io_uring_entries, uring::Params &io_uring_params)
        : ring(io_uring_entries, io_uring_params)
        , ring_entries(io_uring_entries) {
        // worker_threads.reserve(worker_threads_number);
    }

    co_context(unsigned io_uring_entries)
        : ring(io_uring_entries), ring_entries(io_uring_entries) {
        // worker_threads.reserve(worker_threads_number);
    }

    co_context(unsigned io_uring_entries, uring::Params &&io_uring_params)
        : co_context(io_uring_entries, io_uring_params) {}

    void run_thread_pool() {
        for (int i = 0; i < worker_threads_number; ++i)
            std::construct_at(worker_threads+i, &co_context::worker_run, this);
    }

    ~co_context() noexcept {
        for (auto &t : worker_threads) {
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
