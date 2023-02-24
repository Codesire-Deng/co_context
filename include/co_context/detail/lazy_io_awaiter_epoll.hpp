#pragma once

#include "co_context/detail/epoll.hpp"
#include "co_context/detail/thread_meta.hpp"
#include "co_context/detail/user_data.hpp"
#include "co_context/io_context.hpp"
#include "co_context/task.hpp"
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
  public:
    constexpr bool /*NOLINT*/ await_ready() const noexcept { return true; }

    constexpr void
    await_suspend(std::coroutine_handle<> /*unused*/) const noexcept {}

    int /*NOLINT*/ await_resume() const noexcept { return result; }

  protected:
    friend struct lazy_link_timeout;
    int result;

    lazy_awaiter() noexcept = default;
    ~lazy_awaiter() noexcept = default;

    void check_error() noexcept {
        if (result == -1) [[unlikely]] {
            result = -errno;
        }
    }

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

class lazy_nonblocking_awaiter : public lazy_awaiter {
  protected:
    [[nodiscard]]
    bool is_ready() const noexcept {
        return !((result == -EAGAIN) | (result == -EWOULDBLOCK));
    }

    explicit lazy_nonblocking_awaiter(int fd) noexcept : fd(fd) {}

  protected:
    int fd;
};

class lazy_read_awaiter : public lazy_nonblocking_awaiter {
  public:
    void await_suspend(std::coroutine_handle<> current) const noexcept {
        register_poller(current);
    }

    explicit lazy_read_awaiter(int fd) noexcept
        : lazy_nonblocking_awaiter(fd) {}

  protected:
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
        data.in.handle = nullptr;
    }
};

class lazy_write_awaiter : public lazy_nonblocking_awaiter {
  public:
    void await_suspend(std::coroutine_handle<> current) const noexcept {
        register_poller(current);
    }

    explicit lazy_write_awaiter(int fd) noexcept
        : lazy_nonblocking_awaiter(fd) {}

  protected:
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
        data.out.handle = nullptr;
    }
};

struct lazy_splice : lazy_awaiter {
    inline lazy_splice(
        int fd_in,
        int64_t off_in,
        int fd_out,
        int64_t off_out,
        unsigned int nbytes,
        unsigned int splice_flags
    ) noexcept {
        result =
            ::splice(fd_in, &off_in, fd_out, &off_out, nbytes, splice_flags);
        check_error();
    }
};

struct lazy_tee : lazy_awaiter {
    inline lazy_tee(
        int fd_in, int fd_out, unsigned int nbytes, unsigned int splice_flags
    ) noexcept {
        result = ::tee(fd_in, fd_out, nbytes, splice_flags);
        check_error();
    }
};

struct lazy_readv : lazy_awaiter {
    inline lazy_readv(
        int fd, std::span<const iovec> iovecs, uint64_t offset
    ) noexcept {
        result = ::preadv(fd, iovecs.data(), iovecs.size(), offset);
        check_error();
    }
};

struct lazy_readv2 : lazy_awaiter {
    inline lazy_readv2(
        int fd, std::span<const iovec> iovecs, uint64_t offset, int flags
    ) noexcept {
        result = ::preadv2(fd, iovecs.data(), iovecs.size(), offset, flags);
        check_error();
    }
};

struct lazy_writev : lazy_awaiter {
    inline lazy_writev(
        int fd, std::span<const iovec> iovecs, uint64_t offset
    ) noexcept {
        result = ::pwritev(fd, iovecs.data(), iovecs.size(), offset);
        check_error();
    }
};

struct lazy_writev2 : lazy_awaiter {
    inline lazy_writev2(
        int fd, std::span<const iovec> iovecs, uint64_t offset, int flags
    ) noexcept {
        result = ::pwritev2(fd, iovecs.data(), iovecs.size(), offset, flags);
        check_error();
    }
};

struct lazy_recvmsg : lazy_read_awaiter {
    inline lazy_recvmsg(int fd, msghdr *msg, unsigned flags) noexcept
        : lazy_read_awaiter(fd)
        , msg(msg)
        , flags(flags) {}

    bool await_ready() noexcept {
        result = ::recvmsg(fd, msg, flags);
        check_error();
        return is_ready();
    }

    int await_resume() noexcept {
        if (!is_ready()) {
            result = ::recvmsg(fd, msg, flags);
            check_error();
            unregister_poller();
        }
        return result;
    }

  private:
    msghdr *msg;
    unsigned flags;
};

struct lazy_sendmsg : lazy_write_awaiter {
    inline lazy_sendmsg(int fd, const msghdr *msg, unsigned int flags) noexcept
        : lazy_write_awaiter(fd)
        , msg(msg)
        , flags(flags) {}

    bool await_ready() noexcept {
        result = ::sendmsg(fd, msg, flags);
        check_error();
        return is_ready();
    }

    int await_resume() noexcept {
        if (!is_ready()) {
            result = ::sendmsg(fd, msg, flags);
            check_error();
            unregister_poller();
        }
        return result;
    }

  private:
    const msghdr *msg;
    unsigned flags;
};

struct lazy_fsync : lazy_awaiter {
    inline lazy_fsync(int fd, uint32_t fsync_flags) noexcept {
        assert(fsync_flags == 0 || fsync_flags == IORING_FSYNC_DATASYNC);
        if (fsync_flags == 0) {
            result = ::fsync(fd);
        } else {
            result = ::fdatasync(fd);
        }
        check_error();
    }
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

struct lazy_accept : lazy_read_awaiter {
    inline lazy_accept(
        int fd, sockaddr *addr, socklen_t *addrlen, int flags
    ) noexcept
        : lazy_read_awaiter(fd)
        , addr(addr)
        , addrlen(addrlen)
        , flags(flags) {}

    bool await_ready() noexcept {
        result = ::accept4(fd, addr, addrlen, flags | SOCK_NONBLOCK);
        check_error();
        return is_ready();
    }

    int await_resume() noexcept {
        if (!is_ready()) {
            result = ::accept4(fd, addr, addrlen, flags | SOCK_NONBLOCK);
            check_error();
            unregister_poller();
        }
        return result;
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

struct lazy_connect : lazy_read_awaiter {
    inline lazy_connect(
        int sockfd, const sockaddr *addr, socklen_t addrlen
    ) noexcept
        : lazy_read_awaiter(sockfd)
        , addr(addr)
        , addrlen(addrlen) {}

    bool await_ready() noexcept {
        result = ::connect(fd, addr, addrlen);
        check_error();
        return is_ready();
    }

    int await_resume() noexcept {
        if (!is_ready()) {
            result = ::connect(fd, addr, addrlen);
            check_error();
            unregister_poller();
        }
        return result;
    }

  private:
    const sockaddr *addr;
    socklen_t addrlen;
};

struct lazy_fallocate : lazy_awaiter {
    inline lazy_fallocate(int fd, int mode, off_t offset, off_t len) noexcept {
        result = ::fallocate(fd, mode, offset, len);
        check_error();
    }
};

struct lazy_openat : lazy_awaiter {
    inline lazy_openat(
        int dfd, const char *path, int flags, mode_t mode
    ) noexcept {
        result = ::openat(dfd, path, flags, mode);
        check_error();
    }
};

struct lazy_close : lazy_awaiter {
    inline explicit lazy_close(int fd) noexcept { result = ::close(fd); }
};

struct lazy_read : lazy_read_awaiter {
    inline lazy_read(int fd, std::span<char> buf, uint64_t offset) noexcept
        : lazy_read_awaiter(fd)
        , buf(buf)
        , offset(offset) {}

    bool await_ready() noexcept {
        if (offset == -1ULL) {
            result = ::read(fd, buf.data(), buf.size());
        } else {
            result = ::pread(fd, buf.data(), buf.size(), offset);
        }
        check_error();
        return is_ready();
    }

    int await_resume() noexcept {
        if (!is_ready()) {
            if (offset == -1ULL) {
                result = ::read(fd, buf.data(), buf.size());
            } else {
                result = ::pread(fd, buf.data(), buf.size(), offset);
            }
            check_error();
            unregister_poller();
        }
        return result;
    }

  private:
    std::span<char> buf;
    uint64_t offset;
};

struct lazy_write : lazy_write_awaiter {
    inline lazy_write(
        int fd, std::span<const char> buf, uint64_t offset
    ) noexcept
        : lazy_write_awaiter(fd)
        , buf(buf)
        , offset(offset) {}

    bool await_ready() noexcept {
        if (offset == -1ULL) {
            result = ::write(fd, buf.data(), buf.size());
        } else {
            result = ::pwrite(fd, buf.data(), buf.size(), offset);
        }
        check_error();
        return is_ready();
    }

    int await_resume() noexcept {
        if (!is_ready()) {
            if (offset == -1ULL) {
                result = ::write(fd, buf.data(), buf.size());
            } else {
                result = ::pwrite(fd, buf.data(), buf.size(), offset);
            }
            check_error();
            unregister_poller();
        }
        return result;
    }

  private:
    std::span<const char> buf;
    uint64_t offset;
};

struct lazy_statx : lazy_awaiter {
    inline lazy_statx(
        int dfd,
        const char *path,
        int flags,
        unsigned int mask,
        struct statx *statxbuf
    ) noexcept {
        result = ::statx(dfd, path, flags, mask, statxbuf);
        check_error();
    }
};

struct lazy_fadvise : lazy_awaiter {
    inline lazy_fadvise(
        int fd, uint64_t offset, off_t len, int advice
    ) noexcept {
        result = -::posix_fadvise(fd, offset, len, advice);
        // do not check_error();
    }
};

struct lazy_madvise : lazy_awaiter {
    inline lazy_madvise(void *addr, off_t length, int advice) noexcept {
        result = ::madvise(addr, length, advice);
        check_error();
    }
};

struct lazy_send : lazy_write_awaiter {
    inline lazy_send(int sockfd, std::span<const char> buf, int flags) noexcept
        : lazy_write_awaiter(sockfd)
        , buf(buf)
        , flags(flags) {}

    bool await_ready() noexcept {
        result = ::send(fd, buf.data(), buf.size(), flags);
        check_error();
        return is_ready();
    }

    int await_resume() noexcept {
        if (!is_ready()) {
            result = ::send(fd, buf.data(), buf.size(), flags);
            check_error();
            unregister_poller();
        }
        return result;
    }

  private:
    std::span<const char> buf;
    int flags;
};

// TODO deal with prep_send_set_addr()

struct lazy_recv : lazy_read_awaiter {
    inline lazy_recv(int sockfd, std::span<char> buf, int flags) noexcept
        : lazy_read_awaiter(sockfd)
        , buf(buf)
        , flags(flags) {}

    bool await_ready() noexcept {
        result = ::recv(fd, buf.data(), buf.size(), flags);
        check_error();
        return is_ready();
    }

    int await_resume() noexcept {
        if (!is_ready()) {
            result = ::recv(fd, buf.data(), buf.size(), flags);
            check_error();
            unregister_poller();
        }
        return result;
    }

  private:
    std::span<char> buf;
    int flags;
};

struct lazy_openat2 : lazy_awaiter {
    inline lazy_openat2(int dfd, const char *path, open_how *how) noexcept {
        result =
            ::syscall(SYS_openat2, dfd, path, how, sizeof(struct open_how));
        check_error();
    }
};

struct lazy_epoll_ctl : lazy_awaiter {
    inline lazy_epoll_ctl(int epfd, int fd, int op, epoll_event *ev) noexcept {
        result = ::epoll_ctl(epfd, op, fd, ev);
        check_error();
    }
};

struct lazy_shutdown : lazy_awaiter {
    inline lazy_shutdown(int fd, int how) noexcept {
        result = ::shutdown(fd, how);
        check_error();
    }
};

struct lazy_unlinkat : lazy_awaiter {
    inline lazy_unlinkat(int dfd, const char *path, int flags) noexcept {
        result = ::unlinkat(dfd, path, flags);
        check_error();
    }
};

struct lazy_unlink : lazy_awaiter {
    inline lazy_unlink(const char *path, int flags) noexcept {
        assert(flags == 0 && "lazy_unlink: do not support flags");
        result = ::unlink(path);
        check_error();
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
        result = ::renameat2(olddfd, oldpath, newdfd, newpath, flags);
        check_error();
    }
};

struct lazy_rename : lazy_awaiter {
    inline lazy_rename(const char *oldpath, const char *newpath) noexcept {
        result = ::rename(oldpath, newpath);
        check_error();
    }
};

struct lazy_sync_file_range : lazy_awaiter {
    inline lazy_sync_file_range(
        int fd, uint32_t len, uint64_t offset, int flags
    ) noexcept {
        result = ::sync_file_range(fd, offset, len, flags);
        check_error();
    }
};

struct lazy_mkdirat : lazy_awaiter {
    inline lazy_mkdirat(int dfd, const char *path, mode_t mode) noexcept {
        result = ::mkdirat(dfd, path, mode);
        check_error();
    }
};

struct lazy_mkdir : lazy_awaiter {
    inline lazy_mkdir(const char *path, mode_t mode) noexcept {
        result = ::mkdir(path, mode);
        check_error();
    }
};

struct lazy_symlinkat : lazy_awaiter {
    inline lazy_symlinkat(
        const char *target, int newdirfd, const char *linkpath
    ) noexcept {
        result = ::symlinkat(target, newdirfd, linkpath);
        check_error();
    }
};

struct lazy_symlink : lazy_awaiter {
    inline lazy_symlink(const char *target, const char *linkpath) noexcept {
        result = ::symlink(target, linkpath);
        check_error();
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
        result = ::linkat(olddfd, oldpath, newdfd, newpath, flags);
        check_error();
    }
};

struct lazy_link : lazy_awaiter {
    inline lazy_link(
        const char *oldpath, const char *newpath, int flags
    ) noexcept {
        assert(flags == 0 && "lazy_link: do not support flags");
        result = ::link(oldpath, newpath);
        check_error();
    }
};

struct lazy_getxattr : lazy_awaiter {
    inline lazy_getxattr(
        const char *name, char *value, const char *path, size_t len
    ) noexcept {
        result = ::getxattr(path, name, value, len);
        check_error();
    }
};

struct lazy_setxattr : lazy_awaiter {
    inline lazy_setxattr(
        const char *name, char *value, const char *path, int flags, size_t len
    ) noexcept {
        result = ::setxattr(path, name, value, len, flags);
        check_error();
    }
};

struct lazy_fgetxattr : lazy_awaiter {
    inline lazy_fgetxattr(
        int fd, const char *name, char *value, size_t len
    ) noexcept {
        result = ::fgetxattr(fd, name, value, len);
        check_error();
    }
};

struct lazy_fsetxattr : lazy_awaiter {
    inline lazy_fsetxattr(
        int fd, const char *name, const char *value, int flags, size_t len
    ) noexcept {
        result = ::fsetxattr(fd, name, value, len, flags);
        check_error();
    }
};

struct lazy_socket : lazy_awaiter {
    inline lazy_socket(
        int domain, int type, int protocol, unsigned int flags
    ) noexcept {
        assert(flags == 0 && "lazy_socket: flags are currently unused.");
        result = ::socket(domain, type, protocol);
        check_error();
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

    void await_suspend(std::coroutine_handle<> current) noexcept {
        resume_ctx.worker.co_spawn_auto(current);
    }

    constexpr void await_resume() const noexcept {}

    explicit lazy_resume_on(co_context::io_context &resume_context) noexcept
        : resume_ctx(resume_context) {}

  private:
    co_context::io_context &resume_ctx;
};

} // namespace co_context::detail
