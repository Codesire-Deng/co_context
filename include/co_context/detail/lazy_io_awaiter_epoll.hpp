#pragma once

#include "co_context/detail/epoll.hpp"
#include "co_context/detail/thread_meta.hpp"
#include "co_context/detail/user_data.hpp"
#include "co_context/io_context.hpp"
#include "co_context/task.hpp"
#include "co_context/utility/polymorphism.hpp"
#include "co_context/utility/time_cast.hpp"
#include "uring/utility/kernel_version.hpp"
#include <cassert>
#include <chrono>
#include <coroutine>
#include <cstdint>
#include <fcntl.h>
#include <linux/openat2.h>
#include <span>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <sys/xattr.h>
#include <type_traits>

namespace co_context::detail {

class lazy_awaiter {
  protected:
    friend struct lazy_link_timeout;

    lazy_awaiter() noexcept = default;
    ~lazy_awaiter() noexcept = default;

#ifndef __INTELLISENSE__

  public:
    lazy_awaiter(const lazy_awaiter &) = delete;
    lazy_awaiter(lazy_awaiter &&) = delete;
    lazy_awaiter &operator=(const lazy_awaiter &) = delete;
    lazy_awaiter &operator=(lazy_awaiter &&) = delete;
#endif
};

template<typename T>
concept lazy_io_awaiter =
    std::derived_from<std::remove_reference_t<T>, lazy_awaiter>;

template<lazy_io_awaiter L, lazy_io_awaiter R>
inline co_context::task<int> operator&&(L &&lhs, R &&rhs) noexcept {
    co_await lhs;
    co_return co_await rhs;
}

template<lazy_io_awaiter R>
inline co_context::task<int> &&
operator&&(co_context::task<int> &&lhs, R &&rhs) noexcept {
    co_await lhs;
    co_return co_await rhs;
}

template<typename T>
class lazy_blocking_linux_awaiter
    : public lazy_awaiter
    , public CRTP<T, lazy_blocking_linux_awaiter> {
  public:
    constexpr bool /*NOLINT*/ await_ready() const noexcept { return true; }

    constexpr void
    await_suspend(std::coroutine_handle<> /*unused*/) const noexcept {}

    int /*NOLINT*/ await_resume() const noexcept {
        int result = this->self().do_syscall();
        if (result == -1) [[unlikely]] {
            result = -errno;
        }
        return result;
    }
};

template<typename T>
class lazy_blocking_posix_awaiter
    : public lazy_awaiter
    , public CRTP<T, lazy_blocking_posix_awaiter> {
  public:
    constexpr bool /*NOLINT*/ await_ready() const noexcept { return true; }

    constexpr void
    await_suspend(std::coroutine_handle<> /*unused*/) const noexcept {}

    int /*NOLINT*/ await_resume() const noexcept {
        int result = this->self().do_syscall();
        result = -result;
        return result;
    }
};

class lazy_nonblocking_awaiter : public lazy_awaiter {
  protected:
    template<typename T>
    friend struct lazy_nonblocking_linux_awaiter;

    template<typename T>
    friend struct lazy_nonblocking_posix_awaiter;

    void check_error() noexcept {
        if (result == -1) [[unlikely]] {
            result = -errno;
        }
    }

    [[nodiscard]]
    bool is_ready() const noexcept {
        return !((result == -EAGAIN) | (result == -EWOULDBLOCK));
    }

    explicit lazy_nonblocking_awaiter(int fd) noexcept : fd(fd) {}

  protected:
    int result;
    int fd;
};

class lazy_nonblocking_read_awaiter : public lazy_nonblocking_awaiter {
  public:
    void await_suspend(std::coroutine_handle<> current) const noexcept {
        register_poller(current);
    }

    explicit lazy_nonblocking_read_awaiter(int fd) noexcept
        : lazy_nonblocking_awaiter(fd) {}

  protected:
    template<typename T>
    friend struct lazy_nonblocking_linux_awaiter;

    template<typename T>
    friend struct lazy_nonblocking_posix_awaiter;

    void register_poller(std::coroutine_handle<> current) const noexcept {
        auto *worker = this_thread.worker;
        assert(worker != nullptr);
        ++worker->requests_to_reap;
        auto &poller = worker->poller;
        auto &data = poller.make_fd_data(fd);
        data.interests |= EPOLLIN | EPOLLET;
        data.in.handle = current;
        epoll::epoll_event e;
        e.events = data.interests;
        e.data.ptr = &data;
        poller.mod_or_add(fd, e);
    }

    void unregister_poller() const noexcept {
        auto &poller = this_thread.worker->poller;
        auto &data = poller.fd_data[fd];
        data.interests &= ~uint32_t(EPOLLIN);
        data.in.handle = nullptr;
    }
};

class lazy_nonblocking_write_awaiter : public lazy_nonblocking_awaiter {
  public:
    void await_suspend(std::coroutine_handle<> current) const noexcept {
        register_poller(current);
    }

    explicit lazy_nonblocking_write_awaiter(int fd) noexcept
        : lazy_nonblocking_awaiter(fd) {}

  protected:
    template<typename T>
    friend struct lazy_nonblocking_linux_awaiter;

    template<typename T>
    friend struct lazy_nonblocking_posix_awaiter;

    void register_poller(std::coroutine_handle<> current) const noexcept {
        auto *worker = this_thread.worker;
        assert(worker != nullptr);
        ++worker->requests_to_reap;
        auto &poller = worker->poller;
        auto &data = poller.make_fd_data(fd);
        data.interests |= EPOLLOUT | EPOLLET;
        data.out.handle = current;
        epoll::epoll_event e;
        e.events = data.interests;
        e.data.ptr = &data;
        poller.mod_or_add(fd, e);
    }

    void unregister_poller() const noexcept {
        auto &poller = this_thread.worker->poller;
        auto &data = poller.fd_data[fd];
        data.interests &= ~uint32_t(EPOLLOUT);
        data.out.handle = nullptr;
    }
};

template<typename T>
struct lazy_nonblocking_linux_awaiter
    : CRTP<T, lazy_nonblocking_linux_awaiter> {
    bool await_ready() noexcept {
        auto &me = this->self();
        me.result = me.do_syscall();
        me.check_error();
        return me.is_ready();
    }

    int await_resume() noexcept {
        auto &me = this->self();
        if (!me.is_ready()) {
            me.result = me.do_syscall();
            me.check_error();
            me.unregister_poller();
        }
        return me.result;
    }
};

struct lazy_splice : lazy_blocking_linux_awaiter<lazy_splice> {
    inline lazy_splice(
        int fd_in,
        int64_t off_in,
        int fd_out,
        int64_t off_out,
        unsigned int nbytes,
        unsigned int splice_flags
    ) noexcept
        : fd_in(fd_in)
        , off_in(off_in)
        , fd_out(fd_out)
        , off_out(off_out)
        , nbytes(nbytes)
        , splice_flags(splice_flags) {}

    [[nodiscard]]
    int do_syscall() const noexcept {
        auto off_in = this->off_in;
        auto off_out = this->off_out;
        // check off_in/off_out == -1
        return ::splice(
            fd_in, off_in != -1 ? &off_in : nullptr, fd_out,
            off_out != -1 ? &off_out : nullptr, nbytes, splice_flags
        );
    }

  private:
    int fd_in;
    int64_t off_in;
    int fd_out;
    int64_t off_out;
    unsigned int nbytes;
    unsigned int splice_flags;
};

struct lazy_tee : lazy_blocking_linux_awaiter<lazy_tee> {
    inline lazy_tee(
        int fd_in, int fd_out, unsigned int nbytes, unsigned int splice_flags
    ) noexcept
        : fd_in(fd_in)
        , fd_out(fd_out)
        , nbytes(nbytes)
        , splice_flags(splice_flags) {}

    [[nodiscard]]
    int do_syscall() const noexcept {
        return ::tee(fd_in, fd_out, nbytes, splice_flags);
    }

  private:
    int fd_in;
    int fd_out;
    unsigned int nbytes;
    unsigned int splice_flags;
};

struct lazy_readv
    : lazy_nonblocking_read_awaiter
    , lazy_nonblocking_linux_awaiter<lazy_readv> {
    inline lazy_readv(
        int fd, std::span<const iovec> iovecs, uint64_t offset
    ) noexcept
        : lazy_nonblocking_read_awaiter(fd)
        , iovecs(iovecs)
        , offset(offset) {}

    [[nodiscard]]
    int do_syscall() const noexcept {
        return ::preadv(fd, iovecs.data(), iovecs.size(), offset);
    }

  private:
    std::span<const iovec> iovecs;
    uint64_t offset;
};

struct lazy_readv2
    : lazy_nonblocking_read_awaiter
    , lazy_nonblocking_linux_awaiter<lazy_readv2> {
    inline lazy_readv2(
        int fd, std::span<const iovec> iovecs, uint64_t offset, int flags
    ) noexcept
        : lazy_nonblocking_read_awaiter(fd)
        , iovecs(iovecs)
        , offset(offset)
        , flags(flags) {}

    [[nodiscard]]
    int do_syscall() const noexcept {
        return ::preadv2(fd, iovecs.data(), iovecs.size(), offset, flags);
    }

  private:
    std::span<const iovec> iovecs;
    uint64_t offset;
    int flags;
};

struct lazy_writev
    : lazy_nonblocking_write_awaiter
    , lazy_nonblocking_linux_awaiter<lazy_writev> {
    inline lazy_writev(
        int fd, std::span<const iovec> iovecs, uint64_t offset
    ) noexcept
        : lazy_nonblocking_write_awaiter(fd)
        , iovecs(iovecs)
        , offset(offset) {}

    [[nodiscard]]
    int do_syscall() const noexcept {
        return ::pwritev(fd, iovecs.data(), iovecs.size(), offset);
    }

  private:
    std::span<const iovec> iovecs;
    uint64_t offset;
};

struct lazy_writev2
    : lazy_nonblocking_write_awaiter
    , lazy_nonblocking_linux_awaiter<lazy_writev2> {
    inline lazy_writev2(
        int fd, std::span<const iovec> iovecs, uint64_t offset, int flags
    ) noexcept
        : lazy_nonblocking_write_awaiter(fd)
        , iovecs(iovecs)
        , offset(offset)
        , flags(flags) {}

    [[nodiscard]]
    int do_syscall() const noexcept {
        return ::pwritev2(fd, iovecs.data(), iovecs.size(), offset, flags);
    }

  private:
    std::span<const iovec> iovecs;
    uint64_t offset;
    int flags;
};

struct lazy_recvmsg
    : lazy_nonblocking_read_awaiter
    , lazy_nonblocking_linux_awaiter<lazy_recvmsg> {
    inline lazy_recvmsg(int fd, msghdr *msg, unsigned flags) noexcept
        : lazy_nonblocking_read_awaiter(fd)
        , msg(msg)
        , flags(flags) {}

    [[nodiscard]]
    int do_syscall() const noexcept {
        return ::recvmsg(fd, msg, flags);
    }

  private:
    msghdr *msg;
    unsigned flags;
};

struct lazy_sendmsg
    : lazy_nonblocking_write_awaiter
    , lazy_nonblocking_linux_awaiter<lazy_sendmsg> {
    inline lazy_sendmsg(int fd, const msghdr *msg, unsigned int flags) noexcept
        : lazy_nonblocking_write_awaiter(fd)
        , msg(msg)
        , flags(flags) {}

    [[nodiscard]]
    int do_syscall() const noexcept {
        return ::sendmsg(fd, msg, flags);
    }

  private:
    const msghdr *msg;
    unsigned flags;
};

struct lazy_fsync : lazy_blocking_linux_awaiter<lazy_fsync> {
    inline lazy_fsync(int fd, uint32_t fsync_flags) noexcept
        : fd(fd)
        , fsync_flags(fsync_flags) {}

    [[nodiscard]]
    int do_syscall() const noexcept {
        assert(fsync_flags == 0 || fsync_flags == IORING_FSYNC_DATASYNC);
        if (fsync_flags == 0) {
            return ::fsync(fd);
        } else {
            return ::fdatasync(fd);
        }
    }

  private:
    int fd;
    uint32_t fsync_flags;
};

struct lazy_timeout_timespec : lazy_awaiter {
  public:
    inline lazy_timeout_timespec(
        const __kernel_timespec &ts, unsigned int count, unsigned int flags
    ) noexcept {
        assert(false && "TODO");
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

inline constexpr uint32_t pure_timer_flag = IORING_TIMEOUT_ETIME_SUCCESS;

inline constexpr uint32_t timeout_relative_flag = 0
#if LIBURINGCXX_IS_KERNEL_REACH(5, 15)
                                                  | IORING_TIMEOUT_BOOTTIME
#endif
    ;

inline constexpr uint32_t timeout_absolute_steady_flag = IORING_TIMEOUT_ABS;

inline constexpr uint32_t timeout_absolute_realtime_flag =
    IORING_TIMEOUT_ABS | IORING_TIMEOUT_REALTIME;

struct lazy_timeout : lazy_timeout_base {
    template<class Rep, class Period>
    inline explicit lazy_timeout(std::chrono::duration<Rep, Period> duration
    ) noexcept
        : lazy_timeout_base(duration) {
        // sqe->prep_timeout(ts, 0, timeout_relative_flag | pure_timer_flag);
        assert(false && "TODO");
    }

    template<class Duration>
    inline explicit lazy_timeout(
        std::chrono::time_point<std::chrono::steady_clock, Duration> time_point
    ) noexcept
        : lazy_timeout_base(time_point) {
        // sqe->prep_timeout(
        //     ts, 0, timeout_absolute_steady_flag | pure_timer_flag
        // );
        assert(false && "TODO");
    }

    template<class Duration>
    inline explicit lazy_timeout(
        std::chrono::time_point<std::chrono::system_clock, Duration> time_point
    ) noexcept
        : lazy_timeout_base(time_point) {
        // sqe->prep_timeout(
        //     ts, 0, timeout_absolute_realtime_flag | pure_timer_flag
        // );
        assert(false && "TODO");
    }
};

struct lazy_timeout_remove : lazy_awaiter {
    inline lazy_timeout_remove(uint64_t user_data, unsigned flags) noexcept {
        // sqe->prep_timeout_remove(user_data, flags);
        assert(false && "TODO");
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
        // sqe->prep_timeout_update(ts, user_data, flags);
        assert(false && "TODO");
    }
};

struct lazy_accept
    : lazy_nonblocking_read_awaiter
    , lazy_nonblocking_linux_awaiter<lazy_accept> {
    inline lazy_accept(
        int fd, sockaddr *addr, socklen_t *addrlen, int flags
    ) noexcept
        : lazy_nonblocking_read_awaiter(fd)
        , addr(addr)
        , addrlen(addrlen)
        , flags(flags) {}

    [[nodiscard]]
    int do_syscall() const noexcept {
        return ::accept4(fd, addr, addrlen, flags | SOCK_NONBLOCK);
    }

  private:
    sockaddr *addr;
    socklen_t *addrlen;
    int flags;
};

struct lazy_cancel_fd : lazy_awaiter {
    inline lazy_cancel_fd(int fd, unsigned int flags) noexcept {
        // sqe->prep_cancle_fd(fd, flags);
        assert(false && "TODO");
    }
};

struct lazy_link_timeout_base : lazy_timeout_base {
    template<class Expire>
    // Should not be used directly
    inline lazy_link_timeout_base(Expire expire, unsigned int flags) noexcept
        : lazy_timeout_base(expire) {
        assert(false && "TODO");
    }
};

struct lazy_link_timeout : lazy_link_timeout_base {
  private:
    void arrange_io(lazy_awaiter &&timed_io) noexcept {
        assert(false && "TODO");
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

struct lazy_connect
    : lazy_nonblocking_read_awaiter
    , lazy_nonblocking_linux_awaiter<lazy_connect> {
    inline lazy_connect(
        int sockfd, const sockaddr *addr, socklen_t addrlen
    ) noexcept
        : lazy_nonblocking_read_awaiter(sockfd)
        , addr(addr)
        , addrlen(addrlen) {}

    [[nodiscard]]
    int do_syscall() noexcept {
        return ::connect(fd, addr, addrlen);
    }

  private:
    const sockaddr *addr;
    socklen_t addrlen;
};

struct lazy_fallocate : lazy_blocking_linux_awaiter<lazy_fallocate> {
    inline lazy_fallocate(int fd, int mode, off_t offset, off_t len) noexcept
        : fd(fd)
        , mode(mode)
        , offset(offset)
        , len(len) {}

    [[nodiscard]]
    int do_syscall() const noexcept {
        return ::fallocate(fd, mode, offset, len);
    }

  private:
    int fd;
    int mode;
    off_t offset;
    off_t len;
};

struct lazy_openat : lazy_blocking_linux_awaiter<lazy_openat> {
    inline lazy_openat(
        int dfd, const char *path, int flags, mode_t mode
    ) noexcept
        : dfd(dfd)
        , path(path)
        , flags(flags)
        , mode(mode) {}

    [[nodiscard]]
    int do_syscall() const noexcept {
        return ::openat(dfd, path, flags, mode);
    }

  private:
    int dfd;
    const char *path;
    int flags;
    mode_t mode;
};

struct lazy_close : lazy_blocking_linux_awaiter<lazy_close> {
    inline explicit lazy_close(int fd) noexcept : fd(fd) {}

    [[nodiscard]]
    int do_syscall() const noexcept {
        return ::close(fd);
    }

  private:
    int fd;
};

struct lazy_read
    : lazy_nonblocking_read_awaiter
    , lazy_nonblocking_linux_awaiter<lazy_read> {
    inline lazy_read(int fd, std::span<char> buf, uint64_t offset) noexcept
        : lazy_nonblocking_read_awaiter(fd)
        , buf(buf)
        , offset(offset) {}

    [[nodiscard]]
    int do_syscall() const noexcept {
        if (offset == -1ULL) {
            return ::read(fd, buf.data(), buf.size());
        } else {
            return ::pread(fd, buf.data(), buf.size(), offset);
        }
    }

  private:
    std::span<char> buf;
    uint64_t offset;
};

struct lazy_write
    : lazy_nonblocking_write_awaiter
    , lazy_nonblocking_linux_awaiter<lazy_write> {
    inline lazy_write(
        int fd, std::span<const char> buf, uint64_t offset
    ) noexcept
        : lazy_nonblocking_write_awaiter(fd)
        , buf(buf)
        , offset(offset) {}

    [[nodiscard]]
    int do_syscall() const noexcept {
        if (offset == -1ULL) {
            return ::write(fd, buf.data(), buf.size());
        } else {
            return ::pwrite(fd, buf.data(), buf.size(), offset);
        }
    }

  private:
    std::span<const char> buf;
    uint64_t offset;
};

struct lazy_statx : lazy_blocking_linux_awaiter<lazy_statx> {
    inline lazy_statx(
        int dfd,
        const char *path,
        int flags,
        unsigned int mask,
        struct statx *statxbuf
    ) noexcept {}

    [[nodiscard]]
    int do_syscall() const noexcept {
        return ::statx(dfd, path, flags, mask, statxbuf);
    }

  private:
    int dfd;
    const char *path;
    int flags;
    unsigned int mask;
    struct statx *statxbuf;
};

struct lazy_fadvise : lazy_blocking_posix_awaiter<lazy_fadvise> {
    inline lazy_fadvise(int fd, uint64_t offset, off_t len, int advice) noexcept
        : fd(fd)
        , offset(offset)
        , len(len)
        , advice(advice) {}

    [[nodiscard]]
    int do_syscall() const noexcept {
        return ::posix_fadvise(fd, offset, len, advice);
    }

  private:
    int fd;
    uint64_t offset;
    off_t len;
    int advice;
};

struct lazy_madvise : lazy_blocking_linux_awaiter<lazy_madvise> {
    inline lazy_madvise(void *addr, off_t length, int advice) noexcept
        : addr(addr)
        , length(length)
        , advice(advice) {}

    [[nodiscard]]
    int do_syscall() const noexcept {
        return ::madvise(addr, length, advice);
    }

  private:
    void *addr;
    off_t length;
    int advice;
};

struct lazy_send
    : lazy_nonblocking_write_awaiter
    , lazy_nonblocking_linux_awaiter<lazy_send> {
    inline lazy_send(int sockfd, std::span<const char> buf, int flags) noexcept
        : lazy_nonblocking_write_awaiter(sockfd)
        , buf(buf)
        , flags(flags) {}

    [[nodiscard]]
    int do_syscall() const noexcept {
        return ::send(fd, buf.data(), buf.size(), flags);
    }

  private:
    std::span<const char> buf;
    int flags;
};

// TODO deal with prep_send_set_addr()

struct lazy_recv
    : lazy_nonblocking_read_awaiter
    , lazy_nonblocking_linux_awaiter<lazy_recv> {
    inline lazy_recv(int sockfd, std::span<char> buf, int flags) noexcept
        : lazy_nonblocking_read_awaiter(sockfd)
        , buf(buf)
        , flags(flags) {}

    [[nodiscard]]
    int do_syscall() noexcept {
        return ::recv(fd, buf.data(), buf.size(), flags);
    }

  private:
    std::span<char> buf;
    int flags;
};

struct lazy_openat2 : lazy_blocking_linux_awaiter<lazy_openat2> {
    inline lazy_openat2(int dfd, const char *path, open_how *how) noexcept
        : dfd(dfd)
        , path(path)
        , how(how) {}

    [[nodiscard]]
    int do_syscall() const noexcept {
        return ::syscall(SYS_openat2, dfd, path, how, sizeof(struct open_how));
    }

  private:
    int dfd;
    const char *path;
    open_how *how;
};

struct lazy_epoll_ctl : lazy_awaiter {
    inline lazy_epoll_ctl(int epfd, int fd, int op, epoll_event *ev) noexcept
        : epfd(epfd)
        , fd(fd)
        , op(op)
        , ev(ev) {}

    [[nodiscard]]
    int do_syscall() const noexcept {
        return ::epoll_ctl(epfd, op, fd, ev);
    }

  private:
    int epfd;
    int fd;
    int op;
    epoll_event *ev;
};

struct lazy_shutdown : lazy_blocking_linux_awaiter<lazy_shutdown> {
    inline lazy_shutdown(int fd, int how) noexcept : fd(fd), how(how) {}

    [[nodiscard]]
    int do_syscall() const noexcept {
        return ::shutdown(fd, how);
    }

  private:
    int fd;
    int how;
};

struct lazy_unlinkat : lazy_blocking_linux_awaiter<lazy_unlinkat> {
    inline lazy_unlinkat(int dfd, const char *path, int flags) noexcept
        : dfd(dfd)
        , path(path)
        , flags(flags) {}

    [[nodiscard]]
    int do_syscall() const noexcept {
        return ::unlinkat(dfd, path, flags);
    }

  private:
    int dfd;
    const char *path;
    int flags;
};

struct lazy_unlink : lazy_blocking_linux_awaiter<lazy_unlink> {
    inline lazy_unlink(const char *path, int flags) noexcept : path(path) {
        assert(flags == 0 && "lazy_unlink: do not support flags");
    }

    [[nodiscard]]
    int do_syscall() const noexcept {
        return ::unlink(path);
    }

  private:
    const char *path;
};

struct lazy_renameat : lazy_blocking_linux_awaiter<lazy_renameat> {
    inline lazy_renameat(
        int olddfd,
        const char *oldpath,
        int newdfd,
        const char *newpath,
        int flags
    ) noexcept
        : olddfd(olddfd)
        , oldpath(oldpath)
        , newdfd(newdfd)
        , newpath(newpath)
        , flags(flags) {}

    [[nodiscard]]
    int do_syscall() const noexcept {
        return ::renameat2(olddfd, oldpath, newdfd, newpath, flags);
    }

  private:
    int olddfd;
    const char *oldpath;
    int newdfd;
    const char *newpath;
    int flags;
};

struct lazy_rename : lazy_blocking_linux_awaiter<lazy_rename> {
    inline lazy_rename(const char *oldpath, const char *newpath) noexcept
        : oldpath(oldpath)
        , newpath(newpath) {}

    [[nodiscard]]
    int do_syscall() const noexcept {
        return ::rename(oldpath, newpath);
    }

  private:
    const char *oldpath;
    const char *newpath;
};

struct lazy_sync_file_range
    : lazy_blocking_linux_awaiter<lazy_sync_file_range> {
    inline lazy_sync_file_range(
        int fd, uint32_t len, uint64_t offset, int flags
    ) noexcept
        : fd(fd)
        , len(len)
        , offset(offset)
        , flags(flags) {}

    [[nodiscard]]
    int do_syscall() const noexcept {
        return ::sync_file_range(fd, offset, len, flags);
    }

  private:
    int fd;
    uint32_t len;
    uint64_t offset;
    int flags;
};

struct lazy_mkdirat : lazy_blocking_linux_awaiter<lazy_mkdirat> {
    inline lazy_mkdirat(int dfd, const char *path, mode_t mode) noexcept
        : dfd(dfd)
        , path(path)
        , mode(mode) {}

    [[nodiscard]]
    int do_syscall() const noexcept {
        return ::mkdirat(dfd, path, mode);
    }

  private:
    int dfd;
    const char *path;
    mode_t mode;
};

struct lazy_mkdir : lazy_blocking_linux_awaiter<lazy_mkdir> {
    inline lazy_mkdir(const char *path, mode_t mode) noexcept
        : path(path)
        , mode(mode) {}

    [[nodiscard]]
    int do_syscall() const noexcept {
        return ::mkdir(path, mode);
    }

  private:
    const char *path;
    mode_t mode;
};

struct lazy_symlinkat : lazy_blocking_linux_awaiter<lazy_symlinkat> {
    inline lazy_symlinkat(
        const char *target, int newdirfd, const char *linkpath
    ) noexcept
        : target(target)
        , newdirfd(newdirfd)
        , linkpath(linkpath) {}

    [[nodiscard]]
    int do_syscall() const noexcept {
        return ::symlinkat(target, newdirfd, linkpath);
    }

  private:
    const char *target;
    int newdirfd;
    const char *linkpath;
};

struct lazy_symlink : lazy_blocking_linux_awaiter<lazy_symlink> {
    inline lazy_symlink(const char *target, const char *linkpath) noexcept
        : target(target)
        , linkpath(linkpath) {}

    [[nodiscard]]
    int do_syscall() const noexcept {
        return ::symlink(target, linkpath);
    }

  private:
    const char *target;
    const char *linkpath;
};

struct lazy_linkat : lazy_blocking_linux_awaiter<lazy_linkat> {
    inline lazy_linkat(
        int olddfd,
        const char *oldpath,
        int newdfd,
        const char *newpath,
        int flags
    ) noexcept
        : olddfd(olddfd)
        , oldpath(oldpath)
        , newdfd(newdfd)
        , newpath(newpath)
        , flags(flags) {}

    [[nodiscard]]
    int do_syscall() const noexcept {
        return ::linkat(olddfd, oldpath, newdfd, newpath, flags);
    }

  private:
    int olddfd;
    const char *oldpath;
    int newdfd;
    const char *newpath;
    int flags;
};

struct lazy_link : lazy_blocking_linux_awaiter<lazy_link> {
    inline lazy_link(
        const char *oldpath, const char *newpath, [[maybe_unused]] int flags
    ) noexcept
        : oldpath(oldpath)
        , newpath(newpath) {
        assert(flags == 0 && "lazy_link: do not support flags");
    }

    [[nodiscard]]
    int do_syscall() const noexcept {
        return ::link(oldpath, newpath);
    }

  private:
    const char *oldpath;
    const char *newpath;
    // int flags;
};

struct lazy_getxattr : lazy_blocking_linux_awaiter<lazy_getxattr> {
    inline lazy_getxattr(
        const char *name, char *value, const char *path, size_t len
    ) noexcept
        : name(name)
        , value(value)
        , path(path)
        , len(len) {}

    [[nodiscard]]
    int do_syscall() const noexcept {
        return ::getxattr(path, name, value, len);
    }

  private:
    const char *name;
    char *value;
    const char *path;
    size_t len;
};

struct lazy_setxattr : lazy_blocking_linux_awaiter<lazy_setxattr> {
    inline lazy_setxattr(
        const char *name, char *value, const char *path, int flags, size_t len
    ) noexcept
        : name(name)
        , value(value)
        , path(path)
        , flags(flags)
        , len(len) {}

    [[nodiscard]]
    int do_syscall() const noexcept {
        return ::setxattr(path, name, value, len, flags);
    }

  private:
    const char *name;
    char *value;
    const char *path;
    int flags;
    size_t len;
};

struct lazy_fgetxattr : lazy_blocking_linux_awaiter<lazy_fgetxattr> {
    inline lazy_fgetxattr(
        int fd, const char *name, char *value, size_t len
    ) noexcept
        : fd(fd)
        , name(name)
        , value(value)
        , len(len) {}

    [[nodiscard]]
    int do_syscall() const noexcept {
        return ::fgetxattr(fd, name, value, len);
    }

  private:
    int fd;
    const char *name;
    char *value;
    size_t len;
};

struct lazy_fsetxattr : lazy_blocking_linux_awaiter<lazy_fsetxattr> {
    inline lazy_fsetxattr(
        int fd, const char *name, const char *value, int flags, size_t len
    ) noexcept
        : fd(fd)
        , name(name)
        , value(value)
        , flags(flags)
        , len(len) {}

    [[nodiscard]]
    int do_syscall() const noexcept {
        return ::fsetxattr(fd, name, value, len, flags);
    }

  private:
    int fd;
    const char *name;
    const char *value;
    int flags;
    size_t len;
};

struct lazy_socket : lazy_blocking_linux_awaiter<lazy_socket> {
    inline lazy_socket(
        int domain, int type, int protocol, [[maybe_unused]] unsigned int flags
    ) noexcept
        : domain(domain)
        , type(type)
        , protocol(protocol) {
        assert(flags == 0 && "lazy_socket: flags are currently unused.");
    }

    [[nodiscard]]
    int do_syscall() const noexcept {
        return ::socket(domain, type, protocol);
    }

  private:
    int domain;
    int type;
    int protocol;
    // unsigned int flags;
};

} // namespace co_context::detail
