#pragma once
#include <co_context/detail/compat.hpp>
#include <co_context/log/log.hpp>

#include <coroutine>
#include <cstdint>
#include <memory>

namespace co_context::detail {

struct [[nodiscard]] task_info {
    std::coroutine_handle<> handle;

    int32_t result;

    [[nodiscard]]
    uint64_t as_user_data() const noexcept {
        return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(this));
    }
};

inline constexpr uintptr_t raw_task_info_mask =
    ~uintptr_t(alignof(task_info) - 1);

static_assert((~raw_task_info_mask) == 0x7);

inline task_info *raw_task_info_ptr(uintptr_t info) noexcept {
    return CO_CONTEXT_ASSUME_ALIGNED(alignof(task_info))(
        reinterpret_cast /*NOLINT*/<task_info *>(info & raw_task_info_mask)
    );
}

} // namespace co_context::detail
