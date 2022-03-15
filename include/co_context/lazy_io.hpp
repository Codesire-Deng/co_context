#pragma once

#include "../co_context.hpp"

namespace co_context {

namespace detail {

    struct lazy_awaiter {
        task_info io_info;
        liburingcxx::SQEntry sqe;

        constexpr bool await_ready() const noexcept { return false; }

        std::coroutine_handle<>
        await_suspend(std::coroutine_handle<> current) noexcept {
            io_info.handle = current;
            auto &worker = *detail::this_thread.worker;
            worker.submit(&io_info);
            return worker.schedule();
        }

        int32_t await_resume() const noexcept { return io_info.result; }

        lazy_awaiter() noexcept : io_info(task_info::task_type::sqe) {
            sqe.setData(reinterpret_cast<uint64_t>(&io_info));
            io_info.tid_hint = detail::this_thread.tid;
            io_info.sqe = &sqe;
        }
    };

} // namespace detail

inline namespace lazy {

    detail::lazy_awaiter
    read(int fd, std::span<char> buf, uint64_t offset) noexcept {
        detail::lazy_awaiter awaiter;
        awaiter.io_info.sqe->prepareRead(fd, buf, offset);
        return awaiter;
    }

    detail::lazy_awaiter
    write(int fd, std::span<const char> buf, uint64_t offset) noexcept {
        detail::lazy_awaiter awaiter;
        awaiter.io_info.sqe->prepareWrite(fd, buf, offset);
        return awaiter;
    }

} // namespace lazy

} // namespace co_context
