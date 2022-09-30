/*
 *  A C++ helper for io_uring
 *
 *     Copyright 2022 Zifeng Deng
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */
#pragma once

#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 500 /* Required for glibc to expose sigset_t */
#endif

#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <cerrno>
#include <signal.h>
#include <stdbool.h>
#include <inttypes.h>
#include <time.h>
#include <sched.h>
#include <linux/swab.h>
#include <linux/version.h>
#include <system_error>
#include <cassert>
#include "uring/compat.h"
#include "uring/io_uring.h"
#include "uring/barrier.h"
#include "uring/syscall.hpp"
#include "uring/detail/sq.hpp"
#include "uring/detail/cq.hpp"
#include "uring/detail/int_flags.h"

struct statx;

namespace liburingcxx {

namespace config {

    // HACK this assumes app will use registered ring.
    constexpr bool using_register_ring_fd =
        LINUX_VERSION_CODE >= KERNEL_VERSION(5, 18, 0);

    constexpr unsigned default_enter_flags_registered_ring =
        using_register_ring_fd ? IORING_ENTER_REGISTERED_RING : 0;

    constexpr unsigned default_enter_flags =
        default_enter_flags_registered_ring;

};

constexpr uint64_t LIBURING_UDATA_TIMEOUT = -1;

struct uring_params final : io_uring_params {
    /**
     * @brief Construct a new io_uring_params without initializing
     */
    uring_params() noexcept = default;

    /**
     * @brief Construct a new io_uring_params with memset and flags
     */
    explicit uring_params(unsigned flags) noexcept {
        memset(this, 0, sizeof(*this));
        this->flags = flags;
    }
};

template<unsigned uring_flags>
class [[nodiscard]] uring final {
  public:
    using params = uring_params;

  private:
    using submission_queue = detail::submission_queue;
    using completion_queue = detail::completion_queue;

    submission_queue sq;
    completion_queue cq;
    // unsigned flags; // is now uring_flags
    int ring_fd;

    unsigned features;
    int enter_ring_fd;
    __u8 int_flags;
    __u8 pad[3];
    unsigned pad2;

  public:
    /**
     * @brief Submit sqes acquired from io_uring_get_sqe() to the kernel.
     *
     * @return unsigned number of sqes submitted
     */
    unsigned submit() {
        const unsigned submitted = sq.flush();
        unsigned enter_flags = config::default_enter_flags;

        if (is_sq_ring_need_enter(enter_flags)) {
            if constexpr (uring_flags & IORING_SETUP_IOPOLL)
                enter_flags |= IORING_ENTER_GETEVENTS;

            // HACK see config::using_register_ring_fd.
            // if (this->int_flags & INT_FLAG_REG_RING)
            //     enter_flags |= IORING_ENTER_REGISTERED_RING;

            const int consumed_num = detail::__sys_io_uring_enter(
                enter_ring_fd, submitted, 0, enter_flags, NULL
            );

            if (consumed_num < 0) [[unlikely]]
                throw std::system_error{
                    -consumed_num, std::system_category(), "uring::submit"};
        }

        return submitted;
    }

    /**
     * @brief Submit sqes acquired from io_uring_get_sqe() to the kernel.
     *
     * @return unsigned number of sqes submitted
     */
    unsigned submit_and_wait(unsigned wait_num) {
        const unsigned submitted = sq.flush();
        unsigned enter_flags = config::default_enter_flags;

        if (wait_num || is_sq_ring_need_enter(enter_flags)) {
            if (wait_num || (uring_flags & IORING_SETUP_IOPOLL))
                enter_flags |= IORING_ENTER_GETEVENTS;

            // HACK see config::default_enter_flags.
            // if (this->int_flags & INT_FLAG_REG_RING)
            //     enter_flags |= IORING_ENTER_REGISTERED_RING;

            const int consumed_num = detail::__sys_io_uring_enter(
                enter_ring_fd, submitted, wait_num, enter_flags, NULL
            );

            if (consumed_num < 0) [[unlikely]]
                throw std::system_error{
                    -consumed_num, std::system_category(),
                    "uring::submit_and_wait"};
        }

        return submitted;
    }

    /**
     * @brief Returns number of unconsumed (if SQPOLL) or unsubmitted entries
     * exist in the SQ ring
     */
    inline unsigned sq_pending() const noexcept {
        return sq.template pending<uring_flags>();
    }

    /**
     * @brief Returns how much space is left in the SQ ring.
     *
     * @return unsigned the available space in SQ ring
     */
    inline unsigned sq_space_left() const noexcept {
        return sq.ring_entries - sq_pending();
    }

    inline unsigned get_sq_ring_entries() const noexcept {
        return sq.ring_entries;
    }

    inline const sq_entry *__get_sqes() const noexcept { return sq.sqes; }

    /**
     * @brief Return an sqe to fill. User must later call submit().
     *
     * @details Return an sqe to fill. Application must later call
     * io_uring_submit() when it's ready to tell the kernel about it. The caller
     * may call this function multiple times before calling submit().
     *
     * @return sq_entry* Returns a vacant sqe, or nullptr if we're full.
     */
    [[nodiscard]] inline sq_entry *get_sq_entry() noexcept {
        return sq.template get_sq_entry<uring_flags>();
    }

    /**
     * @brief Append an SQE to SQ, but do not notify the io_uring.
     *
     * @param sqe
     */
    inline void append_sq_entry(const sq_entry *sqe) noexcept {
        sq.append_sq_entry(sqe);
    }

    /**
     * @brief wait until the SQ ring is not full
     *
     * @details Only applicable when using SQPOLL - allows the caller to wait
     * for space to free up in the SQ ring, which happens when the kernel side
     * thread has consumed one or more entries. If the SQ ring is currently
     * non-full, no action is taken. Note: may return -EINVAL if the kernel
     * doesn't support this feature.
     *
     * @return what I don't know
     */
    inline int wait_sq_ring() {
        if constexpr (!(uring_flags & IORING_SETUP_SQPOLL)) return 0;
        if (sq_space_left()) return 0;

        // HACK this assumes app will use registered ring.
        // if (ring->int_flags & INT_FLAG_REG_RING)
        //     flags |= IORING_ENTER_REGISTERED_RING;
        const int result = detail::__sys_io_uring_enter(
            this->enter_ring_fd, 0, 0,
            IORING_ENTER_SQ_WAIT | config::default_enter_flags_registered_ring,
            nullptr
        );

        if (result < 0) [[unlikely]]
            throw std::system_error{
                -result, std::system_category(),
                "wait_sq_ring __sys_io_uring_enter"};
        return result;
    }

    inline unsigned cq_ready_relaxed() const noexcept {
        return *cq.ktail - *cq.khead;
    }

    inline unsigned cq_ready_acquire() const noexcept {
        return io_uring_smp_load_acquire(cq.ktail) - *cq.khead;
    }

    inline cq_entry *wait_cq_entry() {
        auto [cqe, available_num] = __peek_cq_entry();
        if (cqe != nullptr) return cqe;

        return wait_cq_entry_num(1);
    }

    inline cq_entry *peek_cq_entry() {
        auto [cqe, available_num] = __peek_cq_entry();
        if (cqe != nullptr) return cqe;

        return wait_cq_entry_num(0);
    }

    inline cq_entry *wait_cq_entry_num(unsigned num) {
        return get_cq_entry(/* submit */ 0, num, /* sigmask */ nullptr);
    }

    inline void seen_cq_entry(const cq_entry *cqe) noexcept {
        assert(cqe != nullptr);
        cq_advance(1);
    }

    int register_ring_fd() {
        assert(config::using_register_ring_fd && "kernel version < 5.18");

        struct io_uring_rsrc_update up = {
            .offset = -1U,
            .data = (uint64_t)this->ring_fd,
        };

        const int ret = detail::__sys_io_uring_register(
            this->ring_fd, IORING_REGISTER_RING_FDS, &up, 1
        );

        if (ret == 1) [[likely]] {
            this->enter_ring_fd = up.offset;
            this->int_flags |= INT_FLAG_REG_RING;
        } else if (ret < 0) {
            throw std::system_error{
                -ret, std::system_category(), "uring::register_ring_fd"};
        }

        return ret;
    }

    int unregister_ring_fd() {
        assert(config::using_register_ring_fd && "kernel version < 5.18");

        struct io_uring_rsrc_update up = {
            .offset = this->enter_ring_fd,
        };

        const int ret = detail::__sys_io_uring_register(
            this->ring_fd, IORING_UNREGISTER_RING_FDS, &up, 1
        );

        if (ret == 1) [[likely]] {
            this->enter_ring_fd = this->ring_fd;
            this->int_flags &= ~INT_FLAG_REG_RING;
        } else if (ret < 0) {
            throw std::system_error{
                -ret, std::system_category(), "uring::unregister_ring_fd"};
        }

        return ret;
    }

  public:
    uring(unsigned entries, params &params) {
        // override the params.flags
        params.flags = uring_flags;
        const int fd = detail::__sys_io_uring_setup(entries, &params);
        if (fd < 0) [[unlikely]]
            throw std::system_error{
                -fd, std::system_category(), "uring()::__sys_io_uring_setup"};

        memset(this, 0, sizeof(*this));
        // this->flags = params.flags;
        this->ring_fd = this->enter_ring_fd = fd;
        this->features = params.features;
        this->int_flags = 0;
        try {
            mmap_queue(fd, params);
            this->sq.init_free_queue();
        } catch (...) {
            close(fd);
            std::rethrow_exception(std::current_exception());
        }
    }

    uring(unsigned entries, params &&params) : uring(entries, params) {}

    uring(unsigned entries) : uring(entries, params{uring_flags}) {}

    /**
     * ban all copying or moving
     */
    uring(const uring &) = delete;
    uring(uring &&) = delete;
    uring &operator=(const uring &) = delete;
    uring &operator=(uring &&) = delete;

    ~uring() noexcept {
        munmap(sq.sqes, sq.ring_entries * sizeof(io_uring_sqe));
        unmap_rings();
        close(ring_fd);
    }

  private:
    /**
     * @brief Create mapping from kernel to SQ and CQ.
     *
     * @param fd fd of io_uring in kernel
     * @param p params describing the shape of ring
     */
    void mmap_queue(int fd, params &p) {
        sq.ring_sz = p.sq_off.array + p.sq_entries * sizeof(unsigned);
        cq.ring_sz = p.cq_off.cqes + p.cq_entries * sizeof(io_uring_cqe);

        if (p.features & IORING_FEAT_SINGLE_MMAP)
            sq.ring_sz = cq.ring_sz = std::max(sq.ring_sz, cq.ring_sz);

        sq.ring_ptr = mmap(
            nullptr, sq.ring_sz, PROT_READ | PROT_WRITE,
            MAP_SHARED | MAP_POPULATE, fd, IORING_OFF_SQ_RING
        );
        if (sq.ring_ptr == MAP_FAILED) [[unlikely]]
            throw std::system_error{
                errno, std::system_category(), "sq.ring MAP_FAILED"};

        if (p.features & IORING_FEAT_SINGLE_MMAP) {
            cq.ring_ptr = sq.ring_ptr;
        } else {
            cq.ring_ptr = mmap(
                nullptr, cq.ring_sz, PROT_READ | PROT_WRITE,
                MAP_SHARED | MAP_POPULATE, fd, IORING_OFF_CQ_RING
            );
            if (cq.ring_ptr == MAP_FAILED) [[unlikely]] {
                // don't forget to clean up sq
                cq.ring_ptr = nullptr;
                unmap_rings();
                throw std::system_error{
                    errno, std::system_category(), "cq.ring MAP_FAILED"};
            }
        }

        sq.set_offset(p.sq_off);

        const size_t sqes_size = p.sq_entries * sizeof(io_uring_sqe);
        sq.sqes = reinterpret_cast<sq_entry *>(mmap(
            0, sqes_size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, fd,
            IORING_OFF_SQES
        ));
        if (sq.sqes == MAP_FAILED) [[unlikely]] {
            unmap_rings();
            throw std::system_error{
                errno, std::system_category(), "sq.sqes MAP_FAILED"};
        }

        cq.set_offset(p.cq_off);
    }

    inline void unmap_rings() noexcept {
        munmap(sq.ring_ptr, sq.ring_sz);
        if (cq.ring_ptr && cq.ring_ptr != sq.ring_ptr)
            munmap(cq.ring_ptr, cq.ring_sz);
    }

    inline constexpr bool is_sq_ring_need_enter(unsigned &enter_flags
    ) const noexcept {
        if constexpr (!(uring_flags & IORING_SETUP_SQPOLL)) return true;

        /*
         * Ensure the kernel can see the store to the SQ tail before we read
         * the flags.
         * See https://github.com/axboe/liburing/issues/541
         */
        // PERF memory order
        // BUG
        // std::atomic_thread_fence(std::memory_order_seq_cst);

        if (IO_URING_READ_ONCE(*sq.kflags) & IORING_SQ_NEED_WAKEUP)
            [[unlikely]] {
            enter_flags |= IORING_ENTER_SQ_WAKEUP;
            return true;
        }

        return false;
    }

    inline bool is_cq_ring_need_flush() const noexcept {
        return IO_URING_READ_ONCE(*sq.kflags)
               & (IORING_SQ_CQ_OVERFLOW | IORING_SQ_TASKRUN);
    }

    inline bool is_cq_ring_need_enter() const noexcept {
        if constexpr (uring_flags & IORING_SETUP_IOPOLL) return true;
        return is_cq_ring_need_flush();
    }

    inline void cq_advance(unsigned num) noexcept {
        assert(num > 0 && "cq_advance: num must be positive.");
        io_uring_smp_store_release(cq.khead, *cq.khead + num);
    }

    auto __peek_cq_entry() {
        struct ReturnType {
            cq_entry *cqe;
            unsigned available_num;
        } ret;

        const unsigned mask = cq.ring_mask;

        while (true) {
            const unsigned tail = io_uring_smp_load_acquire(cq.ktail);
            const unsigned head = *cq.khead;

            ret.cqe = nullptr;
            ret.available_num = tail - head;
            if (ret.available_num == 0) return ret;

            ret.cqe = cq.cqes + (head & mask);
            if (!(this->features & IORING_FEAT_EXT_ARG)
                && ret.cqe->user_data == LIBURING_UDATA_TIMEOUT) [[unlikely]] {
                cq_advance(1);
                if (ret.cqe->res < 0) [[unlikely]] {
                    // TODO Reconsider whether to use exceptions
                    throw std::system_error{
                        -ret.cqe->res, std::system_category(),
                        "uring::__peek_cq_entry"};
                } else {
                    continue;
                }
            }

            break;
        }

        return ret;
    }

    cq_entry *get_cq_entry(detail::cq_entry_getter &data) {
        bool is_looped = false;
        while (true) {
            bool is_need_enter = false;
            unsigned flags = 0;

            auto [cqe, available_num] = __peek_cq_entry();
            if (cqe == nullptr && data.wait_num == 0 && data.submit == 0) {
                /*
                 * If we already looped once, we already entererd
                 * the kernel. Since there's nothing to submit or
                 * wait for, don't keep retrying.
                 */
                if (is_looped || !is_cq_ring_need_enter()) return nullptr;
                // TODO Reconsider whether to use exceptions
                // throw std::system_error{
                //     EAGAIN, std::system_category(), "get_cq_entry_impl.1"};
                is_need_enter = true;
            }
            if (data.wait_num > available_num || is_need_enter) {
                flags = IORING_ENTER_GETEVENTS | data.getFlags;
                is_need_enter = true;
            }
            if (data.submit && is_sq_ring_need_enter(flags)) {
                is_need_enter = true;
            }
            if (!is_need_enter) return cqe;

            // HACK this assumes app will use registered ring.
            // if (this->int_flags & INT_FLAG_REG_RING)
            //     flags |= IORING_ENTER_REGISTERED_RING;
            if constexpr (config::using_register_ring_fd)
                flags |= IORING_ENTER_REGISTERED_RING;

            // TODO Upgrade for ring.int_flags & INT_FLAG_REG_RING
            const int result = detail::__sys_io_uring_enter2(
                enter_ring_fd, data.submit, data.wait_num, flags,
                (sigset_t *)data.arg, data.size
            );

            if (result < 0) [[unlikely]]
                // TODO Reconsider whether to use exceptions
                throw std::system_error{
                    -result, std::system_category(), "get_cq_entry_impl.2"};
            data.submit -= result;
            if (cqe != nullptr) return cqe;
            is_looped = true;
        }
    }

    inline cq_entry *
    get_cq_entry(unsigned submit, unsigned wait_num, sigset_t *sigmask) {
        detail::cq_entry_getter data{
            .submit = submit,
            .wait_num = wait_num,
            .getFlags = 0,
            .size = _NSIG / 8,
            .arg = sigmask};
        return get_cq_entry(data);
    }
};

} // namespace liburingcxx
