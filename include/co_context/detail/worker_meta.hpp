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

        using tid_t = config::threads_number_width_t;

        alignas(config::cache_line_size) sharing_zone sharing;

        worker_swap_cur submit_cur;
        worker_swap_cur reap_cur;
        worker_swap_zone<task_info_ptr> *submit_swap_ptr;
        worker_swap_zone<std::coroutine_handle<>> *reap_swap_ptr;
        tid_t tid;
        std::queue<task_info *> submit_overflow_buf;

        void submit(task_info_ptr io_info) noexcept;

        void try_clear_submit_overflow_buf() noexcept;

        std::coroutine_handle<> schedule() noexcept;

        void init(const int thread_index, io_context *const context);

        void co_spawn(main_task entrance) noexcept;

        void worker_run(const int thread_index, io_context *const context);
    };

    inline void worker_meta::co_spawn(main_task entrance) noexcept {
        log::v(
            "worker[%u] co_spawn coro %lx\n", this_thread.tid,
            entrance.get_io_info_ptr()->handle.address()
        );
        this->submit(entrance.get_io_info_ptr());
    }

} // namespace detail

} // namespace co_context
