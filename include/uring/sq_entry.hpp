#pragma once

#include "uring/io_uring.h"
#include <cstdint>
#include <cstring>
#include <span>
#include <sys/socket.h>
// #include <sys/uio.h>
#include <fcntl.h>
#include "uring/compat.h"

struct __kernel_timespec;

namespace liburingcxx {

template<unsigned uring_flags>
class uring;

class sq_entry final : private io_uring_sqe {
  public:
    template<unsigned uring_flags>
    friend class ::liburingcxx::uring;

    inline sq_entry &clone_from(const sq_entry &other) noexcept {
        std::memcpy(this, &other, sizeof(*this));
        return *this;
    }

    inline sq_entry &set_data(uint64_t data) noexcept {
        this->user_data = data;
        return *this;
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

    // see `man io_uring_enter`
    // available since Linux 5.17
    inline sq_entry &set_cqe_skip() noexcept {
        this->flags |= IOSQE_CQE_SKIP_SUCCESS;
        return *this;
    }

    inline sq_entry &set_target_fixed_file(uint32_t file_index) noexcept {
        /* 0 means no fixed files, indexes should be encoded as "index + 1" */
        this->file_index = file_index + 1;
        return *this;
    }

    inline void *get_padding() noexcept { return __pad2; }

    inline sq_entry &prepare_rw(
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

    inline sq_entry &prepare_readv(
        int fd, std::span<const iovec> iovecs, uint64_t offset
    ) noexcept {
        return prepare_rw(
            IORING_OP_READV, fd, iovecs.data(), iovecs.size(), offset
        );
    }

    inline sq_entry &prepare_readv2(
        int fd, std::span<const iovec> iovecs, __u64 offset, int flags
    ) noexcept {
        prepare_readv(fd, iovecs, offset);
        this->rw_flags = flags;
        return *this;
    }

    inline sq_entry &prepare_read_fixed(
        int fd, std::span<char> buf, uint64_t offset, uint16_t bufIndex
    ) noexcept {
        prepare_rw(IORING_OP_READ_FIXED, fd, buf.data(), buf.size(), offset);
        this->buf_index = bufIndex;
        return *this;
    }

    inline sq_entry &prepare_writev(
        int fd, std::span<const iovec> iovecs, uint64_t offset
    ) noexcept {
        return prepare_rw(
            IORING_OP_WRITEV, fd, iovecs.data(), iovecs.size(), offset
        );
    }

    inline sq_entry &prepare_writev2(
        int fd, std::span<const iovec> iovecs, uint64_t offset, int flags
    ) noexcept {
        prepare_writev(fd, iovecs, offset);
        this->rw_flags = flags;
        return *this;
    }

    inline sq_entry &prepare_write_fixed(
        int fd, std::span<const char> buf, uint64_t offset, uint16_t bufIndex
    ) noexcept {
        prepare_rw(IORING_OP_WRITE_FIXED, fd, buf.data(), buf.size(), offset);
        this->buf_index = bufIndex;
        return *this;
    }

    inline sq_entry &
    prepare_read(int fd, std::span<char> buf, uint64_t offset) noexcept {
        return prepare_rw(IORING_OP_READ, fd, buf.data(), buf.size(), offset);
    }

    inline sq_entry &
    prepare_write(int fd, std::span<const char> buf, uint64_t offset) noexcept {
        return prepare_rw(IORING_OP_WRITE, fd, buf.data(), buf.size(), offset);
    }

    inline sq_entry &
    prepare_recvmsg(int fd, msghdr *msg, unsigned flags) noexcept {
        prepare_rw(IORING_OP_RECVMSG, fd, msg, 1, 0);
        this->msg_flags = flags;
        return *this;
    }

    /**
     * @brief same as recvmsg but generate multi-CQE, see
     * `man io_uring_prep_recvmsg_multishot`
     *
     * available @since Linux 5.20
     */
    inline sq_entry &
    prepare_recvmsg_multishot(int fd, msghdr *msg, unsigned flags) noexcept {
        prepare_recvmsg(fd, msg, flags);
        this->ioprio |= IORING_RECV_MULTISHOT;
        return *this;
    }

    inline sq_entry &
    prepare_sendmsg(int fd, const msghdr *msg, unsigned flags) noexcept {
        prepare_rw(IORING_OP_SENDMSG, fd, msg, 1, 0);
        this->msg_flags = flags;
        return *this;
    }

    inline sq_entry &prepare_nop() noexcept {
        return prepare_rw(IORING_OP_NOP, -1, nullptr, 0, 0);
    }

    inline sq_entry &prepare_timeout(
        __kernel_timespec *ts, unsigned count, unsigned flags
    ) noexcept {
        prepare_rw(IORING_OP_TIMEOUT, -1, ts, 1, count);
        this->timeout_flags = flags;
        return *this;
    }

    inline sq_entry &
    prepare_timeout_remove(uint64_t user_data, unsigned flags) noexcept {
        prepare_rw(
            IORING_OP_TIMEOUT_REMOVE, -1, reinterpret_cast<void *>(user_data),
            0, 0
        );
        this->timeout_flags = flags;
        return *this;
    }

    inline sq_entry &prepare_timeout_rpdate(
        __kernel_timespec *ts, uint64_t user_data, unsigned flags
    ) noexcept {
        prepare_rw(
            IORING_OP_TIMEOUT_REMOVE, -1, reinterpret_cast<void *>(user_data),
            0, (uintptr_t)ts
        );
        this->timeout_flags = flags | IORING_TIMEOUT_UPDATE;
        return *this;
    }

    inline sq_entry &prepare_accept(
        int fd, sockaddr *addr, socklen_t *addrlen, int flags
    ) noexcept {
        prepare_rw(IORING_OP_ACCEPT, fd, addr, 0, (uint64_t)addrlen);
        this->accept_flags = (uint32_t)flags;
        return *this;
    }

    inline sq_entry &prepare_accept_direct(
        int fd,
        sockaddr *addr,
        socklen_t *addrlen,
        int flags,
        uint32_t file_index
    ) noexcept {
        prepare_accept(fd, addr, addrlen, flags);
        set_target_fixed_file(file_index);
        return *this;
    }

    inline sq_entry &prepare_cancle(uint64_t user_data, int flags) noexcept {
        prepare_rw(IORING_OP_ASYNC_CANCEL, -1, nullptr, 0, 0);
        this->addr = user_data;
        this->cancel_flags = (uint32_t)flags;
        return *this;
    }

    inline sq_entry &prepare_cancle_fd(int fd, unsigned int flags) noexcept {
        prepare_rw(IORING_OP_ASYNC_CANCEL, fd, nullptr, 0, 0);
        this->cancel_flags = (uint32_t)flags | IORING_ASYNC_CANCEL_FD;
        return *this;
    }

    inline sq_entry &prepare_link_timeout(
        struct __kernel_timespec *ts, unsigned flags
    ) noexcept {
        prepare_rw(IORING_OP_LINK_TIMEOUT, -1, ts, 1, 0);
        this->timeout_flags = flags;
        return *this;
    }

    inline sq_entry &
    prepare_connect(int fd, const sockaddr *addr, socklen_t addrlen) noexcept {
        return prepare_rw(IORING_OP_CONNECT, fd, addr, 0, addrlen);
    }

    inline sq_entry &prepare_close(int fd) noexcept {
        return prepare_rw(IORING_OP_CLOSE, fd, nullptr, 0, 0);
    }

    inline sq_entry &
    prepare_send(int sockfd, std::span<const char> buf, int flags) noexcept {
        prepare_rw(IORING_OP_SEND, sockfd, buf.data(), buf.size(), 0);
        this->msg_flags = (uint32_t)flags;
        return *this;
    }

    inline sq_entry &
    prepare_recv(int sockfd, std::span<char> buf, int flags) noexcept {
        prepare_rw(IORING_OP_RECV, sockfd, buf.data(), buf.size(), 0);
        this->msg_flags = (uint32_t)flags;
        return *this;
    }

    /**
     * @brief same as recv but generate multi-CQE, see
     * `man io_uring_prep_recv_multishot`
     *
     * available @since Linux 5.20
     */
    inline sq_entry &prepare_recv_multishot(
        int sockfd, std::span<char> buf, int flags
    ) noexcept {
        prepare_recv(sockfd, buf, flags);
        this->ioprio |= IORING_RECV_MULTISHOT;
        return *this;
    }

    inline sq_entry &prepare_shutdown(int fd, int how) noexcept {
        return prepare_rw(IORING_OP_SHUTDOWN, fd, nullptr, (uint32_t)how, 0);
    }

    inline sq_entry &prepare_fsync(int fd, uint32_t fsync_flags) noexcept {
        prepare_rw(IORING_OP_SYNC_FILE_RANGE, fd, nullptr, 0, 0);
        this->fsync_flags = fsync_flags;
        return *this;
    }

    inline sq_entry &prepare_syncFileRange(
        int fd, uint32_t len, uint64_t offset, int flags
    ) noexcept {
        prepare_rw(IORING_OP_SYNC_FILE_RANGE, fd, nullptr, len, offset);
        this->sync_range_flags = (uint32_t)flags;
        return *this;
    }

    inline sq_entry &
    prepare_files_update(std::span<int> fds, int offset) noexcept {
        prepare_rw(IORING_OP_FILES_UPDATE, -1, fds.data(), fds.size(), offset);
        return *this;
    }

    inline sq_entry &
    prepare_fallocate(int fd, int mode, off_t offset, off_t len) noexcept {
        prepare_rw(
            IORING_OP_FALLOCATE, fd, (const uintptr_t *)(unsigned long)len,
            (uint32_t)mode, (uint64_t)offset
        );
        return *this;
    }

    inline sq_entry &
    prepare_openat(int dfd, const char *path, int flags, mode_t mode) noexcept {
        prepare_rw(IORING_OP_OPENAT, dfd, path, mode, 0);
        this->open_flags = (uint32_t)flags;
        return *this;
    }

    /* open directly into the fixed file table */
    inline sq_entry &prepare_openat_direct(
        int dfd, const char *path, int flags, mode_t mode, unsigned file_index
    ) noexcept {
        return prepare_openat(dfd, path, flags, mode)
            .set_target_fixed_file(file_index);
    }

    inline sq_entry &
    prepare_openat2(int dfd, const char *path, struct open_how *how) noexcept {
        prepare_rw(
            IORING_OP_OPENAT2, dfd, path, sizeof(*how), (uint64_t)(uintptr_t)how
        );
        return *this;
    }

    /* open directly into the fixed file table */
    inline sq_entry &prepare_openat2_direct(
        int dfd, const char *path, struct open_how *how, unsigned file_index
    ) noexcept {
        return prepare_openat2(dfd, path, how)
            .set_target_fixed_file(file_index);
    }

    inline sq_entry &prepare_provide_buffers(
        const void *addr, int len, int nr, int bgid, int bid
    ) noexcept {
        prepare_rw(
            IORING_OP_PROVIDE_BUFFERS, nr, addr, (uint32_t)len, (uint64_t)bid
        );
        this->buf_group = (uint16_t)bgid;
        return *this;
    }

    inline sq_entry &prepare_provide_buffers(int nr, int bgid) noexcept {
        prepare_rw(IORING_OP_REMOVE_BUFFERS, nr, nullptr, 0, 0);
        this->buf_group = (uint16_t)bgid;
        return *this;
    }

    inline sq_entry &prepare_statx(
        int dfd,
        const char *path,
        int flags,
        unsigned mask,
        struct statx *statxbuf
    ) noexcept {
        prepare_rw(IORING_OP_STATX, dfd, path, mask, (uint64_t)statxbuf);
        this->statx_flags = (uint32_t)flags;
        return *this;
    }

    inline sq_entry &
    prepare_fadvise(int fd, uint64_t offset, off_t len, int advice) noexcept {
        prepare_rw(
            IORING_OP_FADVISE, fd, nullptr, (uint32_t)len, (uint64_t)offset
        );
        this->fadvise_advice = (uint32_t)advice;
        return *this;
    }

    inline sq_entry &
    prepare_madvise(void *addr, off_t length, int advice) noexcept {
        prepare_rw(IORING_OP_MADVISE, -1, addr, (uint32_t)length, 0);
        this->fadvise_advice = (uint32_t)advice;
        return *this;
    }

    inline sq_entry &
    prepare_unlinkat(int dfd, const char *path, int flags) noexcept {
        prepare_rw(IORING_OP_UNLINKAT, dfd, path, 0, 0);
        this->unlink_flags = (uint32_t)flags;
        return *this;
    }

    inline sq_entry &prepare_renameat(
        int olddfd,
        const char *oldpath,
        int newdfd,
        const char *newpath,
        int flags
    ) noexcept {
        prepare_rw(
            IORING_OP_RENAMEAT, olddfd, oldpath, (uint32_t)newdfd,
            (uint64_t)(uintptr_t)newpath
        );
        this->rename_flags = (uint32_t)flags;
        return *this;
    }

    inline sq_entry &
    prepare_mkdirat(int dfd, const char *path, mode_t mode) noexcept {
        prepare_rw(IORING_OP_MKDIRAT, dfd, path, mode, 0);
        return *this;
    }

    inline sq_entry &prepare_mkdir(const char *path, mode_t mode) noexcept {
        return prepare_mkdirat(AT_FDCWD, path, mode);
    }

    inline sq_entry &prepare_symlinkat(
        const char *target, int newdirfd, const char *linkpath
    ) noexcept {
        prepare_rw(
            IORING_OP_SYMLINKAT, newdirfd, target, 0,
            (uint64_t)(uintptr_t)linkpath
        );
        return *this;
    }

    inline sq_entry &
    prepare_symlink(const char *target, const char *linkpath) noexcept {
        return prepare_symlinkat(target, AT_FDCWD, linkpath);
    }

    inline sq_entry &prepare_linkat(
        int olddfd,
        const char *oldpath,
        int newdfd,
        const char *newpath,
        int flags
    ) noexcept {
        prepare_rw(
            IORING_OP_LINKAT, olddfd, oldpath, (uint32_t)newdfd,
            (uint64_t)(uintptr_t)newpath
        );
        this->hardlink_flags = (uint32_t)flags;
        return *this;
    }

    inline sq_entry &
    prepare_link(const char *oldpath, const char *newpath, int flags) noexcept {
        return prepare_linkat(AT_FDCWD, oldpath, AT_FDCWD, newpath, flags);
    }

    /**
     * @brief send a CQE to another ring
     *
     * available @since Linux 5.18
     */
    inline sq_entry &prepare_msg_ring(
        int fd, unsigned int cqe_res, uint64_t cqe_user_data, unsigned int flags
    ) noexcept {
        prepare_rw(IORING_OP_MSG_RING, fd, nullptr, cqe_res, cqe_user_data);
        this->rw_flags = flags;
        return *this;
    }

    /**
     * @pre Either fd_in or fd_out must be a pipe.
     * @param off_in If fd_in refers to a pipe, off_in must be (int64_t) -1;
     *               If fd_in does not refer to a pipe and off_in is (int64_t)
     * -1, then bytes are read from fd_in starting from the file offset and it
     * is adjust appropriately; If fd_in does not refer to a pipe and off_in is
     * not (int64_t) -1, then the starting offset of fd_in will be off_in.
     * @param off_out The description of off_in also applied to off_out.
     * @param splice_flags see man splice(2) for description of flags.
     *
     * This splice operation can be used to implement sendfile by splicing to an
     * intermediate pipe first, then splice to the final destination. In fact,
     * the implementation of sendfile in kernel uses splice internally.
     *
     * NOTE that even if fd_in or fd_out refers to a pipe, the splice operation
     * can still failed with EINVAL if one of the fd doesn't explicitly support
     * splice operation, e.g. reading from terminal is unsupported from
     * kernel 5.7 to 5.11. Check issue #291 for more information.
     */
    inline sq_entry &prepare_splice(
        int fd_in,
        int64_t off_in,
        int fd_out,
        int64_t off_out,
        unsigned int nbytes,
        unsigned int splice_flags
    ) noexcept {
        prepare_rw(
            IORING_OP_SPLICE, fd_out, nullptr, nbytes, (uint64_t)off_out
        );
        this->splice_off_in = (uint64_t)off_in;
        this->splice_flags = splice_flags;
        this->splice_fd_in = fd_in;
        return *this;
    }

    inline sq_entry &prepare_tee(
        int fd_in, int fd_out, unsigned int nbytes, unsigned int splice_flags
    ) noexcept {
        prepare_rw(IORING_OP_TEE, fd_out, nullptr, nbytes, 0);
        this->splice_off_in = 0;
        this->splice_flags = splice_flags;
        this->splice_fd_in = fd_in;
        return *this;
    }

    /* TODO: more prepare: epoll_ctl ...
     */
};

} // namespace liburingcxx