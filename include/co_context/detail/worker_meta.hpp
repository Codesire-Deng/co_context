#pragma once

#include "co_context/config.hpp"
#include "co_context/detail/swap_zone.hpp"
#include "co_context/detail/submit_info.hpp"
#include "co_context/detail/reap_info.hpp"
#include "co_context/task.hpp"
#include "co_context/log/log.hpp"
#include <thread>
#include <queue>

namespace co_context {

class io_context;

namespace detail {

    struct worker_meta final {
        enum class [[deprecated]] worker_state : uint8_t{
            running, idle, blocked};

        using cur_t = config::cur_t;

        /**
         * @brief sharing zone with main thread
         */
        struct sharing_zone {
            alignas(config::cache_line_size
            ) spsc_cursor<cur_t, config::swap_capacity> submit_cur;

            alignas(config::cache_line_size
            ) worker_swap_zone<detail::submit_info> submit_swap;

            alignas(config::cache_line_size
            ) spsc_cursor<cur_t, config::swap_capacity> reap_cur;

            alignas(config::cache_line_size
            ) worker_swap_zone<detail::reap_info> reap_swap;
        };

        using tid_t = config::threads_number_size_t;

        alignas(config::cache_line_size) sharing_zone sharing;

        alignas(config::cache_line_size) io_context *ctx = nullptr;
        cur_t local_submit_tail = 0;
        tid_t tid;
        std::thread host_thread;
        /*
        std::queue<submit_info> submit_overflow_buf;
        */

        liburingcxx::SQEntry *get_free_sqe() noexcept;

        [[deprecated]] void swap_last_two_sqes() noexcept;

        void submit_sqe() noexcept;

        void submit_non_sqe(uintptr_t typed_task) noexcept;

        /*
        void try_clear_submit_overflow_buf() noexcept;
        */

        std::coroutine_handle<> schedule() noexcept;

        cur_t number_to_schedule_relaxed() noexcept {
            const auto &cur = this->sharing.reap_cur;
            return cur.size();
        }

        std::coroutine_handle<> try_schedule() noexcept;

        void init(const int thread_index, io_context *const context);

        void co_spawn(task<void> &&entrance) noexcept;

        void co_spawn(std::coroutine_handle<> entrance) noexcept;

        void worker_run_loop(const int thread_index, io_context *const context);

        void worker_run_once();
    };

    inline void worker_meta::co_spawn(std::coroutine_handle<> entrance
    ) noexcept {
        assert(entrance.address() != nullptr);
        log::v(
            "worker[%u] co_spawn coro %lx\n", this_thread.tid,
            entrance.address()
        );
        this->submit_non_sqe(reinterpret_cast<uintptr_t>(entrance.address()));
    }

    inline void worker_meta::co_spawn(task<void> &&entrance) noexcept {
        assert(entrance.get_handle().address() != nullptr);
        this->co_spawn(entrance.get_handle());
        entrance.detach();
    }

} // namespace detail

} // namespace co_context
