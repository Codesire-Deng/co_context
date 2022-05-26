#pragma once

// #include <new>
#include <cstddef>
#include <cstdint>
#include "uring/io_uring.h"

namespace co_context {

namespace config {

    // ======================== io_uring configuration ========================
    inline constexpr unsigned io_uring_flags = 0;
    // inline constexpr unsigned io_uring_flags = IORING_SETUP_SQPOLL;
    // ========================================================================

    // ========================== CPU configuration ===========================
    /**
     * @brief This tells if each thread (io_contexts and workers) should stay
     * on the corresponding CPU. Doing so would dismiss some cache miss.
     */
    // #define CO_CONTEXT_USE_CPU_AFFINITY

    /**
     * @brief Size of cache line on the CPU L1 cache. Usually this is 64 byte.
     * @note Now `std::hardware_destructive_interference_size` is not used
     * because of warning from gcc 12
     */
#if 0 && __cpp_lib_hardware_interference_size >= 201603
    inline constexpr size_t cache_line_size =
        std::hardware_destructive_interference_size;
#else
    inline constexpr size_t cache_line_size = 64;
#endif
    // ========================================================================

    // ====================== Thread model configuration ======================
    using threads_number_size_t = uint16_t;

    using tid_size_t = threads_number_size_t;

    using tid_t = tid_size_t;

    inline constexpr bool using_hyper_threading = true;

    /**
     * @brief This tells if a standalone thread will run in kernel space.
     *
     * @note Do not modify this, check `io_uring_flags` instead.
     */
    inline constexpr bool using_SQPOLL = io_uring_flags & IORING_SETUP_SQPOLL;

    /**
     * @brief This number tells how many standalone worker-threads are running
     * with one io_context. This can be set to zero, meaning the io_context
     * itself is a worker.
     *
     * @note If your program is CPU-bound, try to increase this. Otherwise,
     * decreasing this and use more io_contexts will benefit IO-bound program.
     */
    inline constexpr tid_size_t worker_threads_number = 0;
    // inline constexpr tid_size_t worker_threads_number = 1;
    // inline constexpr tid_size_t worker_threads_number = 2;
    // inline constexpr tid_size_t worker_threads_number = 3;
    // inline constexpr tid_size_t worker_threads_number = 4;

    /**
     * @note Do not modify this, check `worker_threads_number` instead.
     */
    inline constexpr tid_size_t workers_number =
        worker_threads_number > 1 ? worker_threads_number : 1;

    inline constexpr bool use_standalone_completion_poller = false;
    static_assert(
        !use_standalone_completion_poller || worker_threads_number > 0
    );
    // ========================================================================

    // ======================= io_context configuration =======================
    // Enabling this would significantly increase latency in multi-thread model.
    inline constexpr bool use_wait_and_notify = false;
    static_assert(!use_wait_and_notify || worker_threads_number != 0);

    using swap_capacity_size_t = uint16_t;
    using cur_t = swap_capacity_size_t;

    // inline constexpr swap_capacity_size_t swap_capacity = 1;
    inline constexpr swap_capacity_size_t swap_capacity = 4;
    // inline constexpr swap_capacity_size_t swap_capacity = 8;
    // inline constexpr swap_capacity_size_t swap_capacity = 16;
    // inline constexpr swap_capacity_size_t swap_capacity = 32;
    // inline constexpr swap_capacity_size_t swap_capacity = 64;
    // inline constexpr swap_capacity_size_t swap_capacity = 128;
    // inline constexpr swap_capacity_size_t swap_capacity = 256;
    // inline constexpr swap_capacity_size_t swap_capacity = 512;
    // inline constexpr swap_capacity_size_t swap_capacity = 1024;
    // inline constexpr swap_capacity_size_t swap_capacity = 2048;
    // inline constexpr swap_capacity_size_t swap_capacity = 4096;
    // inline constexpr swap_capacity_size_t swap_capacity = 8192;
    // inline constexpr swap_capacity_size_t swap_capacity = 16384;
    // inline constexpr swap_capacity_size_t swap_capacity = 32768;
    static_assert(swap_capacity % 4 == 0);

    inline constexpr uint8_t submit_poll_rounds = 1;

    inline constexpr uint8_t reap_poll_rounds = 1;
    // ========================================================================

    // ========================== net configuration ===========================
    inline constexpr bool loopback_only = true;
    // inline constexpr bool loopback_only = false;
    // ========================================================================

    // =========================== co configuration ===========================
    using semaphore_counting_t = std::ptrdiff_t;
    using condition_variable_counting_t = std::uintptr_t;
    // ========================================================================

    // ======================== lazy_io configuration =========================
    inline constexpr bool enable_link_io_result = false;
    // ========================================================================

    // ======================== eager_io configuration ========================
    using eager_io_state_t = uint8_t;
    inline constexpr bool enable_eager_io = false;
    // ========================================================================

} // namespace config

// logging config
namespace config {

    enum class level : uint8_t { verbose, debug, info, warning, error, no_log };

    // inline constexpr level log_level = level::verbose;
    // inline constexpr level log_level = level::debug;
    inline constexpr level log_level = level::info;
    // inline constexpr level log_level = level::warning;
    // inline constexpr level log_level = level::error;
    // inline constexpr level log_level = level::no_log;

} // namespace config

} // namespace co_context