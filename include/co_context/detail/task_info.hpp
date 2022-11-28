#pragma once
#include "co_context/config.hpp"
#include "co_context/compat.hpp"
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
    union {
        std::coroutine_handle<> handle;
        config::semaphore_counting_t update;
        config::condition_variable_counting_t notify_counter;
    };

    union {
        // const cq_entry *cqe;
        int32_t result;
        // counting_semaphore *sem;
        // condition_variable *cv;
    };

    enum class task_type : uint8_t {
        co_spawn,
        lazy_sqe,
        lazy_link_sqe,
        // lazy_detached_sqe,
        // result,
        semaphore_release,
        condition_variable_notify,
        none
    };

    task_type type;

    explicit task_info(task_type type) noexcept : type(type) {
        log::v("task_info generated at %lx\n", this);
    }

    [[nodiscard]] uint64_t as_user_data() const noexcept {
        return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(this));
    }

    [[deprecated, nodiscard]] uint64_t as_linked_user_data() const noexcept {
        return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(this))
               | uint64_t(task_type::lazy_link_sqe);
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
