#pragma once

#include "../co_context.hpp"

namespace co_context {

namespace detail {

    struct lazy_awaiter {
        task_info io_info;
        liburingcxx::SQEntry sqe;

        constexpr bool await_ready() const noexcept { return false; }

        // std::coroutine_handle<>
        void await_suspend(std::coroutine_handle<> current) noexcept {
            io_info.handle = current;
            auto &worker = *detail::this_thread.worker;
            worker.submit(&io_info);
        }

        int32_t await_resume() const noexcept { return io_info.result; }

        lazy_awaiter() noexcept : io_info(task_info::task_type::sqe) {
            sqe.setData(reinterpret_cast<uint64_t>(&io_info));
            io_info.tid_hint = detail::this_thread.tid;
            io_info.sqe = &sqe;
        }
    };

    struct lazy_awaiter_yield {
        task_info io_info;

        constexpr bool await_ready() const noexcept { return false; }

        void await_suspend(std::coroutine_handle<> current) noexcept {
            io_info.handle = current;
            auto &worker = *detail::this_thread.worker;
            worker.submit(&io_info);
        }

        constexpr void await_resume() const noexcept {}

        constexpr lazy_awaiter_yield() noexcept
            : io_info(task_info::task_type::co_spawn) {
            io_info.tid_hint = detail::this_thread.tid;
            io_info.result = -1;
        }
    };

} // namespace detail

inline namespace lazy {

    using detail::lazy_awaiter;
    using detail::lazy_awaiter_yield;

    lazy_awaiter read(int fd, std::span<char> buf, uint64_t offset) noexcept {
        lazy_awaiter awaiter;
        awaiter.sqe.prepareRead(fd, buf, offset);
        return awaiter;
    }

    lazy_awaiter
    write(int fd, std::span<const char> buf, uint64_t offset) noexcept {
        lazy_awaiter awaiter;
        awaiter.sqe.prepareWrite(fd, buf, offset);
        return awaiter;
    }

    lazy_awaiter
    accept(int fd, sockaddr *addr, socklen_t *addrlen, int flags) noexcept {
        lazy_awaiter awaiter;
        awaiter.sqe.prepareAccept(fd, addr, addrlen, flags);
        return awaiter;
    }

    lazy_awaiter recv(int sockfd, std::span<char> buf, int flags) noexcept {
        lazy_awaiter awaiter;
        awaiter.sqe.prepareRecv(sockfd, buf, flags);
        return awaiter;
    }

    lazy_awaiter
    send(int sockfd, std::span<const char> buf, int flags) noexcept {
        lazy_awaiter awaiter;
        awaiter.sqe.prepareSend(sockfd, buf, flags);
        return awaiter;
    }

    lazy_awaiter
    connect(int sockfd, const sockaddr *addr, socklen_t addrlen) noexcept {
        lazy_awaiter awaiter;
        awaiter.sqe.prepareConnect(sockfd, addr, addrlen);
        return awaiter;
    }

    lazy_awaiter close(int fd) noexcept {
        lazy_awaiter awaiter;
        awaiter.sqe.prepareClose(fd);
        return awaiter;
    }

    lazy_awaiter shutdown(int fd, int how) noexcept {
        lazy_awaiter awaiter;
        awaiter.sqe.prepareShutdown(fd, how);
        return awaiter;
    }

    lazy_awaiter fsync(int fd, uint32_t fsync_flags) noexcept {
        lazy_awaiter awaiter;
        awaiter.sqe.prepareFsync(fd, fsync_flags);
        return awaiter;
    }

    lazy_awaiter
    sync_file_range(int fd, uint32_t len, uint64_t offset, int flags) noexcept {
        lazy_awaiter awaiter;
        awaiter.sqe.prepareSyncFileRange(fd, len, offset, flags);
        return awaiter;
    }

    lazy_awaiter_yield yield() noexcept { return {}; }

} // namespace lazy

} // namespace co_context
