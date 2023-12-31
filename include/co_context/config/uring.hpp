#pragma once

#include <uring/compat.hpp>
#include <uring/io_uring.h>
#include <uring/utility/kernel_version.hpp>

namespace co_context::config {

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

/**
 * @brief Use msg_ring to co_spawn betweens io_contexts, instead of
 * eventfd, std::mutex and std::queue.
 */
#define CO_CONTEXT_IS_USING_MSG_RING LIBURINGCXX_IS_KERNEL_REACH(5, 18)
#define CO_CONTEXT_IS_USING_EVENTFD  !CO_CONTEXT_IS_USING_MSG_RING
inline constexpr bool is_using_msg_ring = LIBURINGCXX_IS_KERNEL_REACH(5, 18);

/**
 * @brief This tells if a standalone thread will run in kernel space.
 *
 * @note Do not modify this, check `io_uring_setup_flags` instead.
 */
inline constexpr bool is_using_sqpoll =
    bool(io_uring_setup_flags & IORING_SETUP_SQPOLL);

} // namespace co_context::config
