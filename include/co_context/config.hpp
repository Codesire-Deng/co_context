#pragma once

#include <new>
#include <cstddef>
#include <cstdint>

namespace co_context {

namespace config {

    inline constexpr bool use_hyper_threading = true;
#define USE_CPU_AFFINITY
#ifdef USE_CPU_AFFINITY
    inline constexpr bool use_CPU_affinity = true;
#else
    inline constexpr bool use_CPU_affinity = false;
#endif

#if __cpp_lib_hardware_interference_size >= 201603
    inline constexpr size_t cache_line_size =
        std::hardware_destructive_interference_size;
#else
    inline constexpr size_t cache_line_size = 64;
#endif

    // these configurations below can be overridden as template parameters
    inline constexpr unsigned total_threads_number = 8;
    inline constexpr bool low_latency_mode = true;
    inline constexpr uint16_t swap_capacity = 1024;

} // namespace config

namespace test {
    inline constexpr int64_t swap_tot = 1'250'000'000;
} // namespace test

} // namespace co_context