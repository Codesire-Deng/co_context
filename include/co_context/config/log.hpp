#pragma once

#include <cstdint>

namespace co_context::config {

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

} // namespace co_context::config
