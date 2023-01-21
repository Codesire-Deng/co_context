#pragma once

#include <chrono>
#include <linux/time_types.h>

namespace co_context {

template<class Rep, class Period>
[[nodiscard]]
inline __kernel_timespec
to_kernel_timespec(std::chrono::duration<Rep, Period> duration) {
    using std::chrono::seconds;
    using std::chrono::nanoseconds;
    using std::chrono::duration_cast;
    const auto sec = duration_cast<seconds>(duration);
    const auto nano = duration_cast<nanoseconds>(duration - sec);
    return __kernel_timespec{
        .tv_sec = sec.count(),
        .tv_nsec = nano.count(),
    };
}

template<class Duration>
[[nodiscard]]
inline __kernel_timespec to_kernel_timespec(
    std::chrono::time_point<std::chrono::steady_clock, Duration> time_point
) {
    return to_kernel_timespec(time_point.time_since_epoch());
}

template<class Duration>
[[nodiscard]]
inline __kernel_timespec to_kernel_timespec(
    std::chrono::time_point<std::chrono::system_clock, Duration> time_point
) {
    return to_kernel_timespec(time_point.time_since_epoch());
}

} // namespace co_context
