#pragma once

#include "co_context/co/condition_variable.hpp"
#include "co_context/detail/task_info.hpp"

namespace co_context {

namespace detail {

    static_assert(std::is_standard_layout_v<condition_variable>);

    inline condition_variable *as_condition_variable(task_info *task_ptr
    ) noexcept {
        using cv = condition_variable;
        return reinterpret_cast<cv *>(
            reinterpret_cast<uintptr_t>(task_ptr) - cv::__task_offset()
        );
    }

} // namespace detail

} // namespace co_context
