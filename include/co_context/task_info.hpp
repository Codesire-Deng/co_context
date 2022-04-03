#pragma once
#include <coroutine>
#include <atomic>
#include "co_context/log/log.hpp"
#include "co_context/detail/thread_meta.hpp"

namespace liburingcxx {
class SQEntry;
class CQEntry;
}

namespace co_context {

class semaphore;

namespace detail {

    using liburingcxx::SQEntry;
    using liburingcxx::CQEntry;

    struct [[nodiscard]] task_info {
        union {
            SQEntry *sqe;
            CQEntry *cqe;
            int32_t result;
            semaphore *sem;
        };
        union {
            std::coroutine_handle<> handle;
            config::semaphore_underlying_type update;
        };

        config::tid_t tid_hint;

        enum class task_type {
            sqe,
            cqe,
            result,
            co_spawn,
            semaphore_release,
            nop
        };

        const task_type type;

        constexpr task_info(task_type type) noexcept : type(type) {
            log::v("task_info generated\n");
        }
    };

    using task_info_ptr = task_info *;

} // namespace detail

} // namespace co_context
