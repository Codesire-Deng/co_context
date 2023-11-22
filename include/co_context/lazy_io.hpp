#pragma once

#include <co_context/detail/attributes.hpp>
#include <co_context/detail/lazy_io_awaiter.hpp>

namespace co_context {

inline namespace lazy {

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
    [[CO_CONTEXT_AWAIT_HINT]]
    inline detail::lazy_splice splice(
        int fd_in,
        int64_t off_in,
        int fd_out,
        int64_t off_out,
        unsigned int nbytes,
        unsigned int splice_flags = 0
    ) noexcept {
        return detail::lazy_splice{fd_in,   off_in, fd_out,
                                   off_out, nbytes, splice_flags};
    }

    [[CO_CONTEXT_AWAIT_HINT]]
    inline detail::lazy_tee
    tee(int fd_in, int fd_out, unsigned int nbytes, unsigned int splice_flags
    ) noexcept {
        return detail::lazy_tee{fd_in, fd_out, nbytes, splice_flags};
    }

    [[CO_CONTEXT_AWAIT_HINT]]
    inline detail::lazy_readv readv(
        int fd, std::span<const iovec> iovecs, uint64_t offset = -1ULL
    ) noexcept {
        return detail::lazy_readv{fd, iovecs, offset};
    }

    [[CO_CONTEXT_AWAIT_HINT]]
    inline detail::lazy_readv2 readv2(
        int fd, std::span<const iovec> iovecs, __u64 offset, int flags
    ) noexcept {
        return detail::lazy_readv2{fd, iovecs, offset, flags};
    }

    [[CO_CONTEXT_AWAIT_HINT]]
    inline detail::lazy_read_fixed read_fixed(
        int fd, std::span<char> buf, uint64_t offset, uint16_t buf_index
    ) noexcept {
        return detail::lazy_read_fixed{fd, buf, offset, buf_index};
    }

    [[CO_CONTEXT_AWAIT_HINT]]
    inline detail::lazy_writev writev(
        int fd, std::span<const iovec> iovecs, uint64_t offset = -1ULL
    ) noexcept {
        return detail::lazy_writev{fd, iovecs, offset};
    }

    [[CO_CONTEXT_AWAIT_HINT]]
    inline detail::lazy_write_fixed write_fixed(
        int fd, std::span<const char> buf, uint64_t offset, uint16_t buf_index
    ) noexcept {
        return detail::lazy_write_fixed{fd, buf, offset, buf_index};
    }

    [[CO_CONTEXT_AWAIT_HINT]]
    inline detail::lazy_recvmsg
    recvmsg(int fd, msghdr *msg, unsigned int flags) noexcept {
        return detail::lazy_recvmsg{fd, msg, flags};
    }

#if LIBURINGCXX_IS_KERNEL_REACH(5, 20)
    [[CO_CONTEXT_AWAIT_HINT]]
    inline detail::lazy_recvmsg_multishot
    recvmsg_multishot(int fd, msghdr *msg, unsigned int flags) noexcept {
        return detail::lazy_recvmsg_multishot{fd, msg, flags};
    }
#endif

    [[CO_CONTEXT_AWAIT_HINT]]
    inline detail::lazy_sendmsg
    sendmsg(int fd, const msghdr *msg, unsigned int flags) noexcept {
        return detail::lazy_sendmsg{fd, msg, flags};
    }

    [[CO_CONTEXT_AWAIT_HINT]]
    inline detail::lazy_poll_add poll_add(int fd, unsigned poll_mask) noexcept {
        return detail::lazy_poll_add{fd, poll_mask};
    }

    [[CO_CONTEXT_AWAIT_HINT]]
    inline detail::lazy_poll_multishot
    poll_multishot(int fd, unsigned poll_mask) noexcept {
        return detail::lazy_poll_multishot{fd, poll_mask};
    }

    [[CO_CONTEXT_AWAIT_HINT]]
    inline detail::lazy_poll_remove poll_remove(uint64_t user_data) noexcept {
        return detail::lazy_poll_remove{user_data};
    }

    [[CO_CONTEXT_AWAIT_HINT]]
    inline detail::lazy_poll_update poll_update(
        uint64_t old_user_data,
        uint64_t new_user_data,
        unsigned poll_mask,
        unsigned flags
    ) noexcept {
        return detail::lazy_poll_update{
            old_user_data, new_user_data, poll_mask, flags
        };
    }

    [[CO_CONTEXT_AWAIT_HINT]]
    inline detail::lazy_fsync fsync(int fd, uint32_t fsync_flags) noexcept {
        return detail::lazy_fsync{fd, fsync_flags};
    }

    [[CO_CONTEXT_AWAIT_HINT]]
    inline detail::lazy_fsync fsync(
        int fd, uint32_t fsync_flags, uint64_t offset, uint32_t len
    ) noexcept {
        return detail::lazy_fsync{fd, fsync_flags, offset, len};
    }

    [[CO_CONTEXT_AWAIT_HINT]]
    inline detail::lazy_uring_nop uring_nop() noexcept {
        return detail::lazy_uring_nop{};
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
    [[CO_CONTEXT_AWAIT_HINT]]
    inline detail::lazy_timeout_timespec timeout(
        const __kernel_timespec &ts, unsigned int count, unsigned int flags
    ) noexcept {
        return detail::lazy_timeout_timespec{ts, count, flags};
    }

    /**
     * @brief Set duration timeout.
     *
     * @return lazy_awaiter
     */
    template<class Rep, class Period>
    [[CO_CONTEXT_AWAIT_HINT]]
    inline detail::lazy_timeout
    timeout(std::chrono::duration<Rep, Period> duration) noexcept {
        return detail::lazy_timeout{duration};
    }

    /**
     * @brief Set steady time_point timeout.
     *
     * @return lazy_awaiter
     */
    template<class Duration>
    [[CO_CONTEXT_AWAIT_HINT]]
    inline detail::lazy_timeout timeout_at(
        std::chrono::time_point<std::chrono::steady_clock, Duration> time_point
    ) noexcept {
        return detail::lazy_timeout{time_point};
    }

    /**
     * @brief Set realtime time_point timeout.
     *
     * @return lazy_awaiter
     */
    template<class Duration>
    [[CO_CONTEXT_AWAIT_HINT]]
    inline detail::lazy_timeout timeout_at(
        std::chrono::time_point<std::chrono::system_clock, Duration> time_point
    ) noexcept {
        if constexpr (!liburingcxx::is_kernel_reach(5, 15) && !std::chrono::system_clock::is_steady) {
            static_assert(
                detail::false_v<decltype(time_point)>,
                "io_uring timeouts only use the CLOCK_MONOTONIC clock source "
                "until 5.15. That means the clock must be steady. "
                "Please consider std::chrono::steady_clock instead."
            );
        }
        return detail::lazy_timeout{time_point};
    }

    template<class Rep, class Period>
    [[CO_CONTEXT_AWAIT_HINT]]
    inline detail::lazy_link_timeout timeout(
        detail::lazy_awaiter &&timed_io,
        std::chrono::duration<Rep, Period> duration
    ) noexcept {
        return detail::lazy_link_timeout{std::move(timed_io), duration};
    }

    template<class Duration>
    [[CO_CONTEXT_AWAIT_HINT]]
    inline detail::lazy_link_timeout timeout_at(
        detail::lazy_awaiter &&timed_io,
        std::chrono::time_point<std::chrono::steady_clock, Duration> time_point
    ) noexcept {
        return detail::lazy_link_timeout{std::move(timed_io), time_point};
    }

    template<class Duration>
    [[CO_CONTEXT_AWAIT_HINT]]
    inline detail::lazy_link_timeout timeout_at(
        detail::lazy_awaiter &&timed_io,
        std::chrono::time_point<std::chrono::system_clock, Duration> time_point
    ) noexcept {
        return detail::lazy_link_timeout{std::move(timed_io), time_point};
    }

    [[CO_CONTEXT_AWAIT_HINT]]
    inline detail::lazy_timeout_remove
    timeout_remove(uint64_t user_data, unsigned flags) noexcept {
        return detail::lazy_timeout_remove{user_data, flags};
    }

    template<class Rep, class Period>
    [[CO_CONTEXT_AWAIT_HINT]]
    inline detail::lazy_timeout_update timeout_update(
        std::chrono::duration<Rep, Period> duration,
        uint64_t user_data,
        unsigned flags = 0
    ) noexcept {
        return detail::lazy_timeout_update{duration, user_data, flags};
    }

    [[CO_CONTEXT_AWAIT_HINT]]
    inline detail::lazy_accept
    accept(int fd, sockaddr *addr, socklen_t *addrlen, int flags) noexcept {
        return detail::lazy_accept{fd, addr, addrlen, flags};
    }

    [[CO_CONTEXT_AWAIT_HINT]]
    inline detail::lazy_accept_direct accept_direct(
        int fd,
        sockaddr *addr,
        socklen_t *addrlen,
        int flags,
        uint32_t file_index
    ) noexcept {
        return detail::lazy_accept_direct{fd, addr, addrlen, flags, file_index};
    }

#if LIBURINGCXX_IS_KERNEL_REACH(5, 19)
    [[CO_CONTEXT_AWAIT_HINT]]
    inline detail::lazy_multishot_accept multishot_accept(
        int fd, sockaddr *addr, socklen_t *addrlen, int flags
    ) noexcept {
        return detail::lazy_multishot_accept{fd, addr, addrlen, flags};
    }
#endif

#if LIBURINGCXX_IS_KERNEL_REACH(5, 19)
    [[CO_CONTEXT_AWAIT_HINT]]
    inline detail::lazy_multishot_accept_direct multishot_accept_direct(
        int fd, sockaddr *addr, socklen_t *addrlen, int flags
    ) noexcept {
        return detail::lazy_multishot_accept_direct{fd, addr, addrlen, flags};
    }
#endif

    [[CO_CONTEXT_AWAIT_HINT]]
    inline detail::lazy_cancel
    cancel(uint64_t user_data, int flags = 0) noexcept {
        return detail::lazy_cancel{user_data, flags};
    }

    [[CO_CONTEXT_AWAIT_HINT]]
    inline detail::lazy_cancel_fd
    cancel(int fd, unsigned int flags = 0) noexcept {
        return detail::lazy_cancel_fd{fd, flags};
    }

    [[CO_CONTEXT_AWAIT_HINT]]
    inline detail::lazy_connect
    connect(int sockfd, const sockaddr *addr, socklen_t addrlen) noexcept {
        return detail::lazy_connect{sockfd, addr, addrlen};
    }

    [[CO_CONTEXT_AWAIT_HINT]]
    inline detail::lazy_files_update
    files_update(std::span<int> fds, int offset) noexcept {
        return detail::lazy_files_update{fds, offset};
    }

    [[CO_CONTEXT_AWAIT_HINT]]
    inline detail::lazy_fallocate
    fallocate(int fd, int mode, uint64_t offset, uint64_t len) noexcept {
        return detail::lazy_fallocate{fd, mode, offset, len};
    }

    [[CO_CONTEXT_AWAIT_HINT]]
    inline detail::lazy_openat
    openat(int dfd, const char *path, int flags, mode_t mode) noexcept {
        return detail::lazy_openat{dfd, path, flags, mode};
    }

    [[CO_CONTEXT_AWAIT_HINT]]
    inline detail::lazy_openat_direct openat_direct(
        int dfd,
        const char *path,
        int flags,
        mode_t mode,
        unsigned int file_index
    ) noexcept {
        return detail::lazy_openat_direct{dfd, path, flags, mode, file_index};
    }

    [[CO_CONTEXT_AWAIT_HINT]]
    inline detail::lazy_close close(int fd) noexcept {
        return detail::lazy_close{fd};
    }

    [[CO_CONTEXT_AWAIT_HINT]]
    inline detail::lazy_close_direct close_direct(unsigned file_index
    ) noexcept {
        return detail::lazy_close_direct{file_index};
    }

    [[CO_CONTEXT_AWAIT_HINT]]
    inline detail::lazy_read
    read(int fd, std::span<char> buf, uint64_t offset = -1ULL) noexcept {
        return detail::lazy_read{fd, buf, offset};
    }

    [[CO_CONTEXT_AWAIT_HINT]]
    inline detail::lazy_write
    write(int fd, std::span<const char> buf, uint64_t offset = -1ULL) noexcept {
        return detail::lazy_write{fd, buf, offset};
    }

    [[CO_CONTEXT_AWAIT_HINT]]
    inline detail::lazy_statx statx(
        int dfd,
        const char *path,
        int flags,
        unsigned int mask,
        struct statx *statxbuf
    ) noexcept {
        return detail::lazy_statx{dfd, path, flags, mask, statxbuf};
    }

    [[CO_CONTEXT_AWAIT_HINT]]
    inline detail::lazy_fadvise
    fadvise(int fd, uint64_t offset, off_t len, int advice) noexcept {
        return detail::lazy_fadvise{fd, offset, len, advice};
    }

    [[CO_CONTEXT_AWAIT_HINT]]
    inline detail::lazy_madvise
    madvise(void *addr, off_t length, int advice) noexcept {
        return detail::lazy_madvise{addr, length, advice};
    }

    [[CO_CONTEXT_AWAIT_HINT]]
    inline detail::lazy_send
    send(int sockfd, std::span<const char> buf, int flags = 0) noexcept {
        return detail::lazy_send{sockfd, buf, flags};
    }

    [[CO_CONTEXT_AWAIT_HINT]]
    inline detail::lazy_send_zc send_zc(
        int sockfd, std::span<const char> buf, int flags, unsigned zc_flags
    ) noexcept {
        return detail::lazy_send_zc{sockfd, buf, flags, zc_flags};
    }

    [[CO_CONTEXT_AWAIT_HINT]]
    inline detail::lazy_send_zc_fixed send_zc_fixed(
        int sockfd,
        std::span<const char> buf,
        int flags,
        unsigned zc_flags,
        unsigned buf_index
    ) noexcept {
        return detail::lazy_send_zc_fixed{
            sockfd, buf, flags, zc_flags, buf_index
        };
    }

    [[CO_CONTEXT_AWAIT_HINT]]
    inline detail::lazy_sendmsg_zc
    sendmsg_zc(int fd, const msghdr *msg, unsigned flags) noexcept {
        return detail::lazy_sendmsg_zc{fd, msg, flags};
    }

    [[CO_CONTEXT_AWAIT_HINT]]
    inline detail::lazy_recv
    recv(int sockfd, std::span<char> buf, int flags = 0) noexcept {
        return detail::lazy_recv{sockfd, buf, flags};
    }

#if LIBURINGCXX_IS_KERNEL_REACH(5, 20)
    [[CO_CONTEXT_AWAIT_HINT]]
    inline detail::lazy_recv_multishot
    recv_multishot(int sockfd, std::span<char> buf, int flags = 0) noexcept {
        return detail::lazy_recv_multishot{sockfd, buf, flags};
    }
#endif

#ifdef LIBURINGCXX_HAS_OPENAT2
    [[CO_CONTEXT_AWAIT_HINT]]
    inline detail::lazy_openat2
    openat2(int dfd, const char *path, open_how *how) noexcept {
        return detail::lazy_openat2{dfd, path, how};
    }
#endif

#ifdef LIBURINGCXX_HAS_OPENAT2
    [[CO_CONTEXT_AWAIT_HINT]]
    inline detail::lazy_openat2_direct openat2_direct(
        int dfd, const char *path, open_how *how, unsigned int file_index
    ) noexcept {
        return detail::lazy_openat2_direct{dfd, path, how, file_index};
    }
#endif

    [[CO_CONTEXT_AWAIT_HINT]]
    inline detail::lazy_epoll_ctl
    epoll_ctl(int epfd, int fd, int op, epoll_event *ev) noexcept {
        return detail::lazy_epoll_ctl{epfd, fd, op, ev};
    }

    [[CO_CONTEXT_AWAIT_HINT]]
    inline detail::lazy_provide_buffers provide_buffers(
        const void *addr, int len, int nr, int bgid, int bid
    ) noexcept {
        return detail::lazy_provide_buffers{addr, len, nr, bgid, bid};
    }

    [[CO_CONTEXT_AWAIT_HINT]]
    inline detail::lazy_remove_buffers
    remove_buffers(int nr, int bgid) noexcept {
        return detail::lazy_remove_buffers{nr, bgid};
    }

    [[CO_CONTEXT_AWAIT_HINT]]
    inline detail::lazy_shutdown shutdown(int fd, int how) noexcept {
        return detail::lazy_shutdown{fd, how};
    }

    [[CO_CONTEXT_AWAIT_HINT]]
    inline detail::lazy_unlinkat
    unlinkat(int dfd, const char *path, int flags) noexcept {
        return detail::lazy_unlinkat{dfd, path, flags};
    }

    [[CO_CONTEXT_AWAIT_HINT]]
    inline detail::lazy_unlink unlink(const char *path, int flags) noexcept {
        return detail::lazy_unlink{path, flags};
    }

    [[CO_CONTEXT_AWAIT_HINT]]
    inline detail::lazy_renameat renameat(
        int olddfd,
        const char *oldpath,
        int newdfd,
        const char *newpath,
        int flags
    ) noexcept {
        return detail::lazy_renameat{olddfd, oldpath, newdfd, newpath, flags};
    }

    [[CO_CONTEXT_AWAIT_HINT]]
    inline detail::lazy_rename
    rename(const char *oldpath, const char *newpath) noexcept {
        return detail::lazy_rename{oldpath, newpath};
    }

    [[CO_CONTEXT_AWAIT_HINT]]
    inline detail::lazy_sync_file_range
    sync_file_range(int fd, uint32_t len, uint64_t offset, int flags) noexcept {
        return detail::lazy_sync_file_range{fd, len, offset, flags};
    }

    [[CO_CONTEXT_AWAIT_HINT]]
    inline detail::lazy_mkdirat
    mkdirat(int dfd, const char *path, mode_t mode) noexcept {
        return detail::lazy_mkdirat{dfd, path, mode};
    }

    [[CO_CONTEXT_AWAIT_HINT]]
    inline detail::lazy_mkdir mkdir(const char *path, mode_t mode) noexcept {
        return detail::lazy_mkdir{path, mode};
    }

    [[CO_CONTEXT_AWAIT_HINT]]
    inline detail::lazy_symlinkat
    symlinkat(const char *target, int newdirfd, const char *linkpath) noexcept {
        return detail::lazy_symlinkat{target, newdirfd, linkpath};
    }

    [[CO_CONTEXT_AWAIT_HINT]]
    inline detail::lazy_symlink
    symlink(const char *target, const char *linkpath) noexcept {
        return detail::lazy_symlink{target, linkpath};
    }

    [[CO_CONTEXT_AWAIT_HINT]]
    inline detail::lazy_linkat linkat(
        int olddfd,
        const char *oldpath,
        int newdfd,
        const char *newpath,
        int flags
    ) noexcept {
        return detail::lazy_linkat{olddfd, oldpath, newdfd, newpath, flags};
    }

    [[CO_CONTEXT_AWAIT_HINT]]
    inline detail::lazy_link
    link(const char *oldpath, const char *newpath, int flags) noexcept {
        return detail::lazy_link{oldpath, newpath, flags};
    }

#if LIBURINGCXX_IS_KERNEL_REACH(5, 18)
    [[CO_CONTEXT_AWAIT_HINT]]
    inline detail::lazy_msg_ring msg_ring(
        int fd, uint32_t cqe_res, uint64_t cqe_user_data, uint32_t flags
    ) noexcept {
        return detail::lazy_msg_ring{fd, cqe_res, cqe_user_data, flags};
    }
#endif

#if LIBURINGCXX_IS_KERNEL_REACH(6, 2)
    [[CO_CONTEXT_AWAIT_HINT]]
    inline detail::lazy_msg_ring_cqe_flags msg_ring_cqe_flags(
        int fd,
        uint32_t cqe_res,
        uint64_t cqe_user_data,
        uint32_t flags,
        uint32_t cqe_flags
    ) noexcept {
        return detail::lazy_msg_ring_cqe_flags{
            fd, cqe_res, cqe_user_data, flags, cqe_flags
        };
    }
#endif

    [[CO_CONTEXT_AWAIT_HINT]]
    inline detail::lazy_getxattr getxattr(
        const char *name, char *value, const char *path, size_t len
    ) noexcept {
        return detail::lazy_getxattr{name, value, path, len};
    }

    [[CO_CONTEXT_AWAIT_HINT]]
    inline detail::lazy_setxattr setxattr(
        const char *name, char *value, const char *path, int flags, size_t len
    ) noexcept {
        return detail::lazy_setxattr{name, value, path, flags, len};
    }

    [[CO_CONTEXT_AWAIT_HINT]]
    inline detail::lazy_fgetxattr
    fgetxattr(int fd, const char *name, char *value, size_t len) noexcept {
        return detail::lazy_fgetxattr{fd, name, value, len};
    }

    [[CO_CONTEXT_AWAIT_HINT]]
    inline detail::lazy_fsetxattr fsetxattr(
        int fd, const char *name, const char *value, int flags, size_t len
    ) noexcept {
        return detail::lazy_fsetxattr{fd, name, value, flags, len};
    }

    [[CO_CONTEXT_AWAIT_HINT]]
    inline detail::lazy_socket make_socket(
        int domain, int type, int protocol, unsigned int flags
    ) noexcept {
        return detail::lazy_socket{domain, type, protocol, flags};
    }

    [[CO_CONTEXT_AWAIT_HINT]]
    inline detail::lazy_socket_direct make_socket_direct(
        int domain,
        int type,
        int protocol,
        unsigned file_index,
        unsigned int flags
    ) noexcept {
        return detail::lazy_socket_direct{
            domain, type, protocol, file_index, flags
        };
    }

    [[CO_CONTEXT_AWAIT_HINT]]
    inline detail::lazy_socket_direct_alloc make_socket_direct_alloc(
        int domain, int type, int protocol, unsigned int flags
    ) noexcept {
        return detail::lazy_socket_direct_alloc{domain, type, protocol, flags};
    }

    [[CO_CONTEXT_AWAIT_HINT]]
    inline detail::lazy_yield yield() noexcept {
        return {};
    }

    [[CO_CONTEXT_AWAIT_HINT]]
    inline detail::lazy_who_am_i who_am_i() noexcept {
        return {};
    }

    [[CO_CONTEXT_AWAIT_HINT]]
    inline detail::lazy_forget forget() noexcept {
        return {};
    }

    [[CO_CONTEXT_AWAIT_HINT]]
    inline detail::lazy_resume_on
    resume_on(co_context::io_context &resume_context) noexcept {
        return detail::lazy_resume_on{resume_context};
    }

} // namespace lazy

} // namespace co_context
