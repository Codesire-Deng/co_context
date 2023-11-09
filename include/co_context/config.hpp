#pragma once

#include <uring/io_uring.h>
#include <uring/uring_define.hpp>
#include <uring/utility/kernel_version.hpp>

#include <bit>
#include <cstddef>
#include <cstdint>

namespace co_context {

namespace config {

    // ================== io_uring/liburingcxx configuration ==================
    inline constexpr unsigned io_uring_setup_flags = 0;
    // inline constexpr unsigned io_uring_setup_flags = IORING_SETUP_SQPOLL;

    /**
     * @brief This tells if `IORING_SETUP_COOP_TASKRUN`
     * and `IORING_SETUP_TASKRUN_FLAG` should be enabled.
     *
     * @note Do not modify this, check `io_uring_setup_flags` instead.
     */
    inline constexpr unsigned io_uring_coop_taskrun_flag =
        bool(io_uring_setup_flags & IORING_SETUP_SQPOLL)
            ? 0
            : (IORING_SETUP_COOP_TASKRUN | IORING_SETUP_TASKRUN_FLAG);

    inline constexpr uint64_t uring_setup_flags = 0;
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
    // At most 255 threads/io_contexts.
    using ctx_id_t = uint8_t;

    inline constexpr bool is_using_hyper_threading = true;

    /**
     * @brief Use msg_ring to co_spawn betweens io_contexts, instead of
     * eventfd, std::mutex and std::queue.
     */
#define CO_CONTEXT_IS_USING_MSG_RING LIBURINGCXX_IS_KERNEL_REACH(5, 18)
#define CO_CONTEXT_IS_USING_EVENTFD  !CO_CONTEXT_IS_USING_MSG_RING
    inline constexpr bool is_using_msg_ring =
        LIBURINGCXX_IS_KERNEL_REACH(5, 18);

    /**
     * @brief This tells if a standalone thread will run in kernel space.
     *
     * @note Do not modify this, check `io_uring_setup_flags` instead.
     */
    inline constexpr bool is_using_sqpoll =
        bool(io_uring_setup_flags & IORING_SETUP_SQPOLL);

    // ========================================================================

    // ======================= io_context configuration =======================
    using cur_t = uint16_t;
    // inline constexpr cur_t swap_capacity = 4;
    // inline constexpr cur_t swap_capacity = 8;
    // inline constexpr cur_t swap_capacity = 16;
    // inline constexpr cur_t swap_capacity = 32;
    // inline constexpr cur_t swap_capacity = 64;
    // inline constexpr cur_t swap_capacity = 128;
    // inline constexpr cur_t swap_capacity = 256;
    // inline constexpr cur_t swap_capacity = 512;
    // inline constexpr cur_t swap_capacity = 1024;
    // inline constexpr cur_t swap_capacity = 2048;
    // inline constexpr cur_t swap_capacity = 4096;
    // inline constexpr cur_t swap_capacity = 8192;
    inline constexpr cur_t swap_capacity = 16384;
    // inline constexpr cur_t swap_capacity = 32768;
    static_assert(swap_capacity % 4 == 0);

    inline constexpr uint32_t default_io_uring_entries =
        std::bit_ceil<uint32_t>(swap_capacity * 2ULL);

    /**
     * @brief Maximal batch size of submissions. -1 means the batch size is
     * unlimited.
     * @note Once the threshold is reached, it is not mandatary to submit to
     * io_uring immediately. As a result, the actual batch size might be equal
     * to or slightly grater than the threshold.
     */
    inline constexpr uint32_t submission_threshold = 32;
    // inline constexpr uint32_t submission_threshold = -1U;
    // ========================================================================

    // ========================== net configuration ===========================
    inline constexpr bool is_loopback_only = false;
    // ========================================================================

    // =========================== co configuration ===========================
    using semaphore_counting_t = std::ptrdiff_t;
    using condition_variable_counting_t = std::uintptr_t;
    // ========================================================================

    // ========================= timer configuration ==========================
    /**
     * @brief Fix the timer expiring time point, to improve accuracy.
     * E.g. timeout(time) => timeout(time + bias);
     * @note The accuracy of the timer is mainly limited by the OS. The actual
     * time is usually 20~500 microseconds later than the scheduled time.
     * @warning If it is more important to ensure that no network signal is
     * missed than to ensure accuracy, it is recommended to set bias to 0.
     */
    // inline constexpr int64_t timeout_bias_nanosecond = 0;
    inline constexpr int64_t timeout_bias_nanosecond = -30'000;
    // ========================================================================

} // namespace config

// logging config
namespace config {

    enum class level : uint8_t { verbose, debug, info, warning, error, no_log };

    // inline constexpr level log_level = level::verbose;
    // inline constexpr level log_level = level::debug;
    // inline constexpr level log_level = level::info;
    inline constexpr level log_level = level::warning;
    // inline constexpr level log_level = level::error;
    // inline constexpr level log_level = level::no_log;

    inline constexpr bool is_log_v = log_level <= level::verbose;
    inline constexpr bool is_log_d = log_level <= level::debug;
    inline constexpr bool is_log_i = log_level <= level::info;
    inline constexpr bool is_log_w = log_level <= level::warning;
    inline constexpr bool is_log_e = log_level <= level::error;
} // namespace config

} // namespace co_context
