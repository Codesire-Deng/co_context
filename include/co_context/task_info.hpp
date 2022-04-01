#pragma once
#include <coroutine>
#include <atomic>
#include "co_context/log/log.hpp"


namespace liburingcxx {
class SQEntry;
class CQEntry;
}

namespace co_context {
namespace detail {

    using liburingcxx::SQEntry;
    using liburingcxx::CQEntry;

    struct [[nodiscard]] task_info {
        union {
            SQEntry *sqe;
            CQEntry *cqe;
            int32_t result;
            std::atomic_int_fast32_t *remaining_count;
        };
        std::coroutine_handle<> handle;
        int tid_hint;
        enum class task_type { sqe, cqe, result, co_spawn, nop } type;

        constexpr task_info(task_type type) noexcept : type(type) {
            log::v("task_info generated\n");
        }

        // static task_info nop() noexcept;

        // static task_info *new_sqe();
    };

    using task_info_ptr = task_info *;

} // namespace detail

} // namespace co_context
