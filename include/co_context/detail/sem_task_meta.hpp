#pragma once

#include "co_context/co/semaphore.hpp"

namespace co_context {

namespace detail {

    inline constexpr counting_semaphore *
    as_counting_semaphore(task_info *task_ptr) noexcept {
        using sem = counting_semaphore;
        return reinterpret_cast<sem *>(
            reinterpret_cast<uintptr_t>(task_ptr) - sem::__task_offset()
        );
    }

} // namespace detail

} // namespace co_context
