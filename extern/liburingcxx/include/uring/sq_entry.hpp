#pragma once

#include <uring/compat.hpp>
#include <uring/io_uring.h>
#include <uring/utility/io_helper.hpp>
#include <uring/utility/kernel_version.hpp>

#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <span>
#include <sys/socket.h>
#if LIBURINGCXX_HAS_OPENAT2
#include <linux/openat2.h>
#endif

struct __kernel_timespec;
struct epoll_event;
struct statx;

namespace liburingcxx {

template<uint64_t uring_flags>
class uring;

class sq_entry final : private io_uring_sqe {
  public:
    template<uint64_t uring_flags>
    friend class ::liburingcxx::uring;

    inline sq_entry &clone_from(const sq_entry &other) noexcept {
        std::memcpy(this, &other, sizeof(*this));
        return *this;
    }

    inline sq_entry &set_data(uint64_t data) noexcept {
        this->user_data = data;
        return *this;
    }

    [[nodiscard]] inline uint64_t get_data() const noexcept {
        return user_data;
    }

    inline __u64 &fetch_data() noexcept { return this->user_data; }

    inline sq_entry &reset_flags(uint8_t flags) noexcept {
        this->flags = flags;
        return *this;
    }

    // fd is an index into the files array registered
    inline sq_entry &set_fixed_file() noexcept {
        this->flags |= IOSQE_FIXED_FILE;
        return *this;
    }

    inline sq_entry &set_link() noexcept {
        this->flags |= IOSQE_IO_LINK;
        return *this;
    }

    inline sq_entry &set_hard_link() noexcept {
        this->flags |= IOSQE_IO_HARDLINK;
        return *this;
    }

    inline sq_entry &set_drain() noexcept {
        this->flags |= IOSQE_IO_DRAIN;
        return *this;
    }

    // Tell ring to do not try non-blocking IO
    inline sq_entry &set_async() noexcept {
        this->flags |= IOSQE_ASYNC;
        return *this;
    }

    inline sq_entry &set_buffer_select() noexcept {
        this->flags |= IOSQE_BUFFER_SELECT;
        return *this;
    }

#if LIBURINGCXX_IS_KERNEL_REACH(5, 17)
    // see `man io_uring_enter`
    // available since Linux 5.17
    inline sq_entry &set_cqe_skip() noexcept {
        this->flags |= IOSQE_CQE_SKIP_SUCCESS;
        return *this;
    }

    [[nodiscard]] inline bool is_cqe_skip() const noexcept {
        return (this->flags & IOSQE_CQE_SKIP_SUCCESS);
    }
#endif

  private:
    inline sq_entry &set_target_fixed_file(uint32_t file_index) noexcept {
        /* 0 means no fixed files, indexes should be encoded as "index + 1" */
        this->file_index = file_index + 1;
        return *this;
    }

    inline void *get_padding() noexcept { return __pad2; }

    inline sq_entry &prep_rw(
        uint8_t op, int fd, const void *addr, uint32_t len, uint64_t offset
    ) noexcept {
        this->opcode = op;
        this->flags = 0;
        this->ioprio = 0;
        this->fd = fd;
        this->off = offset;
        this->addr = reinterpret_cast<uint64_t>(addr);
        this->len = len;
        this->rw_flags = 0;
        this->buf_index = 0;
        this->personality = 0;
        this->file_index = 0;
        this->addr3 = 0;
        this->__pad2[0] = 0;
        return *this;
    }

  public:
    /**************************************
     *    io operations to be exported    *
     **************************************
     */

    /*
     * io_uring_prep_splice() - Either @fd_in or @fd_out must be a pipe.
     *
     * - If @fd_in refers to a pipe, @off_in is ignored and must be set to -1.
     *
     * - If @fd_in does not refer to a pipe and @off_in is -1, then @nbytes are
     * read from @fd_in starting from the file offset, which is incremented by
     * the number of bytes read.
     *
     * - If @fd_in does not refer to a pipe and @off_in is not -1, then the
     * starting offset of @fd_in will be @off_in.
     *
     * This splice operation can be used to implement sendfile by splicing to an
     * intermediate pipe first, then splice to the final destination.
     * In fact, the implementation of sendfile in kernel uses splice internally.
     *
     * NOTE that even if fd_in or fd_out refers to a pipe, the splice operation
     * can still fail with EINVAL if one of the fd doesn't explicitly support
     * splice operation, e.g. reading from terminal is unsupported from
     * kernel 5.7 to 5.11. Check issue #291 for more information.
     */
    inline sq_entry &prep_splice(
        int fd_in,
        int64_t off_in,
        int fd_out,
        int64_t off_out,
        unsigned int nbytes,
        unsigned int splice_flags
    ) noexcept {
        prep_rw(IORING_OP_SPLICE, fd_out, nullptr, nbytes, (uint64_t)off_out);
        this->splice_off_in = (uint64_t)off_in;
        this->splice_flags = splice_flags;
        this->splice_fd_in = fd_in;
        return *this;
    }

    inline sq_entry &prep_tee(
        int fd_in, int fd_out, unsigned int nbytes, unsigned int splice_flags
    ) noexcept {
        prep_rw(IORING_OP_TEE, fd_out, nullptr, nbytes, 0);
        this->splice_off_in = 0;
        this->splice_flags = splice_flags;
        this->splice_fd_in = fd_in;
        return *this;
    }

    inline sq_entry &prep_readv(
        int fd, std::span<const iovec> iovecs, uint64_t offset
    ) noexcept {
        return prep_rw(
            IORING_OP_READV, fd, iovecs.data(), iovecs.size(), offset
        );
    }

    inline sq_entry &prep_readv2(
        int fd, std::span<const iovec> iovecs, __u64 offset, int flags
    ) noexcept {
        prep_readv(fd, iovecs, offset);
        this->rw_flags = flags;
        return *this;
    }

    inline sq_entry &prep_read_fixed(
        int fd, std::span<char> buf, uint64_t offset, uint16_t buf_index
    ) noexcept {
        prep_rw(IORING_OP_READ_FIXED, fd, buf.data(), buf.size(), offset);
        this->buf_index = buf_index;
        return *this;
    }

    inline sq_entry &prep_writev(
        int fd, std::span<const iovec> iovecs, uint64_t offset
    ) noexcept {
        return prep_rw(
            IORING_OP_WRITEV, fd, iovecs.data(), iovecs.size(), offset
        );
    }

    inline sq_entry &prep_writev2(
        int fd, std::span<const iovec> iovecs, uint64_t offset, int flags
    ) noexcept {
        prep_writev(fd, iovecs, offset);
        this->rw_flags = flags;
        return *this;
    }

    inline sq_entry &prep_write_fixed(
        int fd, std::span<const char> buf, uint64_t offset, uint16_t buf_index
    ) noexcept {
        prep_rw(IORING_OP_WRITE_FIXED, fd, buf.data(), buf.size(), offset);
        this->buf_index = buf_index;
        return *this;
    }

    inline sq_entry &
    prep_recvmsg(int fd, msghdr *msg, unsigned flags) noexcept {
        prep_rw(IORING_OP_RECVMSG, fd, msg, 1, 0);
        this->msg_flags = flags;
        return *this;
    }

#if LIBURINGCXX_IS_KERNEL_REACH(5, 20)
    /**
     * @brief same as recvmsg but generate multi-CQE, see
     * `man io_uring_prep_recvmsg_multishot`
     *
     * available @since Linux 5.20
     */
    inline sq_entry &
    prep_recvmsg_multishot(int fd, msghdr *msg, unsigned flags) noexcept {
        prep_recvmsg(fd, msg, flags);
        this->ioprio |= IORING_RECV_MULTISHOT;
        return *this;
    }
#endif

    inline sq_entry &
    prep_sendmsg(int fd, const msghdr *msg, unsigned flags) noexcept {
        prep_rw(IORING_OP_SENDMSG, fd, msg, 1, 0);
        this->msg_flags = flags;
        return *this;
    }

  private:
    inline static unsigned prep_poll_mask(unsigned poll_mask) noexcept {
#if __BYTE_ORDER == __BIG_ENDIAN
        poll_mask = __swahw32(poll_mask);
#endif
        return poll_mask;
    }

  public:
    inline sq_entry &prep_poll_add(int fd, unsigned poll_mask) noexcept {
        prep_rw(IORING_OP_POLL_ADD, fd, nullptr, 0, 0);
        this->poll32_events = prep_poll_mask(poll_mask);
        return *this;
    }

    inline sq_entry &prep_poll_multishot(int fd, unsigned poll_mask) noexcept {
        prep_poll_add(fd, poll_mask);
        this->len = IORING_POLL_ADD_MULTI;
        return *this;
    }

    inline sq_entry &prep_poll_remove(uint64_t user_data) noexcept {
        prep_rw(IORING_OP_POLL_REMOVE, -1, nullptr, 0, 0);
        this->addr = user_data;
        return *this;
    }

    inline sq_entry &prep_poll_update(
        uint64_t old_user_data,
        uint64_t new_user_data,
        unsigned poll_mask,
        unsigned flags
    ) noexcept {
        prep_rw(IORING_OP_POLL_REMOVE, -1, nullptr, flags, new_user_data);
        this->addr = old_user_data;
        this->poll32_events = prep_poll_mask(poll_mask);
        return *this;
    }

    inline sq_entry &prep_fsync(int fd, uint32_t fsync_flags) noexcept {
        prep_rw(IORING_OP_FSYNC, fd, nullptr, 0, 0);
        this->fsync_flags = fsync_flags;
        return *this;
    }

    inline sq_entry &prep_fsync(
        int fd, uint32_t fsync_flags, uint64_t offset, uint32_t len
    ) noexcept {
        prep_rw(IORING_OP_FSYNC, fd, nullptr, len, offset);
        this->fsync_flags = fsync_flags;
        return *this;
    }

    inline sq_entry &prep_nop() noexcept {
        return prep_rw(IORING_OP_NOP, -1, nullptr, 0, 0);
    }

    inline sq_entry &prep_timeout(
        const __kernel_timespec &ts, unsigned count, unsigned flags
    ) noexcept {
        prep_rw(IORING_OP_TIMEOUT, -1, &ts, 1, count);
        this->timeout_flags = flags;
        return *this;
    }

    inline sq_entry &
    prep_timeout_remove(uint64_t user_data, unsigned flags) noexcept {
        prep_rw(
            IORING_OP_TIMEOUT_REMOVE, -1, reinterpret_cast<void *>(user_data),
            0, 0
        );
        this->timeout_flags = flags;
        return *this;
    }

    inline sq_entry &prep_timeout_update(
        const __kernel_timespec &ts, uint64_t user_data, unsigned flags
    ) noexcept {
        prep_rw(
            IORING_OP_TIMEOUT_REMOVE, -1, reinterpret_cast<void *>(user_data),
            0, (uintptr_t)(&ts)
        );
        this->timeout_flags = flags | IORING_TIMEOUT_UPDATE;
        return *this;
    }

    inline sq_entry &prep_accept(
        int fd, sockaddr *addr, socklen_t *addrlen, int flags
    ) noexcept {
        prep_rw(IORING_OP_ACCEPT, fd, addr, 0, (uint64_t)addrlen);
        this->accept_flags = (uint32_t)flags;
        return *this;
    }

    inline sq_entry &prep_accept_direct(
        int fd,
        sockaddr *addr,
        socklen_t *addrlen,
        int flags,
        uint32_t file_index
    ) noexcept {
        prep_accept(fd, addr, addrlen, flags);
        set_target_fixed_file(file_index);
        return *this;
    }

    inline sq_entry &prep_accept_direct_alloc(
        int fd, sockaddr *addr, socklen_t *addrlen, int flags
    ) noexcept {
        return prep_accept_direct(
            fd, addr, addrlen, flags, IORING_FILE_INDEX_ALLOC - 1
        );
    }

#if LIBURINGCXX_IS_KERNEL_REACH(5, 19)
    inline sq_entry &prep_multishot_accept(
        int fd, sockaddr *addr, socklen_t *addrlen, int flags
    ) noexcept {
        prep_accept(fd, addr, addrlen, flags);
        this->ioprio |= IORING_ACCEPT_MULTISHOT;
        return *this;
    }
#endif

#if LIBURINGCXX_IS_KERNEL_REACH(5, 19)
    inline sq_entry &prep_multishot_accept_direct(
        int fd, sockaddr *addr, socklen_t *addrlen, int flags
    ) noexcept {
        return prep_multishot_accept(fd, addr, addrlen, flags)
            .set_target_fixed_file(IORING_FILE_INDEX_ALLOC - 1);
    }
#endif

    // Same as io_uring_prep_cancel64()
    inline sq_entry &prep_cancle(uint64_t user_data, int flags) noexcept {
        prep_rw(IORING_OP_ASYNC_CANCEL, -1, nullptr, 0, 0);
        this->addr = user_data;
        this->cancel_flags = (uint32_t)flags;
        return *this;
    }

    inline sq_entry &prep_cancle(void *user_data, int flags) noexcept {
        return prep_cancle(reinterpret_cast<uint64_t>(user_data), flags);
    }

    inline sq_entry &prep_cancle_fd(int fd, unsigned int flags) noexcept {
        prep_rw(IORING_OP_ASYNC_CANCEL, fd, nullptr, 0, 0);
        this->cancel_flags = (uint32_t)flags | IORING_ASYNC_CANCEL_FD;
        return *this;
    }

    inline sq_entry &
    prep_link_timeout(const __kernel_timespec &ts, unsigned flags) noexcept {
        prep_rw(IORING_OP_LINK_TIMEOUT, -1, &ts, 1, 0);
        this->timeout_flags = flags;
        return *this;
    }

    inline sq_entry &
    prep_connect(int fd, const sockaddr *addr, socklen_t addrlen) noexcept {
        return prep_rw(IORING_OP_CONNECT, fd, addr, 0, addrlen);
    }

    inline sq_entry &
    prep_files_update(std::span<int> fds, int offset) noexcept {
        return prep_rw(
            IORING_OP_FILES_UPDATE, -1, fds.data(), fds.size(), offset
        );
    }

    inline sq_entry &
    prep_fallocate(int fd, int mode, uint64_t offset, uint64_t len) noexcept {
        return prep_rw(
            IORING_OP_FALLOCATE, fd, reinterpret_cast<void *>(uintptr_t(len)),
            (uint32_t)mode, (uint64_t)offset
        );
    }

    inline sq_entry &
    prep_openat(int dfd, const char *path, int flags, mode_t mode) noexcept {
        prep_rw(IORING_OP_OPENAT, dfd, path, mode, 0);
        this->open_flags = (uint32_t)flags;
        return *this;
    }

    /* open directly into the fixed file table */
    inline sq_entry &prep_openat_direct(
        int dfd, const char *path, int flags, mode_t mode, unsigned file_index
    ) noexcept {
        prep_openat(dfd, path, flags, mode);
        set_target_fixed_file(file_index);
        return *this;
    }

    /* open directly into the fixed file table */
    inline sq_entry &prep_openat_direct_alloc(
        int dfd, const char *path, int flags, mode_t mode
    ) noexcept {
        return prep_openat_direct(
            dfd, path, flags, mode, IORING_FILE_INDEX_ALLOC - 1
        );
    }

    inline sq_entry &prep_close(int fd) noexcept {
        return prep_rw(IORING_OP_CLOSE, fd, nullptr, 0, 0);
    }

    inline sq_entry &prep_close_direct(unsigned file_index) noexcept {
        return prep_close(0).set_target_fixed_file(file_index);
    }

    inline sq_entry &
    prep_read(int fd, std::span<char> buf, uint64_t offset) noexcept {
        return prep_rw(IORING_OP_READ, fd, buf.data(), buf.size(), offset);
    }

    inline sq_entry &
    prep_write(int fd, std::span<const char> buf, uint64_t offset) noexcept {
        return prep_rw(IORING_OP_WRITE, fd, buf.data(), buf.size(), offset);
    }

    inline sq_entry &prep_statx(
        int dfd,
        const char *path,
        int flags,
        unsigned mask,
        struct statx *statxbuf
    ) noexcept {
        prep_rw(IORING_OP_STATX, dfd, path, mask, (uint64_t)statxbuf);
        this->statx_flags = (uint32_t)flags;
        return *this;
    }

    inline sq_entry &
    prep_fadvise(int fd, uint64_t offset, off_t len, int advice) noexcept {
        prep_rw(
            IORING_OP_FADVISE, fd, nullptr, (uint32_t)len, (uint64_t)offset
        );
        this->fadvise_advice = (uint32_t)advice;
        return *this;
    }

    inline sq_entry &
    prep_madvise(void *addr, off_t length, int advice) noexcept {
        prep_rw(IORING_OP_MADVISE, -1, addr, (uint32_t)length, 0);
        this->fadvise_advice = (uint32_t)advice;
        return *this;
    }

    inline sq_entry &
    prep_send(int sockfd, std::span<const char> buf, int flags) noexcept {
        prep_rw(IORING_OP_SEND, sockfd, buf.data(), buf.size(), 0);
        this->msg_flags = (uint32_t)flags;
        return *this;
    }

    inline sq_entry &prep_send_zc(
        int sockfd, std::span<const char> buf, int flags, unsigned zc_flags
    ) noexcept {
        prep_rw(IORING_OP_SEND_ZC, sockfd, buf.data(), buf.size(), 0);
        this->msg_flags = (uint32_t)flags;
        this->ioprio = zc_flags;
        return *this;
    }

    inline sq_entry &prep_send_zc_fixed(
        int sockfd,
        std::span<const char> buf,
        int flags,
        unsigned zc_flags,
        unsigned buf_index
    ) noexcept {
        prep_send_zc(sockfd, buf, flags, zc_flags);
        this->ioprio |= IORING_RECVSEND_FIXED_BUF;
        this->buf_index |= buf_index;
        return *this;
    }

    inline sq_entry &
    prep_sendmsg_zc(int fd, const msghdr *msg, unsigned flags) noexcept {
        prep_sendmsg(fd, msg, flags);
        this->opcode = IORING_OP_SENDMSG_ZC;
        return *this;
    }

    inline sq_entry &
    prep_send_set_addr(const sockaddr *dest_addr, uint16_t addr_len) {
        this->addr2 = reinterpret_cast<uint64_t>(dest_addr);
        this->addr_len = addr_len;
        return *this;
    }

    inline sq_entry &
    prep_recv(int sockfd, std::span<char> buf, int flags) noexcept {
        prep_rw(IORING_OP_RECV, sockfd, buf.data(), buf.size(), 0);
        this->msg_flags = (uint32_t)flags;
        return *this;
    }

#if LIBURINGCXX_IS_KERNEL_REACH(5, 20)
    /**
     * @brief same as recv but generate multi-CQE, see
     * `man io_uring_prep_recv_multishot`
     *
     * available @since Linux 5.20
     */
    inline sq_entry &
    prep_recv_multishot(int sockfd, std::span<char> buf, int flags) noexcept {
        prep_recv(sockfd, buf, flags);
        this->ioprio |= IORING_RECV_MULTISHOT;
        return *this;
    }
#endif

#ifdef LIBURINGCXX_HAS_OPENAT2
    inline sq_entry &
    prep_openat2(int dfd, const char *path, struct open_how *how) noexcept {
        prep_rw(
            IORING_OP_OPENAT2, dfd, path, sizeof(*how), (uint64_t)(uintptr_t)how
        );
        return *this;
    }
#endif

#ifdef LIBURINGCXX_HAS_OPENAT2
    /* open directly into the fixed file table */
    inline sq_entry &prep_openat2_direct(
        int dfd, const char *path, struct open_how *how, unsigned file_index
    ) noexcept {
        prep_openat2(dfd, path, how);
        set_target_fixed_file(file_index);
        return *this;
    }
#endif

#ifdef LIBURINGCXX_HAS_OPENAT2
    /* open directly into the fixed file table */
    inline sq_entry &prep_openat2_direct_alloc(
        int dfd, const char *path, struct open_how *how
    ) noexcept {
        return prep_openat2_direct(dfd, path, how, IORING_FILE_INDEX_ALLOC - 1);
    }
#endif

    inline sq_entry &
    prep_epoll_ctl(int epfd, int fd, int op, epoll_event *ev) noexcept {
        return prep_rw(
            IORING_OP_EPOLL_CTL, epfd, ev, (uint32_t)op, (uint32_t)fd
        );
    }

    inline sq_entry &prep_provide_buffers(
        const void *addr, int len, int nr, int bgid, int bid
    ) noexcept {
        prep_rw(
            IORING_OP_PROVIDE_BUFFERS, nr, addr, (uint32_t)len, (uint64_t)bid
        );
        this->buf_group = (uint16_t)bgid;
        return *this;
    }

    inline sq_entry &prep_remove_buffers(int nr, int bgid) noexcept {
        prep_rw(IORING_OP_REMOVE_BUFFERS, nr, nullptr, 0, 0);
        this->buf_group = (uint16_t)bgid;
        return *this;
    }

    inline sq_entry &prep_shutdown(int fd, int how) noexcept {
        return prep_rw(IORING_OP_SHUTDOWN, fd, nullptr, (uint32_t)how, 0);
    }

    inline sq_entry &
    prep_unlinkat(int dfd, const char *path, int flags) noexcept {
        prep_rw(IORING_OP_UNLINKAT, dfd, path, 0, 0);
        this->unlink_flags = (uint32_t)flags;
        return *this;
    }

    inline sq_entry &prep_unlink(const char *path, int flags) noexcept {
        return prep_unlinkat(AT_FDCWD, path, flags);
    }

    inline sq_entry &prep_renameat(
        int olddfd,
        const char *oldpath,
        int newdfd,
        const char *newpath,
        int flags
    ) noexcept {
        prep_rw(
            IORING_OP_RENAMEAT, olddfd, oldpath, (uint32_t)newdfd,
            (uint64_t)(uintptr_t)newpath
        );
        this->rename_flags = (uint32_t)flags;
        return *this;
    }

    inline sq_entry &
    prep_rename(const char *oldpath, const char *newpath) noexcept {
        return prep_renameat(AT_FDCWD, oldpath, AT_FDCWD, newpath, 0);
    }

    inline sq_entry &prep_sync_file_range(
        int fd, uint32_t len, uint64_t offset, int flags
    ) noexcept {
        prep_rw(IORING_OP_SYNC_FILE_RANGE, fd, nullptr, len, offset);
        this->sync_range_flags = (uint32_t)flags;
        return *this;
    }

    inline sq_entry &
    prep_mkdirat(int dfd, const char *path, mode_t mode) noexcept {
        return prep_rw(IORING_OP_MKDIRAT, dfd, path, mode, 0);
    }

    inline sq_entry &prep_mkdir(const char *path, mode_t mode) noexcept {
        return prep_mkdirat(AT_FDCWD, path, mode);
    }

    inline sq_entry &prep_symlinkat(
        const char *target, int newdirfd, const char *linkpath
    ) noexcept {
        return prep_rw(
            IORING_OP_SYMLINKAT, newdirfd, target, 0,
            (uint64_t)(uintptr_t)linkpath
        );
    }

    inline sq_entry &
    prep_symlink(const char *target, const char *linkpath) noexcept {
        return prep_symlinkat(target, AT_FDCWD, linkpath);
    }

    inline sq_entry &prep_linkat(
        int olddfd,
        const char *oldpath,
        int newdfd,
        const char *newpath,
        int flags
    ) noexcept {
        prep_rw(
            IORING_OP_LINKAT, olddfd, oldpath, (uint32_t)newdfd,
            (uint64_t)(uintptr_t)newpath
        );
        this->hardlink_flags = (uint32_t)flags;
        return *this;
    }

    inline sq_entry &
    prep_link(const char *oldpath, const char *newpath, int flags) noexcept {
        return prep_linkat(AT_FDCWD, oldpath, AT_FDCWD, newpath, flags);
    }

#if LIBURINGCXX_IS_KERNEL_REACH(5, 18)
    /**
     * @brief send a CQE to another ring
     *
     * available @since Linux 5.18
     */
    inline sq_entry &prep_msg_ring(
        int fd, uint32_t cqe_res, uint64_t cqe_user_data, uint32_t flags
    ) noexcept {
        prep_rw(IORING_OP_MSG_RING, fd, nullptr, cqe_res, cqe_user_data);
        this->msg_ring_flags = flags;
        return *this;
    }
#endif

// NOTE unconfirmed
#if LIBURINGCXX_IS_KERNEL_REACH(6, 2)
    inline sq_entry &prep_msg_ring_cqe_flags(
        int fd,
        uint32_t cqe_res,
        uint64_t cqe_user_data,
        uint32_t flags,
        uint32_t cqe_flags
    ) noexcept {
        prep_rw(IORING_OP_MSG_RING, fd, nullptr, cqe_res, cqe_user_data);
        this->msg_ring_flags = IORING_MSG_RING_FLAGS_PASS | flags;
        this->file_index = cqe_flags;
        return *this;
    }
#endif

// NOTE unconfirmed
#if LIBURINGCXX_IS_KERNEL_REACH(6, 3)
    inline sq_entry &prep_msg_ring_fd(
        int fd, int source_fd, int target_fd, uint64_t data, uint32_t flags
    ) {
        prep_rw(
            IORING_OP_MSG_RING, fd, (void *)(uintptr_t)IORING_MSG_SEND_FD, 0,
            data
        );
        msg_ring_flags = flags;
        set_target_fixed_file(target_fd);
        addr3 = source_fd;
        return *this;
    }

    inline sq_entry &prep_msg_ring_fd_alloc(
        int fd, int source_fd, uint64_t data, uint32_t flags
    ) {
        return prep_msg_ring_fd(
            fd, source_fd, int(IORING_FILE_INDEX_ALLOC - 1), data, flags
        );
    }
#endif

    inline sq_entry &prep_getxattr(
        const char *name, char *value, const char *path, unsigned int len
    ) noexcept {
        prep_rw(
            IORING_OP_GETXATTR, 0, name, len,
            (uint64_t) reinterpret_cast<uintptr_t>(value)
        );
        this->addr3 = (uint64_t) reinterpret_cast<uintptr_t>(path);
        this->xattr_flags = 0;
        return *this;
    }

    inline sq_entry &prep_setxattr(
        const char *name,
        char *value,
        const char *path,
        int flags,
        unsigned int len
    ) noexcept {
        prep_rw(
            IORING_OP_SETXATTR, 0, name, len,
            (uint64_t) reinterpret_cast<uintptr_t>(value)
        );
        this->addr3 = (uint64_t) reinterpret_cast<uintptr_t>(path);
        this->xattr_flags = flags;
        return *this;
    }

    inline sq_entry &prep_fgetxattr(
        int fd, const char *name, char *value, unsigned int len
    ) noexcept {
        prep_rw(
            IORING_OP_FGETXATTR, fd, name, len,
            (uint64_t) reinterpret_cast<uintptr_t>(value)
        );
        this->xattr_flags = 0;
        return *this;
    }

    inline sq_entry &prep_fsetxattr(
        int fd, const char *name, const char *value, int flags, unsigned int len
    ) noexcept {
        prep_rw(
            IORING_OP_FSETXATTR, fd, name, len,
            (uint64_t) reinterpret_cast<uintptr_t>(value)
        );
        this->xattr_flags = flags;
        return *this;
    }

    inline sq_entry &
    prep_socket(int domain, int type, int protocol, unsigned int flags) {
        prep_rw(IORING_OP_SOCKET, domain, nullptr, protocol, type);
        this->rw_flags = static_cast<int>(flags);
        return *this;
    }

    inline sq_entry &prep_socket_direct(
        int domain,
        int type,
        int protocol,
        unsigned file_index,
        unsigned int flags
    ) {
        prep_rw(IORING_OP_SOCKET, domain, nullptr, protocol, type);
        this->rw_flags = static_cast<int>(flags);
        set_target_fixed_file(file_index);
        return *this;
    }

    inline sq_entry &prep_socket_direct_alloc(
        int domain, int type, int protocol, unsigned int flags
    ) {
        return prep_socket_direct(
            domain, type, protocol, IORING_FILE_INDEX_ALLOC - 1, flags
        );
    }
};

} // namespace liburingcxx
