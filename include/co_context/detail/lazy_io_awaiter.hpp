#pragma once

#include <co_context/detail/thread_meta.hpp>
#include <co_context/detail/user_data.hpp>
#include <co_context/io_context.hpp>
#include <co_context/utility/time_cast.hpp>
#include <uring/utility/kernel_version.hpp>

#include <cassert>
#include <chrono>
#include <coroutine>
#include <cstdint>
#include <span>
#include <type_traits>

namespace co_context::detail {

struct lazy_link_io {
    class lazy_awaiter *last_io;

    static constexpr bool await_ready() noexcept { return false; }

    void await_suspend(std::coroutine_handle<> current) const noexcept;

    /*NOLINT*/ int32_t await_resume() const noexcept;
};

class lazy_awaiter {
  public:
    [[nodiscard]]
    int32_t result() const noexcept {
        return io_info.result;
    }

    static constexpr bool await_ready() noexcept { return false; }

    void await_suspend(std::coroutine_handle<> current) noexcept {
        io_info.handle = current;
    }

    lazy_awaiter &set_async() & noexcept {
        sqe->set_async();
        return *this;
    }

    lazy_awaiter &&set_async() && noexcept {
        sqe->set_async();
        return std::move(*this);
    }

    /*NOLINT*/ int32_t await_resume() const noexcept { return result(); }

    std::suspend_never detach() && noexcept {
#if LIBURINGCXX_IS_KERNEL_REACH(5, 17)
        assert(!sqe->is_cqe_skip());
        sqe->set_cqe_skip();
        --this_thread.worker->requests_to_reap;
#else
        sqe->set_data(uint64_t(reserved_user_data::nop));
#endif
        return {};
    }

    [[nodiscard]]
    uint64_t user_data() const noexcept {
        return sqe->get_data();
    }

  protected:
    friend struct lazy_link_io;
    friend struct lazy_link_timeout;
    liburingcxx::sq_entry *sqe;
    task_info io_info;

    friend lazy_link_io
    operator&&(lazy_awaiter &&lhs, lazy_awaiter &&rhs) noexcept;

    friend lazy_link_io &&
    operator&&(lazy_link_io &&lhs, lazy_awaiter &&rhs) noexcept;

    friend lazy_link_io &&
    operator&&(lazy_link_io &&lhs, lazy_link_io &&rhs) noexcept;

    friend lazy_link_io &&
    operator&&(lazy_awaiter &&lhs, struct lazy_link_timeout &&rhs) noexcept;

    friend void set_link_awaiter(lazy_awaiter &awaiter) noexcept;

    lazy_awaiter() noexcept : sqe(this_thread.worker->get_free_sqe()) {
        sqe->set_data(
            io_info.as_user_data() | uint64_t(user_data_type::task_info_ptr)
        );
    }

#ifndef __INTELLISENSE__

  public:
    lazy_awaiter(const lazy_awaiter &) = delete;
    lazy_awaiter(lazy_awaiter &&) = delete;
    lazy_awaiter &operator=(const lazy_awaiter &) = delete;
    lazy_awaiter &operator=(lazy_awaiter &&) = delete;
#endif
};

inline void set_link_sqe(liburingcxx::sq_entry *sqe) noexcept {
    sqe->set_link();
    sqe->fetch_data() |= uint64_t(user_data_type::task_info_ptr__link_sqe);
}

inline void set_link_awaiter(lazy_awaiter &awaiter) noexcept {
    set_link_sqe(awaiter.sqe);
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
    return static_cast<lazy_link_io &&>(lhs);
}

inline lazy_link_io &&
operator&&(lazy_link_io &&lhs, lazy_link_io &&rhs) noexcept {
    set_link_link_io(lhs);
    return static_cast<lazy_link_io &&>(rhs);
}

inline void lazy_link_io::await_suspend(std::coroutine_handle<> current
) const noexcept {
    this->last_io->io_info.handle = current;
}

inline int32_t lazy_link_io::await_resume() const noexcept {
    return this->last_io->io_info.result;
}

struct lazy_splice : lazy_awaiter {
    inline lazy_splice(
        int fd_in,
        int64_t off_in,
        int fd_out,
        int64_t off_out,
        unsigned int nbytes,
        unsigned int splice_flags
    ) noexcept {
        sqe->prep_splice(fd_in, off_in, fd_out, off_out, nbytes, splice_flags);
    }
};

struct lazy_tee : lazy_awaiter {
    inline lazy_tee(
        int fd_in, int fd_out, unsigned int nbytes, unsigned int splice_flags
    ) noexcept {
        sqe->prep_tee(fd_in, fd_out, nbytes, splice_flags);
    }
};

struct lazy_readv : lazy_awaiter {
    inline lazy_readv(
        int fd, std::span<const iovec> iovecs, uint64_t offset
    ) noexcept {
        sqe->prep_readv(fd, iovecs, offset);
    }
};

struct lazy_readv2 : lazy_awaiter {
    inline lazy_readv2(
        int fd, std::span<const iovec> iovecs, uint64_t offset, int flags
    ) noexcept {
        sqe->prep_readv2(fd, iovecs, offset, flags);
    }
};

struct lazy_read_fixed : lazy_awaiter {
    inline lazy_read_fixed(
        int fd, std::span<char> buf, uint64_t offset, uint16_t buf_index
    ) noexcept {
        sqe->prep_read_fixed(fd, buf, offset, buf_index);
    }
};

struct lazy_writev : lazy_awaiter {
    inline lazy_writev(
        int fd, std::span<const iovec> iovecs, uint64_t offset
    ) noexcept {
        sqe->prep_writev(fd, iovecs, offset);
    }
};

struct lazy_writev2 : lazy_awaiter {
    inline lazy_writev2(
        int fd, std::span<const iovec> iovecs, uint64_t offset, int flags
    ) noexcept {
        sqe->prep_writev2(fd, iovecs, offset, flags);
    }
};

struct lazy_write_fixed : lazy_awaiter {
    inline lazy_write_fixed(
        int fd, std::span<const char> buf, uint64_t offset, uint16_t buf_index
    ) noexcept {
        sqe->prep_write_fixed(fd, buf, offset, buf_index);
    }
};

struct lazy_recvmsg : lazy_awaiter {
    inline lazy_recvmsg(int fd, msghdr *msg, unsigned flags) noexcept {
        sqe->prep_recvmsg(fd, msg, flags);
    }
};

#if LIBURINGCXX_IS_KERNEL_REACH(5, 20)
struct lazy_recvmsg_multishot : lazy_awaiter {
    inline
    lazy_recvmsg_multishot(int fd, msghdr *msg, unsigned flags) noexcept {
        sqe->prep_recvmsg_multishot(fd, msg, flags);
    }
};
#endif

struct lazy_sendmsg : lazy_awaiter {
    inline
    lazy_sendmsg(int fd, const msghdr *msg, unsigned int flags) noexcept {
        sqe->prep_sendmsg(fd, msg, flags);
    }
};

struct lazy_poll_add : lazy_awaiter {
    inline lazy_poll_add(int fd, unsigned poll_mask) noexcept {
        sqe->prep_poll_add(fd, poll_mask);
    }
};

struct lazy_poll_multishot : lazy_awaiter {
    inline lazy_poll_multishot(int fd, unsigned poll_mask) noexcept {
        sqe->prep_poll_multishot(fd, poll_mask);
    }
};

struct lazy_poll_remove : lazy_awaiter {
    inline explicit lazy_poll_remove(uint64_t user_data) noexcept {
        sqe->prep_poll_remove(user_data);
    }
};

struct lazy_poll_update : lazy_awaiter {
    inline lazy_poll_update(
        uint64_t old_user_data,
        uint64_t new_user_data,
        unsigned poll_mask,
        unsigned flags
    ) noexcept {
        sqe->prep_poll_update(old_user_data, new_user_data, poll_mask, flags);
    }
};

struct lazy_fsync : lazy_awaiter {
    inline lazy_fsync(int fd, uint32_t fsync_flags) noexcept {
        sqe->prep_fsync(fd, fsync_flags);
    }

    inline lazy_fsync(
        int fd, uint32_t fsync_flags, uint64_t offset, uint32_t len
    ) noexcept {
        sqe->prep_fsync(fd, fsync_flags, offset, len);
    }
};

struct lazy_uring_nop : lazy_awaiter {
    inline lazy_uring_nop() noexcept { sqe->prep_nop(); }
};

struct lazy_timeout_timespec : lazy_awaiter {
  public:
    inline lazy_timeout_timespec(
        const __kernel_timespec &ts, unsigned int count, unsigned int flags
    ) noexcept {
        sqe->prep_timeout(ts, count, flags);
    }

  protected:
    inline lazy_timeout_timespec() noexcept = default;
};

struct lazy_timeout_base : lazy_awaiter {
  protected:
    __kernel_timespec ts;

  public:
    template<class Rep, class Period>
    void set_ts(std::chrono::duration<Rep, Period> duration) noexcept {
        ts = to_kernel_timespec_biased(duration);
    }

    template<class Duration>
    void set_ts(
        std::chrono::time_point<std::chrono::steady_clock, Duration> time_point
    ) noexcept {
        ts = to_kernel_timespec_biased(time_point);
    }

    template<class Duration>
    void set_ts(
        std::chrono::time_point<std::chrono::system_clock, Duration> time_point
    ) noexcept {
        ts = to_kernel_timespec_biased(time_point);
    }

    template<class Expire>
    // Should not be used directly
    inline explicit lazy_timeout_base(Expire expire) noexcept {
        set_ts(expire);
    }
};

inline constexpr uint32_t pure_timer_flag =
    liburingcxx::is_kernel_reach(6, 0) ? IORING_TIMEOUT_ETIME_SUCCESS : 0;

inline constexpr uint32_t timeout_relative_flag =
    liburingcxx::is_kernel_reach(5, 15) ? IORING_TIMEOUT_BOOTTIME : 0;

inline constexpr uint32_t timeout_absolute_steady_flag = IORING_TIMEOUT_ABS;

inline constexpr uint32_t timeout_absolute_realtime_flag =
    IORING_TIMEOUT_ABS
    | (liburingcxx::is_kernel_reach(5, 15) ? IORING_TIMEOUT_REALTIME : 0);

struct lazy_timeout : lazy_timeout_base {
    template<class Rep, class Period>
    inline explicit lazy_timeout(std::chrono::duration<Rep, Period> duration
    ) noexcept
        : lazy_timeout_base(duration) {
        sqe->prep_timeout(ts, 0, timeout_relative_flag | pure_timer_flag);
    }

    template<class Duration>
    inline explicit lazy_timeout(
        std::chrono::time_point<std::chrono::steady_clock, Duration> time_point
    ) noexcept
        : lazy_timeout_base(time_point) {
        sqe->prep_timeout(
            ts, 0, timeout_absolute_steady_flag | pure_timer_flag
        );
    }

    template<class Duration>
    inline explicit lazy_timeout(
        std::chrono::time_point<std::chrono::system_clock, Duration> time_point
    ) noexcept
        : lazy_timeout_base(time_point) {
        sqe->prep_timeout(
            ts, 0, timeout_absolute_realtime_flag | pure_timer_flag
        );
    }
};

struct lazy_timeout_remove : lazy_awaiter {
    inline lazy_timeout_remove(uint64_t user_data, unsigned flags) noexcept {
        sqe->prep_timeout_remove(user_data, flags);
    }
};

struct lazy_timeout_update : lazy_timeout_base {
    template<class Rep, class Period>
    inline lazy_timeout_update(
        std::chrono::duration<Rep, Period> duration,
        uint64_t user_data,
        unsigned flags
    ) noexcept
        : lazy_timeout_base(duration) {
        sqe->prep_timeout_update(ts, user_data, flags);
    }
};

struct lazy_accept : lazy_awaiter {
    inline lazy_accept(
        int fd, sockaddr *addr, socklen_t *addrlen, int flags
    ) noexcept {
        sqe->prep_accept(fd, addr, addrlen, flags);
    }
};

struct lazy_accept_direct : lazy_awaiter {
    inline lazy_accept_direct(
        int fd,
        sockaddr *addr,
        socklen_t *addrlen,
        int flags,
        uint32_t file_index
    ) noexcept {
        sqe->prep_accept_direct(fd, addr, addrlen, flags, file_index);
    }
};

#if LIBURINGCXX_IS_KERNEL_REACH(5, 19)
struct lazy_multishot_accept : lazy_awaiter {
    inline lazy_multishot_accept(
        int fd, sockaddr *addr, socklen_t *addrlen, int flags
    ) noexcept {
        sqe->prep_multishot_accept(fd, addr, addrlen, flags);
    }
};
#endif

#if LIBURINGCXX_IS_KERNEL_REACH(5, 19)
struct lazy_multishot_accept_direct : lazy_awaiter {
    inline lazy_multishot_accept_direct(
        int fd, sockaddr *addr, socklen_t *addrlen, int flags
    ) noexcept {
        sqe->prep_multishot_accept_direct(fd, addr, addrlen, flags);
    }
};
#endif

// Available since Linux 5.5
struct lazy_cancel : lazy_awaiter {
    inline lazy_cancel(uint64_t user_data, int flags) noexcept {
        sqe->prep_cancle(user_data, flags);
    }
};

struct lazy_cancel_fd : lazy_awaiter {
    inline lazy_cancel_fd(int fd, unsigned int flags) noexcept {
        sqe->prep_cancle_fd(fd, flags);
    }
};

struct lazy_link_timeout_base : lazy_timeout_base {
    template<class Expire>
    // Should not be used directly
    inline lazy_link_timeout_base(Expire expire, unsigned int flags) noexcept
        : lazy_timeout_base(expire) {
        sqe->prep_link_timeout(this->ts, flags);
#if LIBURINGCXX_IS_KERNEL_REACH(5, 17)
        sqe->set_cqe_skip();
        --this_thread.worker->requests_to_reap;
#else
        // Mark timer as lazy_link_sqe task type, but without sqe link.
        // The purpose is to make io_context to handle the timed_io and ignore
        // the timer.
        sqe->set_data(
            this->io_info.as_user_data()
            | uint8_t(user_data_type::task_info_ptr__link_sqe)
        );
#endif
    }
};

struct lazy_link_timeout
    : lazy_link_io
    , lazy_link_timeout_base {
    using lazy_link_io::await_ready;
    using lazy_link_io::await_suspend;
    using lazy_link_io::await_resume;

    std::suspend_never detach() && noexcept {
        std::move(*(this->last_io)).detach();
        return {};
    }

  private:
    void arrange_io(lazy_awaiter &&timed_io) noexcept {
        // Mark timed_io as normal task type, but set sqe link.
        timed_io.sqe->set_link();
        // Mark timer as lazy_link_sqe task type, but without sqe link.
        // The purpose is to make io_context to handle the timed_io and ignore
        // the timer.

        // Send the result to timed_io.
        this->last_io = &timed_io;
    }

  public:
    template<class Rep, class Period>
    inline lazy_link_timeout(
        lazy_awaiter &&timed_io, std::chrono::duration<Rep, Period> duration
    ) noexcept
        : lazy_link_timeout_base(duration, timeout_relative_flag) {
        arrange_io(std::move(timed_io));
    }

    template<class Duration>
    inline lazy_link_timeout(
        lazy_awaiter &&timed_io,
        std::chrono::time_point<std::chrono::steady_clock, Duration> time_point
    ) noexcept
        : lazy_link_timeout_base(time_point, timeout_absolute_steady_flag) {
        arrange_io(std::move(timed_io));
    }

    template<class Duration>
    inline lazy_link_timeout(
        lazy_awaiter &&timed_io,
        std::chrono::time_point<std::chrono::system_clock, Duration> time_point
    ) noexcept
        : lazy_link_timeout_base(time_point, timeout_absolute_realtime_flag) {
        arrange_io(std::move(timed_io));
    }
};

struct lazy_connect : lazy_awaiter {
    inline
    lazy_connect(int sockfd, const sockaddr *addr, socklen_t addrlen) noexcept {
        sqe->prep_connect(sockfd, addr, addrlen);
    }
};

struct lazy_files_update : lazy_awaiter {
    inline lazy_files_update(std::span<int> fds, int offset) noexcept {
        sqe->prep_files_update(fds, offset);
    }
};

struct lazy_fallocate : lazy_awaiter {
    inline
    lazy_fallocate(int fd, int mode, uint64_t offset, uint64_t len) noexcept {
        sqe->prep_fallocate(fd, mode, offset, len);
    }
};

struct lazy_openat : lazy_awaiter {
    inline
    lazy_openat(int dfd, const char *path, int flags, mode_t mode) noexcept {
        sqe->prep_openat(dfd, path, flags, mode);
    }
};

/* open directly into the fixed file table */
struct lazy_openat_direct : lazy_awaiter {
    inline lazy_openat_direct(
        int dfd, const char *path, int flags, mode_t mode, unsigned file_index
    ) noexcept {
        sqe->prep_openat_direct(dfd, path, flags, mode, file_index);
    }
};

struct lazy_close : lazy_awaiter {
    inline explicit lazy_close(int fd) noexcept { sqe->prep_close(fd); }
};

struct lazy_close_direct : lazy_awaiter {
    inline explicit lazy_close_direct(unsigned file_index) noexcept {
        sqe->prep_close_direct(file_index);
    }
};

struct lazy_read : lazy_awaiter {
    inline lazy_read(int fd, std::span<char> buf, uint64_t offset) noexcept {
        sqe->prep_read(fd, buf, offset);
    }
};

struct lazy_write : lazy_awaiter {
    inline
    lazy_write(int fd, std::span<const char> buf, uint64_t offset) noexcept {
        sqe->prep_write(fd, buf, offset);
    }
};

struct lazy_statx : lazy_awaiter {
    inline lazy_statx(
        int dfd,
        const char *path,
        int flags,
        unsigned int mask,
        struct statx *statxbuf
    ) noexcept {
        sqe->prep_statx(dfd, path, flags, mask, statxbuf);
    }
};

struct lazy_fadvise : lazy_awaiter {
    inline
    lazy_fadvise(int fd, uint64_t offset, off_t len, int advice) noexcept {
        sqe->prep_fadvise(fd, offset, len, advice);
    }
};

struct lazy_madvise : lazy_awaiter {
    inline lazy_madvise(void *addr, off_t length, int advice) noexcept {
        sqe->prep_madvise(addr, length, advice);
    }
};

struct lazy_send : lazy_awaiter {
    inline
    lazy_send(int sockfd, std::span<const char> buf, int flags) noexcept {
        sqe->prep_send(sockfd, buf, flags);
    }
};

struct lazy_send_zc : lazy_awaiter {
    inline lazy_send_zc(
        int sockfd, std::span<const char> buf, int flags, unsigned zc_flags
    ) noexcept {
        sqe->prep_send_zc(sockfd, buf, flags, zc_flags);
    }
};

struct lazy_send_zc_fixed : lazy_awaiter {
    inline lazy_send_zc_fixed(
        int sockfd,
        std::span<const char> buf,
        int flags,
        unsigned zc_flags,
        unsigned buf_index
    ) noexcept {
        sqe->prep_send_zc_fixed(sockfd, buf, flags, zc_flags, buf_index);
    }
};

struct lazy_sendmsg_zc : lazy_awaiter {
    inline lazy_sendmsg_zc(int fd, const msghdr *msg, unsigned flags) noexcept {
        sqe->prep_sendmsg_zc(fd, msg, flags);
    }
};

// TODO deal with prep_send_set_addr()

struct lazy_recv : lazy_awaiter {
    inline lazy_recv(int sockfd, std::span<char> buf, int flags) noexcept {
        sqe->prep_recv(sockfd, buf, flags);
    }
};

#if LIBURINGCXX_IS_KERNEL_REACH(5, 20)
struct lazy_recv_multishot : lazy_awaiter {
    inline
    lazy_recv_multishot(int sockfd, std::span<char> buf, int flags) noexcept {
        sqe->prep_recv_multishot(sockfd, buf, flags);
    }
};
#endif

#ifdef LIBURINGCXX_HAS_OPENAT2
struct lazy_openat2 : lazy_awaiter {
    inline lazy_openat2(int dfd, const char *path, open_how *how) noexcept {
        sqe->prep_openat2(dfd, path, how);
    }
};
#endif

#ifdef LIBURINGCXX_HAS_OPENAT2
/* open directly into the fixed file table */
struct lazy_openat2_direct : lazy_awaiter {
    inline lazy_openat2_direct(
        int dfd, const char *path, open_how *how, unsigned int file_index
    ) noexcept {
        sqe->prep_openat2_direct(dfd, path, how, file_index);
    }
};
#endif

struct lazy_epoll_ctl : lazy_awaiter {
    inline lazy_epoll_ctl(int epfd, int fd, int op, epoll_event *ev) noexcept {
        sqe->prep_epoll_ctl(epfd, fd, op, ev);
    }
};

struct lazy_provide_buffers : lazy_awaiter {
    inline lazy_provide_buffers(
        const void *addr, int len, int nr, int bgid, int bid
    ) noexcept {
        sqe->prep_provide_buffers(addr, len, nr, bgid, bid);
    }
};

struct lazy_remove_buffers : lazy_awaiter {
    inline lazy_remove_buffers(int nr, int bgid) noexcept {
        sqe->prep_remove_buffers(nr, bgid);
    }
};

struct lazy_shutdown : lazy_awaiter {
    inline lazy_shutdown(int fd, int how) noexcept {
        sqe->prep_shutdown(fd, how);
    }
};

struct lazy_unlinkat : lazy_awaiter {
    inline lazy_unlinkat(int dfd, const char *path, int flags) noexcept {
        sqe->prep_unlinkat(dfd, path, flags);
    }
};

struct lazy_unlink : lazy_awaiter {
    inline lazy_unlink(const char *path, int flags) noexcept {
        sqe->prep_unlink(path, flags);
    }
};

struct lazy_renameat : lazy_awaiter {
    inline lazy_renameat(
        int olddfd,
        const char *oldpath,
        int newdfd,
        const char *newpath,
        int flags
    ) noexcept {
        sqe->prep_renameat(olddfd, oldpath, newdfd, newpath, flags);
    }
};

struct lazy_rename : lazy_awaiter {
    inline lazy_rename(const char *oldpath, const char *newpath) noexcept {
        sqe->prep_rename(oldpath, newpath);
    }
};

struct lazy_sync_file_range : lazy_awaiter {
    inline lazy_sync_file_range(
        int fd, uint32_t len, uint64_t offset, int flags
    ) noexcept {
        sqe->prep_sync_file_range(fd, len, offset, flags);
    }
};

struct lazy_mkdirat : lazy_awaiter {
    inline lazy_mkdirat(int dfd, const char *path, mode_t mode) noexcept {
        sqe->prep_mkdirat(dfd, path, mode);
    }
};

struct lazy_mkdir : lazy_awaiter {
    inline lazy_mkdir(const char *path, mode_t mode) noexcept {
        sqe->prep_mkdir(path, mode);
    }
};

struct lazy_symlinkat : lazy_awaiter {
    inline lazy_symlinkat(
        const char *target, int newdirfd, const char *linkpath
    ) noexcept {
        sqe->prep_symlinkat(target, newdirfd, linkpath);
    }
};

struct lazy_symlink : lazy_awaiter {
    inline lazy_symlink(const char *target, const char *linkpath) noexcept {
        sqe->prep_symlink(target, linkpath);
    }
};

struct lazy_linkat : lazy_awaiter {
    inline lazy_linkat(
        int olddfd,
        const char *oldpath,
        int newdfd,
        const char *newpath,
        int flags
    ) noexcept {
        sqe->prep_linkat(olddfd, oldpath, newdfd, newpath, flags);
    }
};

struct lazy_link : lazy_awaiter {
    inline
    lazy_link(const char *oldpath, const char *newpath, int flags) noexcept {
        sqe->prep_link(oldpath, newpath, flags);
    }
};

#if LIBURINGCXX_IS_KERNEL_REACH(5, 18)
struct lazy_msg_ring : lazy_awaiter {
    inline lazy_msg_ring(
        int fd, uint32_t cqe_res, uint64_t cqe_user_data, uint32_t flags
    ) noexcept {
        sqe->prep_msg_ring(fd, cqe_res, cqe_user_data, flags);
    }
};
#endif

#if LIBURINGCXX_IS_KERNEL_REACH(6, 2)
struct lazy_msg_ring_cqe_flags : lazy_awaiter {
    inline lazy_msg_ring_cqe_flags(
        int fd,
        uint32_t cqe_res,
        uint64_t cqe_user_data,
        uint32_t flags,
        uint32_t cqe_flags
    ) noexcept {
        sqe->prep_msg_ring_cqe_flags(
            fd, cqe_res, cqe_user_data, flags, cqe_flags
        );
    }
};
#endif

struct lazy_getxattr : lazy_awaiter {
    inline lazy_getxattr(
        const char *name, char *value, const char *path, size_t len
    ) noexcept {
        sqe->prep_getxattr(name, value, path, len);
    }
};

struct lazy_setxattr : lazy_awaiter {
    inline lazy_setxattr(
        const char *name, char *value, const char *path, int flags, size_t len
    ) noexcept {
        sqe->prep_setxattr(name, value, path, flags, len);
    }
};

struct lazy_fgetxattr : lazy_awaiter {
    inline
    lazy_fgetxattr(int fd, const char *name, char *value, size_t len) noexcept {
        sqe->prep_fgetxattr(fd, name, value, len);
    }
};

struct lazy_fsetxattr : lazy_awaiter {
    inline lazy_fsetxattr(
        int fd, const char *name, const char *value, int flags, size_t len
    ) noexcept {
        sqe->prep_fsetxattr(fd, name, value, flags, len);
    }
};

struct lazy_socket : lazy_awaiter {
    inline lazy_socket(
        int domain, int type, int protocol, unsigned int flags
    ) noexcept {
        sqe->prep_socket(domain, type, protocol, flags);
    }
};

struct lazy_socket_direct : lazy_awaiter {
    inline lazy_socket_direct(
        int domain,
        int type,
        int protocol,
        unsigned file_index,
        unsigned int flags
    ) noexcept {
        sqe->prep_socket_direct(domain, type, protocol, file_index, flags);
    }
};

struct lazy_socket_direct_alloc : lazy_awaiter {
    inline lazy_socket_direct_alloc(
        int domain, int type, int protocol, unsigned int flags
    ) noexcept {
        sqe->prep_socket_direct_alloc(domain, type, protocol, flags);
    }
};

struct lazy_yield {
    static constexpr bool await_ready() noexcept { return false; }

    static void await_suspend(std::coroutine_handle<> current) noexcept {
        auto &worker = *detail::this_thread.worker;
        worker.co_spawn_unsafe(current);
    }

    constexpr void await_resume() const noexcept {}

    constexpr lazy_yield() noexcept = default;
};

struct lazy_who_am_i {
    static constexpr bool await_ready() noexcept { return false; }

    constexpr bool await_suspend(std::coroutine_handle<> current) noexcept {
        handle = current;
        return false;
    }

    [[nodiscard]]
    std::coroutine_handle<> await_resume() const noexcept {
        return handle;
    }

    constexpr lazy_who_am_i() noexcept = default;

    std::coroutine_handle<> handle;
};

using lazy_forget = std::suspend_always;

class lazy_resume_on {
  public:
    static constexpr bool await_ready() noexcept { return false; }

    bool await_suspend(std::coroutine_handle<> current) const noexcept {
        if (resume_ctx != detail::this_thread.ctx) [[likely]] {
            resume_ctx->worker.co_spawn_auto(current);
            return true;
        }
        return false;
    }

    constexpr void await_resume() const noexcept {}

    explicit lazy_resume_on(co_context::io_context &resume_context) noexcept
        : resume_ctx(&resume_context) {}

  private:
    co_context::io_context *resume_ctx;
};

/****************************
 *    Helper for link_io    *
 ****************************
 */

inline lazy_link_io &&
operator&&(lazy_awaiter &&lhs, struct lazy_link_timeout &&rhs) noexcept {
    set_link_awaiter(lhs);
    return std::move(rhs);
}

} // namespace co_context::detail
