/*
 *  A C++ helper for io_uring
 *
 *  Copyright (C) 2022 Zifeng Deng
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU Affero General Public License as published
 *  by the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Affero General Public License for more details.
 *
 *  You should have received a copy of the GNU Affero General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
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
#include <system_error>
#include <cassert>
#include "uring/compat.h"
#include "uring/io_uring.h"
#include "uring/barrier.h"
#include "uring/syscall.hpp"
#include "uring/SQEntry.hpp"
#include "uring/CQEntry.hpp"
#include "uring/detail/SQ.hpp"
#include "uring/detail/CQ.hpp"

struct statx;

namespace liburingcxx {

constexpr uint64_t LIBURING_UDATA_TIMEOUT = -1;

struct URingParams final : io_uring_params {
    /**
     * @brief Construct a new io_uring_params without initializing
     */
    URingParams() noexcept = default;

    /**
     * @brief Construct a new io_uring_params with memset and flags
     */
    explicit URingParams(unsigned flags) noexcept {
        memset(this, 0, sizeof(*this));
        this->flags = flags;
    }
};

template<unsigned URingFlags>
class [[nodiscard]] URing final {
  public:
    using Params = URingParams;

  private:
    using SubmissionQueue = detail::SubmissionQueue;
    using CompletionQueue = detail::CompletionQueue;

    SubmissionQueue sq;
    CompletionQueue cq;
    // unsigned flags; // is now URingFlags
    int ring_fd;

    unsigned features;
    // unsigned pad[3];

  public:
    /**
     * @brief Submit sqes acquired from io_uring_get_sqe() to the kernel.
     *
     * @return unsigned number of sqes submitted
     */
    unsigned submit() {
        const unsigned submitted = sq.flush();
        unsigned enterFlags = 0;

        if (isSQRingNeedEnter(enterFlags)) {
            if constexpr (URingFlags & IORING_SETUP_IOPOLL)
                enterFlags |= IORING_ENTER_GETEVENTS;

            const int consumedNum = detail::__sys_io_uring_enter(
                ring_fd, submitted, 0, enterFlags, NULL
            );

            if (consumedNum < 0) [[unlikely]]
                throw std::system_error{
                    errno, std::system_category(), "submitAndWait"};
        }

        return submitted;
    }

    /**
     * @brief Submit sqes acquired from io_uring_get_sqe() to the kernel.
     *
     * @return unsigned number of sqes submitted
     */
    unsigned submitAndWait(unsigned waitNum) {
        const unsigned submitted = sq.flush();
        unsigned enterFlags = 0;

        if (waitNum || isSQRingNeedEnter(enterFlags)) {
            if (waitNum || (URingFlags & IORING_SETUP_IOPOLL))
                enterFlags |= IORING_ENTER_GETEVENTS;

            const int consumedNum = detail::__sys_io_uring_enter(
                ring_fd, submitted, waitNum, enterFlags, NULL
            );

            if (consumedNum < 0) [[unlikely]]
                throw std::system_error{
                    errno, std::system_category(), "submitAndWait"};
        }

        return submitted;
    }

    /**
     * @brief Returns number of unconsumed (if SQPOLL) or unsubmitted entries
     * exist in the SQ ring
     */
    inline unsigned SQReady() const noexcept {
        /*
         * Without a barrier, we could miss an update and think the SQ wasn't
         * ready. We don't need the load acquire for non-SQPOLL since then we
         * drive updates.
         */
        if constexpr (URingFlags & IORING_SETUP_SQPOLL)
            return sq.sqe_tail - io_uring_smp_load_acquire(sq.khead);

        /* always use real head, to avoid losing sync for short submit */
        return sq.sqe_tail - *sq.khead;
    }

    /**
     * @brief Returns how much space is left in the SQ ring.
     *
     * @return unsigned the available space in SQ ring
     */
    inline unsigned SQSpaceLeft() const noexcept {
        return *sq.kring_entries - SQReady();
    }

    /**
     * @brief Return an sqe to fill. User must later call submit().
     *
     * @details Return an sqe to fill. Application must later call
     * io_uring_submit() when it's ready to tell the kernel about it. The caller
     * may call this function multiple times before calling submit().
     *
     * @return SQEntry* Returns a vacant sqe, or nullptr if we're full.
     */
    inline SQEntry *getSQEntry() noexcept {
        const unsigned int head = io_uring_smp_load_acquire(sq.khead);
        const unsigned int next = sq.sqe_tail + 1;
        SQEntry *sqe = nullptr;
        if (next - head <= *sq.kring_entries) {
            sqe = reinterpret_cast<SQEntry *>(
                sq.sqes + (sq.sqe_tail & *sq.kring_mask)
            );
            sq.sqe_tail = next;
        }
        return sqe;
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
    inline int SQRingWait() {
        if constexpr (!(URingFlags & IORING_SETUP_SQPOLL)) return 0;
        if (SQSpaceLeft()) return 0;
        const int result = detail::__sys_io_uring_enter(
            ring_fd, 0, 0, IORING_ENTER_SQ_WAIT, nullptr
        );
        if (result < 0)
            throw std::system_error{
                errno, std::system_category(),
                "SQRingWait __sys_io_uring_enter"};
        return result;
    }

    inline CQEntry *waitCQEntry() { return waitCQEntryNum(1); }

    inline CQEntry *peekCQEntry() { return waitCQEntryNum(0); }

    inline CQEntry *waitCQEntryNum(unsigned num) {
        return getCQEntry(/* submit */ 0, num, /* sigmask */ nullptr);
    }

    inline void SeenCQEntry(CQEntry *cqe) noexcept {
        assert(cqe != nullptr);
        CQAdvance(1);
    }

  public:
    URing(unsigned entries, Params &params) {
        // override the params.flags
        params.flags = URingFlags;
        const int fd = detail::__sys_io_uring_setup(entries, &params);
        if (fd < 0) [[unlikely]]
            throw std::system_error{
                errno, std::system_category(), "__sys_io_uring_setup"};

        memset(this, 0, sizeof(*this));
        // this->flags = params.flags;
        this->ring_fd = fd;
        this->features = params.features;
        try {
            mmapQueue(fd, params);
        } catch (...) {
            close(fd);
            std::rethrow_exception(std::current_exception());
        }
    }

    URing(unsigned entries, Params &&params) : URing(entries, params) {}

    URing(unsigned entries) : URing(entries, Params{URingFlags}) {}

    /**
     * ban all copying or moving
     */
    URing(const URing &) = delete;
    URing(URing &&) = delete;
    URing &operator=(const URing &) = delete;
    URing &operator=(URing &&) = delete;

    ~URing() noexcept {
        munmap(sq.sqes, *sq.kring_entries * sizeof(io_uring_sqe));
        unmapRings();
        close(ring_fd);
    }

  private:
    /**
     * @brief Create mapping from kernel to SQ and CQ.
     *
     * @param fd fd of io_uring in kernel
     * @param p params describing the shape of ring
     */
    void mmapQueue(int fd, Params &p) {
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
                unmapRings();
                throw std::system_error{
                    errno, std::system_category(), "cq.ring MAP_FAILED"};
            }
        }

        sq.setOffset(p.sq_off);

        const size_t sqes_size = p.sq_entries * sizeof(io_uring_sqe);
        sq.sqes = reinterpret_cast<io_uring_sqe *>(mmap(
            0, sqes_size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, fd,
            IORING_OFF_SQES
        ));
        if (sq.sqes == MAP_FAILED) [[unlikely]] {
            unmapRings();
            throw std::system_error{
                errno, std::system_category(), "sq.sqes MAP_FAILED"};
        }

        cq.setOffset(p.cq_off);
    }

    inline void unmapRings() noexcept {
        munmap(sq.ring_ptr, sq.ring_sz);
        if (cq.ring_ptr && cq.ring_ptr != sq.ring_ptr)
            munmap(cq.ring_ptr, cq.ring_sz);
    }

    inline bool isSQRingNeedEnter(unsigned &flags) const noexcept {
        if constexpr (!(URingFlags & IORING_SETUP_SQPOLL)) return true;

        if (IO_URING_READ_ONCE(*sq.kflags) & IORING_SQ_NEED_WAKEUP)
            [[unlikely]] {
            flags |= IORING_ENTER_SQ_WAKEUP;
            return true;
        }

        return false;
    }

    inline bool isCQRingNeedFlush() const noexcept {
        return IO_URING_READ_ONCE(*sq.kflags) & IORING_SQ_CQ_OVERFLOW;
    }

    inline void CQAdvance(unsigned num) noexcept {
        assert(num > 0 && "CQAdvance: num must be positive.");
        io_uring_smp_store_release(cq.khead, *cq.khead + num);
    }

    auto __peekCQEntry() {
        struct ReturnType {
            CQEntry *cqe;
            unsigned availableNum;
        } ret;

        const unsigned mask = *cq.kring_mask;

        while (true) {
            const unsigned tail = io_uring_smp_load_acquire(cq.ktail);
            const unsigned head = *cq.khead;

            ret.cqe = nullptr;
            ret.availableNum = tail - head;
            if (ret.availableNum == 0) return ret;

            ret.cqe = reinterpret_cast<CQEntry *>(cq.cqes + (head & mask));
            if (!(this->features & IORING_FEAT_EXT_ARG)
                && ret.cqe->user_data == LIBURING_UDATA_TIMEOUT) {
                CQAdvance(1);
                if (ret.cqe->res < 0) [[unlikely]] {
                    // TODO Reconsider whether to use exceptions
                    throw std::system_error{
                        ret.cqe->res, std::system_category(), "__peekCQEntry"};
                } else {
                    continue;
                }
            }

            break;
        }

        return ret;
    }

    CQEntry *getCQEntry(detail::CQEGetter &data) {
        while (true) {
            bool isNeedEnter = false;
            bool isCQOverflowFlush = false;
            unsigned flags = 0;

            auto [cqe, availableNum] = __peekCQEntry();
            if (cqe == nullptr && data.waitNum == 0 && data.submit == 0) {
                if (!isCQRingNeedFlush()) return nullptr;
                // TODO Reconsider whether to use exceptions
                // throw std::system_error{
                //     EAGAIN, std::system_category(), "getCQEntry_impl.1"};
                isCQOverflowFlush = true;
            }
            if (data.waitNum > availableNum || isCQOverflowFlush) {
                flags = IORING_ENTER_GETEVENTS | data.getFlags;
                isNeedEnter = true;
            }
            if (data.submit) {
                isSQRingNeedEnter(flags);
                isNeedEnter = true;
            }
            if (!isNeedEnter) return cqe;

            const int result = detail::__sys_io_uring_enter2(
                ring_fd, data.submit, data.waitNum, flags, (sigset_t *)data.arg,
                data.size
            );

            if (result < 0)
                // TODO Reconsider whether to use exceptions
                throw std::system_error{
                    errno, std::system_category(), "getCQEntry_impl.2"};
            data.submit -= result;
            if (cqe != nullptr) return cqe;
        }
    }

    inline CQEntry *
    getCQEntry(unsigned submit, unsigned waitNum, sigset_t *sigmask) {
        detail::CQEGetter data{
            .submit = submit,
            .waitNum = waitNum,
            .getFlags = 0,
            .size = _NSIG / 8,
            .arg = sigmask};
        return getCQEntry(data);
    }
};

} // namespace liburingcxx
