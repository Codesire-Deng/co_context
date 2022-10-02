#pragma once

#include "uring/uring.hpp"
#include "co_context/detail/task_info.hpp"
#include "co_context/detail/thread_meta.hpp"
#include "co_context/detail/worker_meta.hpp"
#include "co_context/utility/as_atomic.hpp"
#include "co_context/detail/eager_io_state.hpp"
// #include "co_context/detail/sqe_task_meta.hpp" // deprecated
#include <span>
#include <memory>

namespace co_context {

namespace detail {

    class eager_awaiter {
      public:
        inline bool is_ready() const noexcept {
            return as_atomic(shared_io_info->eager_io_state)
                       .load(std::memory_order_acquire)
                   & eager::io_ready;
        }

        bool await_ready() const noexcept { return is_ready(); }

        // std::coroutine_handle<>
        bool await_suspend(std::coroutine_handle<> current) noexcept {
            using namespace ::co_context::eager;
            shared_io_info->handle = current;
            const io_state_t old_state =
                as_atomic(shared_io_info->eager_io_state)
                    .fetch_or(io_wait, std::memory_order_acq_rel);
            assert(
                !(old_state & io_detached) && "logic error: wait after detached"
            );
            if (old_state & io_ready) {
                return false; // do not suspend, worker will delete task_info
            } else {
                return true; // suspend, worker will delete task_info
            }
        }

        int32_t await_resume() noexcept {
            int32_t result = shared_io_info->result;
            delete shared_io_info;
            return result;
        }

        void detach() noexcept {
            using namespace ::co_context::eager;
            const io_state_t old_state =
                as_atomic(shared_io_info->eager_io_state)
                    .fetch_or(io_detached, std::memory_order_relaxed);
            assert(
                !(old_state & io_wait) && "logic error: detach after waited"
            );
            if (old_state & io_ready) {
                delete shared_io_info;
            }
        }

      protected:
        liburingcxx::sq_entry *sqe;
        task_info *shared_io_info;

        // PERF memory allocation
        eager_awaiter()
            : shared_io_info(new task_info{task_info::task_type::eager_sqe}) {
            shared_io_info->eager_io_state = 0; // instead of tid_hint
            sqe = this_thread.worker->get_free_sqe();
            sqe->set_data(shared_io_info->as_eager_user_data());
        }

        inline void submit() const noexcept {
            worker_meta *const worker = detail::this_thread.worker;
            worker->submit_sqe();
            /*
            worker->try_clear_submit_overflow_buf();
            */
        }

        eager_awaiter(const eager_awaiter &other) noexcept
            : sqe(other.sqe), shared_io_info(other.shared_io_info) {}

        eager_awaiter(eager_awaiter &&) = delete;

        eager_awaiter &operator=(const eager_awaiter &other) noexcept {
            sqe = other.sqe;
            shared_io_info = other.shared_io_info;
            return *this;
        }

        eager_awaiter &operator=(eager_awaiter &&) = delete;

        ~eager_awaiter() noexcept {
            using namespace ::co_context::eager;
            assert(
                (shared_io_info->eager_io_state & (io_detached | io_wait))
                && "eager_io has neither been detached nor waited!"
            );
        };
    };

    struct eager_read : eager_awaiter {
        inline eager_read(
            int fd, std::span<char> buf, uint64_t offset
        ) noexcept {
            sqe->prep_read(fd, buf, offset);
            submit();
        }
    };

    struct eager_readv : eager_awaiter {
        inline eager_readv(
            int fd, std::span<const iovec> iovecs, uint64_t offset
        ) noexcept {
            sqe->prep_readv(fd, iovecs, offset);
            submit();
        }
    };

    struct eager_read_fixed : eager_awaiter {
        inline eager_read_fixed(
            int fd, std::span<char> buf, uint64_t offset, uint16_t bufIndex
        ) noexcept {
            sqe->prep_read_fixed(fd, buf, offset, bufIndex);
            submit();
        }
    };

    struct eager_write : eager_awaiter {
        inline eager_write(
            int fd, std::span<const char> buf, uint64_t offset
        ) noexcept {
            sqe->prep_write(fd, buf, offset);
            submit();
        }
    };

    struct eager_writev : eager_awaiter {
        inline eager_writev(
            int fd, std::span<const iovec> iovecs, uint64_t offset
        ) noexcept {
            sqe->prep_writev(fd, iovecs, offset);
            submit();
        }
    };

    struct eager_write_fixed : eager_awaiter {
        inline eager_write_fixed(
            int fd,
            std::span<const char> buf,
            uint64_t offset,
            uint16_t bufIndex
        ) noexcept {
            sqe->prep_write_fixed(fd, buf, offset, bufIndex);
            submit();
        }
    };

    struct eager_accept : eager_awaiter {
        inline eager_accept(
            int fd, sockaddr *addr, socklen_t *addrlen, int flags
        ) noexcept {
            sqe->prep_accept(fd, addr, addrlen, flags);
            submit();
        }
    };

    struct eager_accept_direct : eager_awaiter {
        inline eager_accept_direct(
            int fd,
            sockaddr *addr,
            socklen_t *addrlen,
            int flags,
            uint32_t file_index
        ) noexcept {
            sqe->prep_accept_direct(fd, addr, addrlen, flags, file_index);
            submit();
        }
    };

    struct eager_recv : eager_awaiter {
        inline eager_recv(int sockfd, std::span<char> buf, int flags) noexcept {
            sqe->prep_recv(sockfd, buf, flags);
            submit();
        }
    };

    struct eager_recvmsg : eager_awaiter {
        inline eager_recvmsg(int fd, msghdr *msg, unsigned int flags) noexcept {
            sqe->prep_recvmsg(fd, msg, flags);
            submit();
        }
    };

    struct eager_send : eager_awaiter {
        inline eager_send(
            int sockfd, std::span<const char> buf, int flags
        ) noexcept {
            sqe->prep_send(sockfd, buf, flags);
            submit();
        }
    };

    struct eager_sendmsg : eager_awaiter {
        inline eager_sendmsg(
            int fd, const msghdr *msg, unsigned int flags
        ) noexcept {
            sqe->prep_sendmsg(fd, msg, flags);
            submit();
        }
    };

    struct eager_connect : eager_awaiter {
        inline eager_connect(
            int sockfd, const sockaddr *addr, socklen_t addrlen
        ) noexcept {
            sqe->prep_connect(sockfd, addr, addrlen);
            submit();
        }
    };

    struct eager_close : eager_awaiter {
        inline eager_close(int fd) noexcept {
            sqe->prep_close(fd);
            submit();
        }
    };

    struct eager_shutdown : eager_awaiter {
        [[nodiscard("Did you forget to co_await?"
        )]] inline eager_shutdown(int fd, int how) noexcept {
            sqe->prep_shutdown(fd, how);
            submit();
        }
    };

    struct eager_fsync : eager_awaiter {
        [[nodiscard("Did you forget to co_await?"
        )]] inline eager_fsync(int fd, uint32_t fsync_flags) noexcept {
            sqe->prep_fsync(fd, fsync_flags);
            submit();
        }
    };

    struct eager_sync_file_range : eager_awaiter {
        inline eager_sync_file_range(
            int fd, uint32_t len, uint64_t offset, int flags
        ) noexcept {
            sqe->prep_sync_file_range(fd, len, offset, flags);
            submit();
        }
    };

    struct eager_uring_nop : eager_awaiter {
        [[nodiscard("Did you forget to co_await?"
        )]] inline eager_uring_nop() noexcept {
            sqe->prep_nop();
            submit();
        }
    };

    struct eager_files_update : eager_awaiter {
        [[nodiscard("Did you forget to co_await?"
        )]] inline eager_files_update(std::span<int> fds, int offset) noexcept {
            sqe->prep_files_update(fds, offset);
            submit();
        }
    };

    struct eager_fallocate : eager_awaiter {
        inline eager_fallocate(
            int fd, int mode, off_t offset, off_t len
        ) noexcept {
            sqe->prep_fallocate(fd, mode, offset, len);
            submit();
        }
    };

    struct eager_openat : eager_awaiter {
        inline eager_openat(
            int dfd, const char *path, int flags, mode_t mode
        ) noexcept {
            sqe->prep_openat(dfd, path, flags, mode);
            submit();
        }
    };

    /* open directly into the fixed file table */
    struct eager_openat_direct : eager_awaiter {
        inline eager_openat_direct(
            int dfd,
            const char *path,
            int flags,
            mode_t mode,
            unsigned file_index
        ) noexcept {
            sqe->prep_openat_direct(dfd, path, flags, mode, file_index);
            submit();
        }
    };

    struct eager_openat2 : eager_awaiter {
        inline eager_openat2(
            int dfd, const char *path, open_how *how
        ) noexcept {
            sqe->prep_openat2(dfd, path, how);
            submit();
        }
    };

    /* open directly into the fixed file table */
    struct eager_openat2_direct : eager_awaiter {
        inline eager_openat2_direct(
            int dfd, const char *path, open_how *how, unsigned int file_index
        ) noexcept {
            sqe->prep_openat2_direct(dfd, path, how, file_index);
            submit();
        }
    };

    struct eager_statx : eager_awaiter {
        inline eager_statx(
            int dfd,
            const char *path,
            int flags,
            unsigned int mask,
            struct statx *statxbuf
        ) noexcept {
            sqe->prep_statx(dfd, path, flags, mask, statxbuf);
            submit();
        }
    };

    struct eager_unlinkat : eager_awaiter {
        inline eager_unlinkat(int dfd, const char *path, int flags) noexcept {
            sqe->prep_unlinkat(dfd, path, flags);
            submit();
        }
    };

    struct eager_renameat : eager_awaiter {
        inline eager_renameat(
            int olddfd,
            const char *oldpath,
            int newdfd,
            const char *newpath,
            int flags
        ) noexcept {
            sqe->prep_renameat(olddfd, oldpath, newdfd, newpath, flags);
            submit();
        }
    };

    struct eager_mkdirat : eager_awaiter {
        inline eager_mkdirat(int dfd, const char *path, mode_t mode) noexcept {
            sqe->prep_mkdirat(dfd, path, mode);
            submit();
        }
    };

    struct eager_symlinkat : eager_awaiter {
        inline eager_symlinkat(
            const char *target, int newdirfd, const char *linkpath
        ) noexcept {
            sqe->prep_symlinkat(target, newdirfd, linkpath);
            submit();
        }
    };

    struct eager_linkat : eager_awaiter {
        inline eager_linkat(
            int olddfd,
            const char *oldpath,
            int newdfd,
            const char *newpath,
            int flags
        ) noexcept {
            sqe->prep_linkat(olddfd, oldpath, newdfd, newpath, flags);
            submit();
        }
    };

    struct eager_timeout_timespec : eager_awaiter {
        inline eager_timeout_timespec(
            const __kernel_timespec &ts, unsigned int count, unsigned int flags
        ) noexcept {
            sqe->prep_timeout(ts, count, flags);
            submit();
        }
    };

    struct eager_timeout : eager_awaiter {
        __kernel_timespec ts;

        template<class Rep, class Period = std::ratio<1>>
        inline eager_timeout(
            std::chrono::duration<Rep, Period> duration, unsigned int flags
        ) noexcept {
            using namespace std;
            using namespace std::literals;
            ts.tv_sec = duration / 1s;
            duration -= chrono::seconds(ts.tv_sec);
            ts.tv_nsec =
                duration_cast<chrono::duration<long long, std::nano>>(duration)
                    .count();
            sqe->prep_timeout(ts, 0, flags);
            submit();
        }
    };

    struct eager_splice : eager_awaiter {
        inline eager_splice(
            int fd_in,
            int64_t off_in,
            int fd_out,
            int64_t off_out,
            unsigned int nbytes,
            unsigned int splice_flags
        ) noexcept {
            sqe->prep_splice(
                fd_in, off_in, fd_out, off_out, nbytes, splice_flags
            );
            submit();
        }
    };

    struct eager_tee : eager_awaiter {
        inline eager_tee(
            int fd_in,
            int fd_out,
            unsigned int nbytes,
            unsigned int splice_flags
        ) noexcept {
            sqe->prep_tee(fd_in, fd_out, nbytes, splice_flags);
            submit();
        }
    };

} // namespace detail

namespace eager {

    constexpr auto nop() noexcept {
        return std::suspend_never{};
    }

    inline detail::eager_read
    read(int fd, std::span<char> buf, uint64_t offset) noexcept {
        return detail::eager_read{fd, buf, offset};
    }

    inline detail::eager_readv
    readv(int fd, std::span<const iovec> iovecs, uint64_t offset) noexcept {
        return detail::eager_readv{fd, iovecs, offset};
    }

    inline detail::eager_read_fixed read_fixed(
        int fd, std::span<char> buf, uint64_t offset, uint16_t bufIndex
    ) noexcept {
        return detail::eager_read_fixed{fd, buf, offset, bufIndex};
    }

    inline detail::eager_write
    write(int fd, std::span<const char> buf, uint64_t offset) noexcept {
        return detail::eager_write{fd, buf, offset};
    }

    inline detail::eager_writev
    writev(int fd, std::span<const iovec> iovecs, uint64_t offset) noexcept {
        return detail::eager_writev{fd, iovecs, offset};
    }

    inline detail::eager_write_fixed write_fixed(
        int fd, std::span<const char> buf, uint64_t offset, uint16_t bufIndex
    ) noexcept {
        return detail::eager_write_fixed{fd, buf, offset, bufIndex};
    }

    inline detail::eager_accept
    accept(int fd, sockaddr *addr, socklen_t *addrlen, int flags) noexcept {
        return detail::eager_accept{fd, addr, addrlen, flags};
    }

    inline detail::eager_accept_direct accept_direct(
        int fd,
        sockaddr *addr,
        socklen_t *addrlen,
        int flags,
        uint32_t file_index
    ) noexcept {
        return detail::eager_accept_direct{
            fd, addr, addrlen, flags, file_index};
    }

    inline detail::eager_recv
    recv(int sockfd, std::span<char> buf, int flags = 0) noexcept {
        return detail::eager_recv{sockfd, buf, flags};
    }

    inline detail::eager_recvmsg
    recvmsg(int fd, msghdr *msg, unsigned int flags) noexcept {
        return detail::eager_recvmsg{fd, msg, flags};
    }

    inline detail::eager_send
    send(int sockfd, std::span<const char> buf, int flags = 0) noexcept {
        return detail::eager_send{sockfd, buf, flags};
    }

    inline detail::eager_sendmsg
    sendmsg(int fd, const msghdr *msg, unsigned int flags) noexcept {
        return detail::eager_sendmsg{fd, msg, flags};
    }

    inline detail::eager_connect
    connect(int sockfd, const sockaddr *addr, socklen_t addrlen) noexcept {
        return detail::eager_connect{sockfd, addr, addrlen};
    }

    inline detail::eager_close close(int fd) noexcept {
        return detail::eager_close{fd};
    }

    inline detail::eager_shutdown shutdown(int fd, int how) noexcept {
        return detail::eager_shutdown{fd, how};
    }

    inline detail::eager_fsync fsync(int fd, uint32_t fsync_flags) noexcept {
        return detail::eager_fsync{fd, fsync_flags};
    }

    inline detail::eager_sync_file_range
    sync_file_range(int fd, uint32_t len, uint64_t offset, int flags) noexcept {
        return detail::eager_sync_file_range{fd, len, offset, flags};
    }

    inline detail::eager_uring_nop uring_nop() noexcept {
        return detail::eager_uring_nop{};
    }

    inline detail::eager_files_update
    files_update(std::span<int> fds, int offset) noexcept {
        return detail::eager_files_update{fds, offset};
    }

    inline detail::eager_fallocate
    fallocate(int fd, int mode, off_t offset, off_t len) noexcept {
        return detail::eager_fallocate{fd, mode, offset, len};
    }

    inline detail::eager_openat
    openat(int dfd, const char *path, int flags, mode_t mode) noexcept {
        return detail::eager_openat{dfd, path, flags, mode};
    }

    inline detail::eager_openat_direct openat_direct(
        int dfd,
        const char *path,
        int flags,
        mode_t mode,
        unsigned int file_index
    ) noexcept {
        return detail::eager_openat_direct{dfd, path, flags, mode, file_index};
    }

    inline detail::eager_openat2
    openat2(int dfd, const char *path, open_how *how) noexcept {
        return detail::eager_openat2{dfd, path, how};
    }

    inline detail::eager_openat2_direct openat2_direct(
        int dfd, const char *path, open_how *how, unsigned int file_index
    ) noexcept {
        return detail::eager_openat2_direct{dfd, path, how, file_index};
    }

    inline detail::eager_statx statx(
        int dfd,
        const char *path,
        int flags,
        unsigned int mask,
        struct statx *statxbuf
    ) noexcept {
        return detail::eager_statx{dfd, path, flags, mask, statxbuf};
    }

    inline detail::eager_unlinkat
    unlinkat(int dfd, const char *path, int flags) noexcept {
        return detail::eager_unlinkat{dfd, path, flags};
    }

    inline detail::eager_renameat renameat(
        int olddfd,
        const char *oldpath,
        int newdfd,
        const char *newpath,
        int flags
    ) noexcept {
        return detail::eager_renameat{olddfd, oldpath, newdfd, newpath, flags};
    }

    inline detail::eager_mkdirat
    mkdirat(int dfd, const char *path, mode_t mode) noexcept {
        return detail::eager_mkdirat{dfd, path, mode};
    }

    inline detail::eager_symlinkat
    symlinkat(const char *target, int newdirfd, const char *linkpath) noexcept {
        return detail::eager_symlinkat{target, newdirfd, linkpath};
    }

    inline detail::eager_linkat linkat(
        int olddfd,
        const char *oldpath,
        int newdfd,
        const char *newpath,
        int flags
    ) noexcept {
        return detail::eager_linkat{olddfd, oldpath, newdfd, newpath, flags};
    }

    /**
     * @brief Set timeout. When it expires, the coroutine will wake up
     *
     * @param ts The relative time duration, or the absolute time stamp.
     * @param count The completion event count.
     * @param flags If it contains IORING_TIMEOUT_ABS, uses absolute time
     * stamp. See man io_uring_enter(2).
     * @return eager_awaiter
     */
    inline detail::eager_timeout_timespec timeout(
        const __kernel_timespec &ts, unsigned int count, unsigned int flags
    ) noexcept {
        return detail::eager_timeout_timespec{ts, count, flags};
    }

    /**
     * @brief Set duration timeout.
     *
     * @param flags See man io_uring_enter(2).
     * @return eager_awaiter
     */
    template<class Rep, class Period = std::ratio<1>>
    inline detail::eager_timeout timeout(
        std::chrono::duration<Rep, Period> duration, unsigned int flags = 0
    ) noexcept {
        return detail::eager_timeout{duration, flags};
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
    inline detail::eager_splice splice(
        int fd_in,
        int64_t off_in,
        int fd_out,
        int64_t off_out,
        unsigned int nbytes,
        unsigned int splice_flags
    ) noexcept {
        return detail::eager_splice{fd_in,   off_in, fd_out,
                                    off_out, nbytes, splice_flags};
    }

    inline detail::eager_tee
    tee(int fd_in, int fd_out, unsigned int nbytes, unsigned int splice_flags
    ) noexcept {
        return detail::eager_tee{fd_in, fd_out, nbytes, splice_flags};
    }

} // namespace eager

} // namespace co_context
