#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>

namespace co_context::config {

// ========================== CPU configuration ===========================
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
// ========================================================================

// ======================= io_context configuration =======================
using cur_t = uint32_t;
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
 * to or slightly greater than the threshold.
 */
// inline constexpr uint32_t submission_threshold = 32;
inline constexpr uint32_t submission_threshold = -1U;
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

} // namespace co_context::config
