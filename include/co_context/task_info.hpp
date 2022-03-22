#pragma once
#include "../uring.hpp"
#include <coroutine>
#include "co_context/log/log.hpp"

namespace co_context {
namespace detail {

    using liburingcxx::SQEntry;
    using liburingcxx::CQEntry;

    struct [[nodiscard]] task_info {
        union {
            SQEntry *sqe;
            CQEntry *cqe;
            int32_t result;
        };
        std::coroutine_handle<> handle;
        int tid_hint;
        enum class task_type { sqe, cqe, result, co_spawn, nop } type;

        constexpr task_info(task_type type) noexcept : type(type) {
            log::v("task_info generated\n");
        }

        static task_info nop() noexcept {
            task_info ret{task_type::nop};
            ret.sqe = nullptr;
            ret.handle = nullptr;
            ret.tid_hint = -1;
            return ret;
        }

        static task_info *new_sqe() {
            task_info *addr = reinterpret_cast<task_info *>(
                mi_malloc(sizeof(task_info) + sizeof(SQEntry)));
            addr->sqe =
                reinterpret_cast<SQEntry *>((char *)addr + sizeof(task_info));
            addr->type = task_type::sqe;
            return addr;
        }

        /* static task_info from_worker(std::coroutine_handle<> handle)
           noexcept { return task_info{ .sqe = nullptr, .handle{handle},
                        .tid_hint = this_thread.tid,
                    };
                } */
    };

    using task_info_ptr = task_info *;

} // namespace detail

} // namespace co_context
