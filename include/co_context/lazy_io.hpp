#pragma once

#include "co_context/io_context.hpp"
#include <cassert>
#include <span>
#include <chrono>

namespace co_context {

namespace detail {

    struct [[nodiscard("Did you forget to co_await?")]] lazy_awaiter {
        liburingcxx::SQEntry sqe;
        task_info io_info;

        constexpr bool await_ready() const noexcept {
            return false;
        }

        // std::coroutine_handle<>
        void await_suspend(std::coroutine_handle<> current) noexcept {
            io_info.handle = current;
            worker_meta *worker = detail::this_thread.worker;
            worker->submit(&io_info);
        }

        int32_t await_resume() const noexcept {
            return io_info.result;
        }

        lazy_awaiter() noexcept : io_info(task_info::task_type::sqe) {
            sqe.setData(reinterpret_cast<uint64_t>(&io_info));
            io_info.tid_hint = detail::this_thread.tid;
            io_info.sqe = &sqe;
        }

#ifndef __INTELLISENSE__
        lazy_awaiter(const lazy_awaiter &) = delete;
        lazy_awaiter(lazy_awaiter &&) = delete;
        lazy_awaiter &operator=(const lazy_awaiter &) = delete;
        lazy_awaiter &operator=(lazy_awaiter &&) = delete;
#endif
    };

    struct lazy_awaiter_read : lazy_awaiter {
        [[nodiscard("Did you forget to co_await?")]] inline lazy_awaiter_read(
            int fd, std::span<char> buf, uint64_t offset
        ) noexcept {
            sqe.prepareRead(fd, buf, offset);
        }
    };

    struct lazy_awaiter_write : lazy_awaiter {
        [[nodiscard("Did you forget to co_await?")]] inline lazy_awaiter_write(
            int fd, std::span<const char> buf, uint64_t offset
        ) noexcept {
            sqe.prepareWrite(fd, buf, offset);
        }
    };

    struct lazy_awaiter_accept : lazy_awaiter {
        [[nodiscard("Did you forget to co_await?")]] inline lazy_awaiter_accept(
            int fd, sockaddr *addr, socklen_t *addrlen, int flags
        ) noexcept {
            sqe.prepareAccept(fd, addr, addrlen, flags);
        }
    };

    struct lazy_awaiter_recv : lazy_awaiter {
        [[nodiscard("Did you forget to co_await?")]] inline lazy_awaiter_recv(
            int sockfd, std::span<char> buf, int flags
        ) noexcept {
            sqe.prepareRecv(sockfd, buf, flags);
        }
    };

    struct lazy_awaiter_send : lazy_awaiter {
        [[nodiscard("Did you forget to co_await?")]] inline lazy_awaiter_send(
            int sockfd, std::span<const char> buf, int flags
        ) noexcept {
            sqe.prepareSend(sockfd, buf, flags);
        }
    };

    struct lazy_awaiter_connect : lazy_awaiter {
        [[nodiscard("Did you forget to co_await?")]] inline lazy_awaiter_connect(
            int sockfd, const sockaddr *addr, socklen_t addrlen
        ) noexcept {
            sqe.prepareConnect(sockfd, addr, addrlen);
        }
    };

    struct lazy_awaiter_close : lazy_awaiter {
        [[nodiscard("Did you forget to co_await?"
        )]] inline lazy_awaiter_close(int fd) noexcept {
            sqe.prepareClose(fd);
        }
    };

    struct lazy_awaiter_shutdown : lazy_awaiter {
        [[nodiscard("Did you forget to co_await?"
        )]] inline lazy_awaiter_shutdown(int fd, int how) noexcept {
            sqe.prepareShutdown(fd, how);
        }
    };

    struct lazy_awaiter_fsync : lazy_awaiter {
        [[nodiscard("Did you forget to co_await?"
        )]] inline lazy_awaiter_fsync(int fd, uint32_t fsync_flags) noexcept {
            sqe.prepareFsync(fd, fsync_flags);
        }
    };

    struct lazy_awaiter_sync_file_range : lazy_awaiter {
        [[nodiscard("Did you forget to co_await?")]] inline lazy_awaiter_sync_file_range(
            int fd, uint32_t len, uint64_t offset, int flags
        ) noexcept {
            sqe.prepareSyncFileRange(fd, len, offset, flags);
        }
    };

    struct lazy_awaiter_timeout_timespec : lazy_awaiter {
        [[nodiscard("Did you forget to co_await?")]] inline lazy_awaiter_timeout_timespec(
            __kernel_timespec *ts, unsigned int count, unsigned int flags
        ) noexcept {
            sqe.prepareTimeout(ts, count, flags);
        }
    };

    struct lazy_awaiter_timeout : lazy_awaiter {
        __kernel_timespec ts;

        template<class Rep, class Period = std::ratio<1>>
        [[nodiscard("Did you forget to co_await?")]] inline lazy_awaiter_timeout(
            std::chrono::duration<Rep, Period> duration, unsigned int flags
        ) noexcept {
            using namespace std;
            using namespace std::literals;
            ts.tv_sec = duration / 1s;
            duration -= chrono::seconds(ts.tv_sec);
            ts.tv_nsec =
                duration_cast<chrono::duration<long long, std::nano>>(duration)
                    .count();
            sqe.prepareTimeout(&ts, 0, flags);
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
            io_info.result = 0;
        }
    };

} // namespace detail

inline namespace lazy {

    using detail::lazy_awaiter;
    using detail::lazy_awaiter_yield;

    inline detail::lazy_awaiter_read
    read(int fd, std::span<char> buf, uint64_t offset) noexcept {
        return detail::lazy_awaiter_read{fd, buf, offset};
    }

    inline detail::lazy_awaiter_write
    write(int fd, std::span<const char> buf, uint64_t offset) noexcept {
        return detail::lazy_awaiter_write{fd, buf, offset};
    }

    inline detail::lazy_awaiter_accept
    accept(int fd, sockaddr *addr, socklen_t *addrlen, int flags) noexcept {
        return detail::lazy_awaiter_accept{fd, addr, addrlen, flags};
    }

    inline detail::lazy_awaiter_recv
    recv(int sockfd, std::span<char> buf, int flags) noexcept {
        return detail::lazy_awaiter_recv{sockfd, buf, flags};
    }

    inline detail::lazy_awaiter_send
    send(int sockfd, std::span<const char> buf, int flags) noexcept {
        return detail::lazy_awaiter_send{sockfd, buf, flags};
    }

    inline detail::lazy_awaiter_connect
    connect(int sockfd, const sockaddr *addr, socklen_t addrlen) noexcept {
        return detail::lazy_awaiter_connect{sockfd, addr, addrlen};
    }

    inline detail::lazy_awaiter_close close(int fd) noexcept {
        return detail::lazy_awaiter_close{fd};
    }

    inline detail::lazy_awaiter_shutdown shutdown(int fd, int how) noexcept {
        return detail::lazy_awaiter_shutdown{fd, how};
    }

    inline detail::lazy_awaiter_fsync
    fsync(int fd, uint32_t fsync_flags) noexcept {
        return detail::lazy_awaiter_fsync{fd, fsync_flags};
    }

    inline detail::lazy_awaiter_sync_file_range
    sync_file_range(int fd, uint32_t len, uint64_t offset, int flags) noexcept {
        return detail::lazy_awaiter_sync_file_range{fd, len, offset, flags};
    }

    /**
     * @brief Set timeout. When it expires, the coroutine will wake up
     *
     * @param ts The relative time duration, or the absolute time stamp.
     * @param count The completion event count.
     * @param flags If it contains IORING_TIMEOUT_ABS, uses absolute time
     * stamp. See man io_uring_enter(2).
     * @return lazy_awaiter
     */
    inline detail::lazy_awaiter_timeout_timespec timeout(
        __kernel_timespec *ts, unsigned int count, unsigned int flags
    ) noexcept {
        return detail::lazy_awaiter_timeout_timespec{ts, count, flags};
    }

    /**
     * @brief Set duration timeout.
     *
     * @param flags See man io_uring_enter(2).
     * @return lazy_awaiter
     */
    template<class Rep, class Period = std::ratio<1>>
    inline detail::lazy_awaiter_timeout timeout(
        std::chrono::duration<Rep, Period> duration, unsigned int flags = 0
    ) noexcept {
        return detail::lazy_awaiter_timeout{duration, flags};
    }

    inline lazy_awaiter_yield yield() noexcept {
        return {};
    }

} // namespace lazy

} // namespace co_context
