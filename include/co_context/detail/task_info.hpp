#pragma once
#include "co_context/config.hpp"
#include "co_context/detail/compat.hpp"
#include "co_context/log/log.hpp"
#include <coroutine>
#include <memory>

namespace co_context {

class counting_semaphore;
class condition_variable;

} // namespace co_context

namespace co_context::detail {

// using liburingcxx::sq_entry;
// using liburingcxx::cq_entry;

struct [[nodiscard]] task_info {
    std::coroutine_handle<> handle;

    int32_t result;

    enum class task_type : uint8_t { lazy_sqe, lazy_link_sqe, none };

    task_type type;

    explicit task_info(task_type type) noexcept : type(type) {
        log::v("task_info generated at %lx\n", this);
    }

    [[nodiscard]] uint64_t as_user_data() const noexcept {
        return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(this));
    }
};

static_assert(uint8_t(task_info::task_type::none) < alignof(task_info));

inline constexpr uintptr_t raw_task_info_mask =
    ~uintptr_t(alignof(task_info) - 1);

static_assert((~raw_task_info_mask) == 0x7);

inline task_info *raw_task_info_ptr(uintptr_t info) noexcept {
    return CO_CONTEXT_ASSUME_ALIGNED(alignof(task_info)
    )(reinterpret_cast /*NOLINT*/<task_info *>(info & raw_task_info_mask));
}

} // namespace co_context::detail
