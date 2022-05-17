#pragma once

#include "uring/uring.hpp"
#include "co_context/detail/thread_meta.hpp"
#include "co_context/detail/worker_meta.hpp"
#include "co_context/detail/task_info.hpp"
#include <cassert>
#include <span>
#include <chrono>

namespace co_context {

namespace detail {

    struct [[nodiscard("Did you forget to co_await?")]] lazy_link_io {
        class lazy_awaiter *last_io;

        constexpr bool await_ready() const noexcept {
            return false;
        }

        void await_suspend(std::coroutine_handle<> current) noexcept;

        int32_t await_resume() const noexcept;
    };

    class [[nodiscard("Did you forget to co_await?")]] lazy_awaiter {
      public:
        constexpr bool await_ready() const noexcept {
            return false;
        }

        void await_suspend(std::coroutine_handle<> current) noexcept {
            io_info.handle = current;
            submit();
        }

        int32_t await_resume() const noexcept {
            return io_info.result;
        }

      protected:
        friend class lazy_link_io;
        friend struct lazy_link_timeout;
        liburingcxx::SQEntry *sqe;
        task_info io_info;

        inline void submit() noexcept {
            worker_meta *const worker = detail::this_thread.worker;
            worker->submit_sqe();
        }

        friend lazy_link_io operator&&(
            lazy_awaiter &&lhs, lazy_awaiter &&rhs
        ) noexcept;

        friend lazy_link_io &&operator&&(
            lazy_link_io &&lhs, lazy_awaiter &&rhs
        ) noexcept;

        friend lazy_link_io &&operator&&(
            lazy_link_io &&lhs, lazy_link_io &&rhs
        ) noexcept;

        friend void set_link_awaiter(lazy_awaiter & awaiter) noexcept;

        lazy_awaiter() noexcept : io_info(task_info::task_type::lazy_sqe) {
            io_info.tid_hint = detail::this_thread.tid;
            sqe = this_thread.worker->get_free_sqe();
            assert(sqe != nullptr);
            sqe->setData(io_info.as_user_data());
        }

#ifndef __INTELLISENSE__
        lazy_awaiter(const lazy_awaiter &) = delete;
        lazy_awaiter(lazy_awaiter &&) = delete;
        lazy_awaiter &operator=(const lazy_awaiter &) = delete;
        lazy_awaiter &operator=(lazy_awaiter &&) = delete;
#endif
    };

    inline void set_link_sqe(liburingcxx::SQEntry *sqe) noexcept {
        sqe->setLink();
        sqe->fetchData() |= __u64(task_info::task_type::lazy_link_sqe);
    }

    inline void set_link_awaiter(lazy_awaiter &awaiter) noexcept {
        set_link_sqe(awaiter.sqe);
        awaiter.io_info.type = task_info::task_type::lazy_link_sqe;
    }

    inline void set_link_link_io(lazy_link_io &link_io) noexcept {
        set_link_awaiter(*link_io.last_io);
    }

    inline lazy_link_io
    operator&&(lazy_awaiter &&lhs, lazy_awaiter &&rhs) noexcept {
        set_link_awaiter(lhs);
        return lazy_link_io{.last_io = &rhs};
    }

    inline lazy_link_io &&
    operator&&(lazy_link_io &&lhs, lazy_awaiter &&rhs) noexcept {
        set_link_link_io(lhs);
        lhs.last_io = &rhs;
        return std::move(lhs);
    }

    inline lazy_link_io &&
    operator&&(lazy_link_io &&lhs, lazy_link_io &&rhs) noexcept {
        set_link_link_io(lhs);
        return std::move(rhs);
    }

    inline void lazy_link_io::await_suspend(std::coroutine_handle<> current
    ) noexcept {
        this->last_io->io_info.handle = current;
        worker_meta *const worker = detail::this_thread.worker;
        worker->submit_sqe();
    }

    inline int32_t lazy_link_io::await_resume() const noexcept {
        return this->last_io->io_info.result;
    }

    struct lazy_read : lazy_awaiter {
        [[nodiscard("Did you forget to co_await?")]] inline lazy_read(
            int fd, std::span<char> buf, uint64_t offset
        ) noexcept {
            sqe->prepareRead(fd, buf, offset);
        }
    };

    struct lazy_readv : lazy_awaiter {
        [[nodiscard("Did you forget to co_await?")]] inline lazy_readv(
            int fd, std::span<const iovec> iovecs, uint64_t offset
        ) noexcept {
            sqe->prepareReadv(fd, iovecs, offset);
        }
    };

    struct lazy_read_fixed : lazy_awaiter {
        [[nodiscard("Did you forget to co_await?")]] inline lazy_read_fixed(
            int fd, std::span<char> buf, uint64_t offset, uint16_t bufIndex
        ) noexcept {
            sqe->prepareReadFixed(fd, buf, offset, bufIndex);
        }
    };

    struct lazy_write : lazy_awaiter {
        [[nodiscard("Did you forget to co_await?")]] inline lazy_write(
            int fd, std::span<const char> buf, uint64_t offset
        ) noexcept {
            sqe->prepareWrite(fd, buf, offset);
        }
    };

    struct lazy_writev : lazy_awaiter {
        [[nodiscard("Did you forget to co_await?")]] inline lazy_writev(
            int fd, std::span<const iovec> iovecs, uint64_t offset
        ) noexcept {
            sqe->prepareWritev(fd, iovecs, offset);
        }
    };

    struct lazy_write_fixed : lazy_awaiter {
        [[nodiscard("Did you forget to co_await?")]] inline lazy_write_fixed(
            int fd,
            std::span<const char> buf,
            uint64_t offset,
            uint16_t bufIndex
        ) noexcept {
            sqe->prepareWriteFixed(fd, buf, offset, bufIndex);
        }
    };

    struct lazy_accept : lazy_awaiter {
        [[nodiscard("Did you forget to co_await?")]] inline lazy_accept(
            int fd, sockaddr *addr, socklen_t *addrlen, int flags
        ) noexcept {
            sqe->prepareAccept(fd, addr, addrlen, flags);
        }
    };

    struct lazy_accept_direct : lazy_awaiter {
        [[nodiscard("Did you forget to co_await?")]] inline lazy_accept_direct(
            int fd,
            sockaddr *addr,
            socklen_t *addrlen,
            int flags,
            uint32_t fileIndex
        ) noexcept {
            sqe->prepareAcceptDirect(fd, addr, addrlen, flags, fileIndex);
        }
    };

    struct lazy_cancel : lazy_awaiter {
        [[nodiscard("Did you forget to co_await?"
        )]] inline lazy_cancel(uint64_t user_data, int flags) noexcept {
            sqe->prepareCancle(user_data, flags);
        }
    };

    struct lazy_cancel_fd : lazy_awaiter {
        [[nodiscard("Did you forget to co_await?"
        )]] inline lazy_cancel_fd(int fd, unsigned int flags) noexcept {
            sqe->prepareCancleFd(fd, flags);
        }
    };

    struct lazy_recv : lazy_awaiter {
        [[nodiscard("Did you forget to co_await?")]] inline lazy_recv(
            int sockfd, std::span<char> buf, int flags
        ) noexcept {
            sqe->prepareRecv(sockfd, buf, flags);
        }
    };

    struct lazy_recvmsg : lazy_awaiter {
        [[nodiscard("Did you forget to co_await?")]] inline lazy_recvmsg(
            int fd, msghdr *msg, unsigned int flags
        ) noexcept {
            sqe->prepareRecvmsg(fd, msg, flags);
        }
    };

    struct lazy_send : lazy_awaiter {
        [[nodiscard("Did you forget to co_await?")]] inline lazy_send(
            int sockfd, std::span<const char> buf, int flags
        ) noexcept {
            sqe->prepareSend(sockfd, buf, flags);
        }
    };

    struct lazy_sendmsg : lazy_awaiter {
        [[nodiscard("Did you forget to co_await?")]] inline lazy_sendmsg(
            int fd, const msghdr *msg, unsigned int flags
        ) noexcept {
            sqe->prepareSendmsg(fd, msg, flags);
        }
    };

    struct lazy_connect : lazy_awaiter {
        [[nodiscard("Did you forget to co_await?")]] inline lazy_connect(
            int sockfd, const sockaddr *addr, socklen_t addrlen
        ) noexcept {
            sqe->prepareConnect(sockfd, addr, addrlen);
        }
    };

    struct lazy_close : lazy_awaiter {
        [[nodiscard("Did you forget to co_await?")]] inline lazy_close(int fd
        ) noexcept {
            sqe->prepareClose(fd);
        }
    };

    struct lazy_shutdown : lazy_awaiter {
        [[nodiscard("Did you forget to co_await?"
        )]] inline lazy_shutdown(int fd, int how) noexcept {
            sqe->prepareShutdown(fd, how);
        }
    };

    struct lazy_fsync : lazy_awaiter {
        [[nodiscard("Did you forget to co_await?"
        )]] inline lazy_fsync(int fd, uint32_t fsync_flags) noexcept {
            sqe->prepareFsync(fd, fsync_flags);
        }
    };

    struct lazy_sync_file_range : lazy_awaiter {
        [[nodiscard("Did you forget to co_await?")]] inline lazy_sync_file_range(
            int fd, uint32_t len, uint64_t offset, int flags
        ) noexcept {
            sqe->prepareSyncFileRange(fd, len, offset, flags);
        }
    };

    struct lazy_uring_nop : lazy_awaiter {
        [[nodiscard("Did you forget to co_await?"
        )]] inline lazy_uring_nop() noexcept {
            sqe->prepareNop();
        }
    };

    struct lazy_files_update : lazy_awaiter {
        [[nodiscard("Did you forget to co_await?"
        )]] inline lazy_files_update(std::span<int> fds, int offset) noexcept {
            sqe->prepareFilesUpdate(fds, offset);
        }
    };

    struct lazy_fallocate : lazy_awaiter {
        [[nodiscard("Did you forget to co_await?")]] inline lazy_fallocate(
            int fd, int mode, off_t offset, off_t len
        ) noexcept {
            sqe->prepareFallocate(fd, mode, offset, len);
        }
    };

    struct lazy_openat : lazy_awaiter {
        [[nodiscard("Did you forget to co_await?")]] inline lazy_openat(
            int dfd, const char *path, int flags, mode_t mode
        ) noexcept {
            sqe->prepareOpenat(dfd, path, flags, mode);
        }
    };

    /* open directly into the fixed file table */
    struct lazy_openat_direct : lazy_awaiter {
        [[nodiscard("Did you forget to co_await?")]] inline lazy_openat_direct(
            int dfd,
            const char *path,
            int flags,
            mode_t mode,
            unsigned file_index
        ) noexcept {
            sqe->prepareOpenatDirect(dfd, path, flags, mode, file_index);
        }
    };

    struct lazy_openat2 : lazy_awaiter {
        [[nodiscard("Did you forget to co_await?")]] inline lazy_openat2(
            int dfd, const char *path, open_how *how
        ) noexcept {
            sqe->prepareOpenat2(dfd, path, how);
        }
    };

    /* open directly into the fixed file table */
    struct lazy_openat2_direct : lazy_awaiter {
        [[nodiscard("Did you forget to co_await?")]] inline lazy_openat2_direct(
            int dfd, const char *path, open_how *how, unsigned int file_index
        ) noexcept {
            sqe->prepareOpenat2Direct(dfd, path, how, file_index);
        }
    };

    struct lazy_statx : lazy_awaiter {
        [[nodiscard("Did you forget to co_await?")]] inline lazy_statx(
            int dfd,
            const char *path,
            int flags,
            unsigned int mask,
            struct statx *statxbuf
        ) noexcept {
            sqe->prepareStatx(dfd, path, flags, mask, statxbuf);
        }
    };

    struct lazy_unlinkat : lazy_awaiter {
        [[nodiscard("Did you forget to co_await?")]] inline lazy_unlinkat(
            int dfd, const char *path, int flags
        ) noexcept {
            sqe->prepareUnlinkat(dfd, path, flags);
        }
    };

    struct lazy_renameat : lazy_awaiter {
        [[nodiscard("Did you forget to co_await?")]] inline lazy_renameat(
            int olddfd,
            const char *oldpath,
            int newdfd,
            const char *newpath,
            int flags
        ) noexcept {
            sqe->prepareRenameat(olddfd, oldpath, newdfd, newpath, flags);
        }
    };

    struct lazy_mkdirat : lazy_awaiter {
        [[nodiscard("Did you forget to co_await?")]] inline lazy_mkdirat(
            int dfd, const char *path, mode_t mode
        ) noexcept {
            sqe->prepareMkdirat(dfd, path, mode);
        }
    };

    struct lazy_symlinkat : lazy_awaiter {
        [[nodiscard("Did you forget to co_await?")]] inline lazy_symlinkat(
            const char *target, int newdirfd, const char *linkpath
        ) noexcept {
            sqe->prepareSymlinkat(target, newdirfd, linkpath);
        }
    };

    struct lazy_linkat : lazy_awaiter {
        [[nodiscard("Did you forget to co_await?")]] inline lazy_linkat(
            int olddfd,
            const char *oldpath,
            int newdfd,
            const char *newpath,
            int flags
        ) noexcept {
            sqe->prepareLinkat(olddfd, oldpath, newdfd, newpath, flags);
        }
    };

    struct lazy_timeout_timespec : lazy_awaiter {
      public:
        [[nodiscard("Did you forget to co_await?")]] inline lazy_timeout_timespec(
            __kernel_timespec *ts, unsigned int count, unsigned int flags
        ) noexcept {
            sqe->prepareTimeout(ts, count, flags);
        }

      protected:
        inline lazy_timeout_timespec() noexcept {}
    };

    struct lazy_timeout_base : lazy_awaiter {
      protected:
        __kernel_timespec ts;

      public:
        template<class Rep, class Period = std::ratio<1>>
        void set_ts(std::chrono::duration<Rep, Period> duration) noexcept {
            using namespace std;
            using namespace std::literals;
            ts.tv_sec = duration / 1s;
            duration -= chrono::seconds(ts.tv_sec);
            ts.tv_nsec =
                duration_cast<chrono::duration<long long, std::nano>>(duration)
                    .count();
        }

        template<class Rep, class Period = std::ratio<1>>
        [[nodiscard("Should not be used directly"
        )]] inline lazy_timeout_base(std::chrono::duration<Rep, Period> duration
        ) noexcept {
            set_ts(duration);
        }
    };

    struct lazy_timeout : lazy_timeout_base {
        template<class Rep, class Period = std::ratio<1>>
        [[nodiscard("Did you forget to co_await?")]] inline lazy_timeout(
            std::chrono::duration<Rep, Period> duration, unsigned int flags
        ) noexcept
            : lazy_timeout_base(duration) {
            sqe->prepareTimeout(&ts, 0, flags);
        }
    };

    struct lazy_link_timeout_base : lazy_timeout_base {
        template<class Rep, class Period = std::ratio<1>>
        [[nodiscard("Should not be used directly")]] inline lazy_link_timeout_base(
            std::chrono::duration<Rep, Period> duration, unsigned int flags
        ) noexcept
            : lazy_timeout_base(duration) {
            sqe->prepareLinkTimeout(&this->ts, flags);
        }
    };

    struct lazy_link_timeout : lazy_link_io {
        lazy_link_timeout_base timer;

        template<class Rep, class Period = std::ratio<1>>
        [[nodiscard("Did you forget to co_await?")]] inline lazy_link_timeout(
            lazy_awaiter &&timed_io,
            std::chrono::duration<Rep, Period> duration,
            unsigned int flags
        ) noexcept
            : timer(duration, flags) {
            auto &worker = *detail::this_thread.worker;
            // Mark timed_io as normal task type, and set sqe link.
            timed_io.sqe->setLink();
            // Mark timer as lazy_link_sqe task type, and without sqe link.
            timer.io_info.type = task_info::task_type::lazy_link_sqe;
            // Send the result to timed_io.
            this->last_io = &timed_io;
        }
    };

    struct lazy_yield {
        constexpr bool await_ready() const noexcept { return false; }

        void await_suspend(std::coroutine_handle<> current) noexcept {
            auto &worker = *detail::this_thread.worker;
            worker.co_spawn(current);
        }

        constexpr void await_resume() const noexcept {}

        constexpr lazy_yield() noexcept = default;
    };

    struct lazy_splice : lazy_awaiter {
        [[nodiscard("Did you forget to co_await?")]] inline lazy_splice(
            int fd_in,
            int64_t off_in,
            int fd_out,
            int64_t off_out,
            unsigned int nbytes,
            unsigned int splice_flags
        ) noexcept {
            sqe->prepareSplice(
                fd_in, off_in, fd_out, off_out, nbytes, splice_flags
            );
        }
    };

    struct lazy_tee : lazy_awaiter {
        [[nodiscard("Did you forget to co_await?")]] inline lazy_tee(
            int fd_in,
            int fd_out,
            unsigned int nbytes,
            unsigned int splice_flags
        ) noexcept {
            sqe->prepareTee(fd_in, fd_out, nbytes, splice_flags);
        }
    };

} // namespace detail

inline namespace lazy {

    inline detail::lazy_yield yield() noexcept {
        return {};
    }

    inline detail::lazy_read
    read(int fd, std::span<char> buf, uint64_t offset) noexcept {
        return detail::lazy_read{fd, buf, offset};
    }

    inline detail::lazy_readv
    readv(int fd, std::span<const iovec> iovecs, uint64_t offset) noexcept {
        return detail::lazy_readv{fd, iovecs, offset};
    }

    inline detail::lazy_read_fixed read_fixed(
        int fd, std::span<char> buf, uint64_t offset, uint16_t bufIndex
    ) noexcept {
        return detail::lazy_read_fixed{fd, buf, offset, bufIndex};
    }

    inline detail::lazy_write
    write(int fd, std::span<const char> buf, uint64_t offset) noexcept {
        return detail::lazy_write{fd, buf, offset};
    }

    inline detail::lazy_writev
    writev(int fd, std::span<const iovec> iovecs, uint64_t offset) noexcept {
        return detail::lazy_writev{fd, iovecs, offset};
    }

    inline detail::lazy_write_fixed write_fixed(
        int fd, std::span<const char> buf, uint64_t offset, uint16_t bufIndex
    ) noexcept {
        return detail::lazy_write_fixed{fd, buf, offset, bufIndex};
    }

    inline detail::lazy_accept
    accept(int fd, sockaddr *addr, socklen_t *addrlen, int flags) noexcept {
        return detail::lazy_accept{fd, addr, addrlen, flags};
    }

    inline detail::lazy_accept_direct accept_direct(
        int fd,
        sockaddr *addr,
        socklen_t *addrlen,
        int flags,
        uint32_t fileIndex
    ) noexcept {
        return detail::lazy_accept_direct{fd, addr, addrlen, flags, fileIndex};
    }

    [[deprecated("Consider cancel_fd instead.")]] inline detail::lazy_cancel
    cancel(uint64_t user_data, int flags) noexcept {
        return detail::lazy_cancel{user_data, flags};
    }

    inline detail::lazy_cancel_fd cancel(int fd, unsigned int flags) noexcept {
        return detail::lazy_cancel_fd{fd, flags};
    }

    inline detail::lazy_recv
    recv(int sockfd, std::span<char> buf, int flags = 0) noexcept {
        return detail::lazy_recv{sockfd, buf, flags};
    }

    inline detail::lazy_recvmsg
    recvmsg(int fd, msghdr *msg, unsigned int flags) noexcept {
        return detail::lazy_recvmsg{fd, msg, flags};
    }

    inline detail::lazy_send
    send(int sockfd, std::span<const char> buf, int flags = 0) noexcept {
        return detail::lazy_send{sockfd, buf, flags};
    }

    inline detail::lazy_sendmsg
    sendmsg(int fd, const msghdr *msg, unsigned int flags) noexcept {
        return detail::lazy_sendmsg{fd, msg, flags};
    }

    inline detail::lazy_connect
    connect(int sockfd, const sockaddr *addr, socklen_t addrlen) noexcept {
        return detail::lazy_connect{sockfd, addr, addrlen};
    }

    inline detail::lazy_close close(int fd) noexcept {
        return detail::lazy_close{fd};
    }

    inline detail::lazy_shutdown shutdown(int fd, int how) noexcept {
        return detail::lazy_shutdown{fd, how};
    }

    inline detail::lazy_fsync fsync(int fd, uint32_t fsync_flags) noexcept {
        return detail::lazy_fsync{fd, fsync_flags};
    }

    inline detail::lazy_sync_file_range
    sync_file_range(int fd, uint32_t len, uint64_t offset, int flags) noexcept {
        return detail::lazy_sync_file_range{fd, len, offset, flags};
    }

    inline detail::lazy_uring_nop uring_nop() noexcept {
        return detail::lazy_uring_nop{};
    }

    inline detail::lazy_files_update
    files_update(std::span<int> fds, int offset) noexcept {
        return detail::lazy_files_update{fds, offset};
    }

    inline detail::lazy_fallocate
    fallocate(int fd, int mode, off_t offset, off_t len) noexcept {
        return detail::lazy_fallocate{fd, mode, offset, len};
    }

    inline detail::lazy_openat
    openat(int dfd, const char *path, int flags, mode_t mode) noexcept {
        return detail::lazy_openat{dfd, path, flags, mode};
    }

    inline detail::lazy_openat_direct openat_direct(
        int dfd,
        const char *path,
        int flags,
        mode_t mode,
        unsigned int file_index
    ) noexcept {
        return detail::lazy_openat_direct{dfd, path, flags, mode, file_index};
    }

    inline detail::lazy_openat2
    openat2(int dfd, const char *path, open_how *how) noexcept {
        return detail::lazy_openat2{dfd, path, how};
    }

    inline detail::lazy_openat2_direct openat2_direct(
        int dfd, const char *path, open_how *how, unsigned int file_index
    ) noexcept {
        return detail::lazy_openat2_direct{dfd, path, how, file_index};
    }

    inline detail::lazy_statx statx(
        int dfd,
        const char *path,
        int flags,
        unsigned int mask,
        struct statx *statxbuf
    ) noexcept {
        return detail::lazy_statx{dfd, path, flags, mask, statxbuf};
    }

    inline detail::lazy_unlinkat
    unlinkat(int dfd, const char *path, int flags) noexcept {
        return detail::lazy_unlinkat{dfd, path, flags};
    }

    inline detail::lazy_renameat renameat(
        int olddfd,
        const char *oldpath,
        int newdfd,
        const char *newpath,
        int flags
    ) noexcept {
        return detail::lazy_renameat{olddfd, oldpath, newdfd, newpath, flags};
    }

    inline detail::lazy_mkdirat
    mkdirat(int dfd, const char *path, mode_t mode) noexcept {
        return detail::lazy_mkdirat{dfd, path, mode};
    }

    inline detail::lazy_symlinkat
    symlinkat(const char *target, int newdirfd, const char *linkpath) noexcept {
        return detail::lazy_symlinkat{target, newdirfd, linkpath};
    }

    inline detail::lazy_linkat linkat(
        int olddfd,
        const char *oldpath,
        int newdfd,
        const char *newpath,
        int flags
    ) noexcept {
        return detail::lazy_linkat{olddfd, oldpath, newdfd, newpath, flags};
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
    inline detail::lazy_timeout_timespec timeout(
        __kernel_timespec *ts, unsigned int count, unsigned int flags
    ) noexcept {
        return detail::lazy_timeout_timespec{ts, count, flags};
    }

    /**
     * @brief Set duration timeout.
     *
     * @param flags See man io_uring_enter(2).
     * @return lazy_awaiter
     */
    template<class Rep, class Period = std::ratio<1>>
    inline detail::lazy_timeout timeout(
        std::chrono::duration<Rep, Period> duration, unsigned int flags = 0
    ) noexcept {
        return detail::lazy_timeout{duration, flags};
    }

    template<class Rep, class Period = std::ratio<1>>
    inline detail::lazy_link_timeout timeout(
        detail::lazy_awaiter &&timed_io,
        std::chrono::duration<Rep, Period> duration,
        unsigned int flags = 0
    ) noexcept {
        return detail::lazy_link_timeout{std::move(timed_io), duration, flags};
    }

    /**
     * @pre Either fd_in or fd_out must be a pipe.
     * @param off_in If fd_in refers to a pipe, off_in must be (int64_t) -1;
     * If fd_in does not refer to a pipe and off_in is (int64_t) -1,
     * then bytes are read from fd_in starting from the file offset and it is
     * adjust appropriately; If fd_in does not refer to a pipe and off_in is not
     * (int64_t) -1, then the starting offset of fd_in will be off_in.
     * @param off_out The description of off_in also applied to off_out.
     * @param splice_flags see man splice(2) for description of flags.
     *
     * This splice operation can be used to implement sendfile by splicing
     * to an intermediate pipe first, then splice to the final destination.
     * In fact, the implementation of sendfile in kernel uses splice
     * internally.
     *
     * NOTE that even if fd_in or fd_out refers to a pipe, the splice
     * operation can still failed with EINVAL if one of the fd doesn't
     * explicitly support splice operation, e.g. reading from terminal is
     * unsupported from kernel 5.7 to 5.11. Check issue #291 for more
     * information.
     */
    inline detail::lazy_splice splice(
        int fd_in,
        int64_t off_in,
        int fd_out,
        int64_t off_out,
        unsigned int nbytes,
        unsigned int splice_flags
    ) noexcept {
        return detail::lazy_splice{fd_in,   off_in, fd_out,
                                   off_out, nbytes, splice_flags};
    }

    inline detail::lazy_tee
    tee(int fd_in, int fd_out, unsigned int nbytes, unsigned int splice_flags
    ) noexcept {
        return detail::lazy_tee{fd_in, fd_out, nbytes, splice_flags};
    }

} // namespace lazy

} // namespace co_context
