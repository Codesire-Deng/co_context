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
            std::atomic_int_fast32_t *remaining_count;
            semaphore *sem;
        };
        union {
            std::coroutine_handle<> handle;
            config::semaphore_underlying_type update;
        };
        int tid_hint;
        enum class task_type {
            sqe,
            cqe,
            result,
            co_spawn,
            semaphore_release,
            nop
        } type;

        constexpr task_info(task_type type) noexcept : type(type) {
            log::v("task_info generated\n");
        }

        // static task_info nop() noexcept;

        static task_info *new_semaphore_release(
            semaphore *sem, config::semaphore_underlying_type update);
    };

    using task_info_ptr = task_info *;

    inline task_info *task_info::new_semaphore_release(
        semaphore *sem, config::semaphore_underlying_type update) {
        // ret will be deleted after worker reap this.
        task_info *ret = new task_info{task_type::semaphore_release};
        ret->sem = sem;
        ret->update = update;
        ret->tid_hint = this_thread.tid;
        return ret;
    }

} // namespace detail

} // namespace co_context
