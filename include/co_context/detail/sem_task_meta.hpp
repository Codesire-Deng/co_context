#pragma once

#include "co_context/co/semaphore.hpp"

namespace co_context::detail {

inline counting_semaphore *as_counting_semaphore(task_info *task_ptr) noexcept {
    using sem = counting_semaphore;
    return reinterpret_cast<sem *> /*NOLINT*/ (
        reinterpret_cast<uintptr_t>(task_ptr) - sem::__task_offset()
    );
}

} // namespace co_context::detail
