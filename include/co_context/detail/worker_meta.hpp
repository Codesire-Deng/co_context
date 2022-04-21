#pragma once

#include "co_context/config.hpp"
#include "co_context/detail/swap_zone.hpp"
#include "co_context/detail/submit_info.hpp"
#include "co_context/main_task.hpp"
#include <thread>
#include <queue>

namespace co_context {

class io_context;

namespace detail {

    struct alignas(config::cache_line_size) worker_meta final {
        enum class worker_state : uint8_t { running, idle, blocked };

        using cur_t = config::swap_capacity_width_t;

        /**
         * @brief sharing zone with main thread
         */
        struct sharing_zone {
            alignas(config::cache_line_size
            ) spsc_cursor<cur_t, config::swap_capacity> submit_cur;

            alignas(config::cache_line_size
            ) spsc_cursor<cur_t, config::swap_capacity> reap_cur;
            // worker_state state; // TODO atomic?
            // int temp;
        };

        using tid_t = config::threads_number_width_t;

        alignas(config::cache_line_size) sharing_zone sharing;

        alignas(config::cache_line_size
        ) worker_swap_zone<submit_info> *submit_swap_ptr;
        worker_swap_zone<std::coroutine_handle<>> *reap_swap_ptr;
        tid_t tid;
        std::thread host_thread;
        /*
        std::queue<submit_info> submit_overflow_buf;
        */

        liburingcxx::SQEntry *get_free_sqe() noexcept;

        void submit_sqe(submit_info info) noexcept;

        void submit_non_sqe(submit_info io_info) noexcept;

        /*
        void try_clear_submit_overflow_buf() noexcept;
        */

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
        this->submit_non_sqe(submit_info{.request = entrance.get_io_info_ptr()}
        );
    }

} // namespace detail

} // namespace co_context
