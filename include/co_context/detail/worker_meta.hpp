#pragma once

#include "co_context/config.hpp"
#include "co_context/detail/swap_zone.hpp"
#include "co_context/main_task.hpp"
#include <thread>
#include <queue>

namespace co_context {

class io_context;

namespace detail {

    struct alignas(config::cache_line_size) worker_meta final {
        enum class worker_state : uint8_t { running, idle, blocked };
        /**
         * @brief sharing zone with main thread
         */
        struct sharing_zone {
            std::thread host_thread;
            // worker_state state; // TODO atomic?
            // int temp;
        };

        alignas(config::cache_line_size) sharing_zone sharing;

        detail::worker_swap_cur submit_cur;
        detail::worker_swap_cur reap_cur;
        std::queue<task_info *> submit_overflow_buf;

        void submit(task_info_ptr io_info) noexcept;

        std::coroutine_handle<> schedule() noexcept;

        void init(const int thread_index, io_context *const context);

        void co_spawn(main_task entrance) noexcept;

        void run(const int thread_index, io_context *const context);
    };

} // namespace detail

} // namespace co_context
