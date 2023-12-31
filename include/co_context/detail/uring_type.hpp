#pragma once

#include <co_context/config/uring.hpp>

#include <uring/io_uring.h>
#include <uring/uring.hpp>

namespace co_context::detail {

using uring = liburingcxx::uring<
    config::io_uring_setup_flags
    | config::uring_setup_flags
#if LIBURINGCXX_IS_KERNEL_REACH(5, 19)
    /**
     * @note IORING_SETUP_COOP_TASKRUN is used because only one thread can
     * access the ring
     */
    // PERF check IORING_SETUP_TASKRUN_FLAG
    | config::io_uring_coop_taskrun_flag
#endif
#if LIBURINGCXX_IS_KERNEL_REACH(6, 0)
    | IORING_SETUP_SINGLE_ISSUER
#endif
#if LIBURINGCXX_IS_KERNEL_REACH(6, 1)
    | IORING_SETUP_DEFER_TASKRUN
#endif
    >;

} // namespace co_context::detail
