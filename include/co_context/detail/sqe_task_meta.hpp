#pragma once

// #include "co_context/config.hpp"
#include "co_context/detail/task_info.hpp"
#include "uring/uring.hpp"

namespace co_context {

namespace detail {

    using task_info = ::co_context::detail::task_info;

    struct sqe_task_meta {
        liburingcxx::SQEntry sqe;
        task_info io_info;

        constexpr sqe_task_meta(task_info::task_type type) noexcept
            : io_info(type) {
            sqe.setData(io_info.as_user_data());
        }

        inline uint64_t get_user_data() const noexcept {
            return io_info.as_user_data();
        }
    };

    static_assert(std::is_standard_layout_v<sqe_task_meta>);
    static_assert(offsetof(sqe_task_meta, sqe) == 0);

    inline constexpr sqe_task_meta *as_sqe_task_meta(task_info *task_ptr) {
        using meta = sqe_task_meta;
        return reinterpret_cast<meta *>(
            reinterpret_cast<uintptr_t>(task_ptr) - offsetof(meta, io_info)
        );
    }

} // namespace detail

} // namespace co_context