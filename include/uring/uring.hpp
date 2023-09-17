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

#ifndef _GNU_SOURCE
#define _GNU_SOURCE /* Required for musl to expose cpu_set_t */
#endif

#include <uring/barrier.h>
#include <uring/buf_ring.hpp>
#include <uring/compat.hpp>
#include <uring/cq_entry.hpp>
#include <uring/detail/cq.hpp>
#include <uring/detail/int_flags.h>
#include <uring/detail/sq.hpp>
#include <uring/io_uring.h>
#include <uring/syscall.hpp>
#include <uring/uring_define.hpp>
#include <uring/utility/kernel_version.hpp>

#include <algorithm>
#include <cassert>
#include <cerrno>
#include <cinttypes>
#include <concepts>
#include <csignal>
#include <cstdint>
#include <ctime>
#include <linux/swab.h>
#include <sched.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <system_error>
#include <type_traits>
#include <utility>

struct statx;

namespace liburingcxx {

namespace config {

    // HACK this assumes app will use registered ring.
    constexpr bool using_register_ring_fd = is_kernel_reach(5, 18);

    constexpr unsigned default_enter_flags_registered_ring =
        using_register_ring_fd ? IORING_ENTER_REGISTERED_RING : 0;

    constexpr unsigned default_enter_flags =
        default_enter_flags_registered_ring;

}; // namespace config

constexpr uint64_t LIBURING_UDATA_TIMEOUT = -1ULL;

struct uring_params final : io_uring_params {
    /**
     * @brief Construct a new io_uring_params without initializing
     */
    uring_params() noexcept = default;

    /**
     * @brief Construct a new io_uring_params with memset and flags
     */
    explicit uring_params(unsigned flags) noexcept {
        std::memset(this, 0, sizeof(*this));
        this->flags = flags;
    }
};

struct __peek_cq_entry_return_type /*NOLINT*/ final {
    const cq_entry *cqe;
    unsigned available_num;
    int err;
};

template<uint64_t uring_flags>
class [[nodiscard]] uring final {
  public:
    using params = uring_params;

  private:
    using submission_queue = detail::submission_queue;
    using completion_queue = detail::completion_queue;

    submission_queue sq;
    completion_queue cq;
    // unsigned flags; // is now uring_flags
    int ring_fd = -1;

    unsigned features;
    int enter_ring_fd;
    __u8 int_flags;
    __u8 pad[3];
    unsigned pad2;

  public:
    int fd() const noexcept { return ring_fd; }

    int submit() noexcept;

    int submit_and_wait(unsigned wait_num) noexcept;

#if LIBURINGCXX_IS_KERNEL_REACH(5, 11)
    int submit_and_wait_timeout(
        const cq_entry *(&cqe),
        unsigned wait_num,
        const __kernel_timespec &ts,
        sigset_t *sigmask
    ) noexcept;
#endif

    int submit_and_get_events() noexcept;

    int get_events() noexcept;

    [[nodiscard]]
    unsigned sq_pending() const noexcept;

    [[nodiscard]]
    unsigned sq_space_left() const noexcept;

    [[nodiscard]]
    unsigned get_sq_ring_entries() const noexcept;

    [[nodiscard]]
    sq_entry *get_sq_entry() noexcept;

    void append_sq_entry(const sq_entry *sqe) noexcept;

    int wait_sq_ring();

    [[nodiscard]]
    unsigned cq_ready_relaxed() const noexcept;

    [[nodiscard]]
    unsigned cq_ready_acquire() const noexcept;

    int wait_cq_entry(const cq_entry *(&cqe_ptr)) noexcept;

    int peek_cq_entry(const cq_entry *(&cqe_ptr)) noexcept;

    unsigned peek_batch_cq_entries(std::span<const cq_entry *> cqes) noexcept;

    int
    wait_cq_entry_num(const cq_entry *(&cqe_ptr), unsigned wait_num) noexcept;

#if LIBURINGCXX_IS_KERNEL_REACH(5, 11)
    int wait_cq_entries(
        const cq_entry *(&cqe_ptr),
        unsigned wait_num,
        const __kernel_timespec &ts,
        sigset_t *sigmask
    ) noexcept;
#endif

    template<typename F>
        requires std::regular_invocable<F, cq_entry *>
                 && std::is_void_v<std::invoke_result_t<F, cq_entry *>>
    unsigned for_each_cqe(F f) noexcept(noexcept(f(std::declval<cq_entry *>()))
    ) {
        unsigned count = 0;
        for (auto head = *cq.khead; head != io_uring_smp_load_acquire(cq.ktail);
             ++head, ++count) {
            cq_entry *const cqe = &cq.cqe_at<uring_flags>(head);
            f(cqe);
        }
        return count;
    }

    void cq_advance(unsigned num) noexcept;

    void seen_cq_entry(const cq_entry *cqe) noexcept;

    int register_ring_fd();

    int unregister_ring_fd();

    [[nodiscard]]
    constexpr bool is_cq_ring_need_enter() const noexcept;

  public:
    /**
     * @brief Init the io_uring.
     * @note Must be call on the Corresponding thread. (Ring per thread)
     * @param entries The size of sq ring. Must be pow of 2.
     */
    void init(unsigned entries);
    void init(unsigned entries, params &params);
    void init(unsigned entries, params &&params);

    explicit uring() noexcept = default;

    /**
     * ban all copying or moving
     */
    uring(const uring &) = delete;
    uring(uring &&) = delete;
    uring &operator=(const uring &) = delete;
    uring &operator=(uring &&) = delete;

    ~uring() noexcept;

  private:
    int __submit /*NOLINT*/ (
        unsigned submitted, unsigned wait_num, bool getevents
    ) noexcept;

    void mmap_queue(int fd, params &p);

    void unmap_rings() noexcept;

    constexpr bool is_sq_ring_need_enter(unsigned submit, unsigned &enter_flags)
        const noexcept;

    [[nodiscard]]
    bool is_cq_ring_need_flush() const noexcept;

    void buf_ring_cq_advance(buf_ring &br, unsigned count) noexcept;

    // NOLINTNEXTLINE
    [[nodiscard]]
    __peek_cq_entry_return_type __peek_cq_entry() noexcept;

    template<bool has_ts>
    int _get_cq_entry /*NOLINT*/ (
        const cq_entry *(&cqe_ptr), detail::cq_entry_getter &data
    ) noexcept;

    int __get_cq_entry /*NOLINT*/ (
        const cq_entry *(&cqe_ptr),
        unsigned submit,
        unsigned wait_num,
        sigset_t *sigmask
    ) noexcept;

#if LIBURINGCXX_IS_KERNEL_REACH(5, 11)
    int wait_cq_entries_new(
        const cq_entry *(&cqe_ptr),
        unsigned wait_num,
        const __kernel_timespec &ts,
        sigset_t *sigmask
    ) noexcept;
#endif
};

/***************************************
 *    Implementation of class uring    *
 ***************************************
 */

/**
 * @brief Submit sqes acquired from io_uring_get_sqe() to the kernel.
 *
 * @return unsigned number of sqes submitted
 */
template<uint64_t uring_flags>
inline int uring<uring_flags>::submit() noexcept {
    return submit_and_wait(0);
}

/**
 * @brief Submit sqes acquired from io_uring_get_sqe() to the kernel.
 *
 * @return unsigned number of sqes submitted
 */
template<uint64_t uring_flags>
inline int uring<uring_flags>::submit_and_wait(unsigned wait_num) noexcept {
    return __submit(sq.template flush<uring_flags>(), wait_num, false);
}

#if LIBURINGCXX_IS_KERNEL_REACH(5, 11)
template<uint64_t uring_flags>
inline int uring<uring_flags>::submit_and_wait_timeout(
    const cq_entry *(&cqe),
    unsigned wait_num,
    const __kernel_timespec &ts,
    sigset_t *sigmask
) noexcept {
    assert(this->features & IORING_FEAT_EXT_ARG);

    io_uring_getevents_arg arg = {
        .sigmask = (uint64_t)sigmask,
        .sigmask_sz = _NSIG / 8,
        .ts = (uint64_t)(&ts)
    };

    detail::cq_entry_getter data = {
        .submit = sq.template flush<uring_flags>(),
        .wait_num = wait_num,
        .get_flags = IORING_ENTER_EXT_ARG,
        .sz = sizeof(arg),
        .arg = &arg
    };

    return _get_cq_entry<true>(cqe, data);
}
#endif

template<uint64_t uring_flags>
inline int uring<uring_flags>::submit_and_get_events() noexcept {
    return __submit(sq.template flush<uring_flags>(), 0, true);
}

template<uint64_t uring_flags>
inline int uring<uring_flags>::get_events() noexcept {
    constexpr int flags =
        IORING_ENTER_GETEVENTS | config::default_enter_flags_registered_ring;
    return __sys_io_uring_enter(this->enter_ring_fd, 0, 0, flags, nullptr);
}

/**
 * @brief Returns number of unconsumed (if SQPOLL) or unsubmitted
 * entries exist in the SQ ring
 */
template<uint64_t uring_flags>
inline unsigned uring<uring_flags>::sq_pending() const noexcept {
    return sq.template pending<uring_flags>();
}

/**
 * @brief Returns how much space is left in the SQ ring.
 *
 * @return unsigned the available space in SQ ring
 */
template<uint64_t uring_flags>
inline unsigned uring<uring_flags>::sq_space_left() const noexcept {
    return sq.ring_entries - sq_pending();
}

template<uint64_t uring_flags>
inline unsigned uring<uring_flags>::get_sq_ring_entries() const noexcept {
    return sq.ring_entries;
}

/**
 * @brief Return an sqe to fill. User must later call submit().
 *
 * @details Return an sqe to fill. Application must later call
 * io_uring_submit() when it's ready to tell the kernel about it. The caller
 * may call this function multiple times before calling submit().
 *
 * @return sq_entry* Returns a vacant sqe, or nullptr if we're full.
 */
template<uint64_t uring_flags>
inline sq_entry *uring<uring_flags>::get_sq_entry() noexcept {
    return sq.template get_sq_entry<uring_flags>();
}

/**
 * @brief Append an SQE to SQ, but do not notify the io_uring.
 *
 * @param sqe
 */
template<uint64_t uring_flags>
void uring<uring_flags>::append_sq_entry(const sq_entry *sqe) noexcept {
    if constexpr (!(uring_flags & uring_setup::sqe_reorder)) {
        assert(
            false
            && "Please never call `append_sq_entry` "
               "if `uring_setup::sqe_reorder` is disabled"
        );
    }
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
 * @return Sorry, I don't know what is returned by the internal syscall.
 */
template<uint64_t uring_flags>
inline int uring<uring_flags>::wait_sq_ring() {
    if constexpr (!(uring_flags & IORING_SETUP_SQPOLL)) {
        return 0;
    }
    if (sq_space_left()) {
        return 0;
    }

    // HACK this assumes app will use registered ring.
    // if (ring->int_flags & INT_FLAG_REG_RING)
    //     flags |= IORING_ENTER_REGISTERED_RING;
    const int result = __sys_io_uring_enter(
        this->enter_ring_fd, 0, 0,
        IORING_ENTER_SQ_WAIT | config::default_enter_flags_registered_ring,
        nullptr
    );

    if (result < 0) [[unlikely]] {
        throw std::system_error{
            -result, std::system_category(), "wait_sq_ring __sys_io_uring_enter"
        };
    }
    return result;
}

/**
 * @brief Return the number of cqes available for application.
 */
template<uint64_t uring_flags>
inline unsigned uring<uring_flags>::cq_ready_relaxed() const noexcept {
    return *cq.ktail - *cq.khead;
}

/**
 * @brief Return the number of cqes available for application.
 */
template<uint64_t uring_flags>
inline unsigned uring<uring_flags>::cq_ready_acquire() const noexcept {
    return io_uring_smp_load_acquire(cq.ktail) - *cq.khead;
}

/**
 * @brief Return an IO completion, waiting for it if necessary. Returns 0 with
 * cqe_ptr filled in on success, -errno on failure.
 */
template<uint64_t uring_flags>
inline int uring<uring_flags>::wait_cq_entry(const cq_entry *(&cqe_ptr)
) noexcept {
    auto [cqe, available_num, err] = __peek_cq_entry();
    if (!err && cqe != nullptr) {
        cqe_ptr = cqe;
        return 0;
    }

    return wait_cq_entry_num(cqe_ptr, 1);
}

/**
 * @brief Return an IO completion, if one is readily available. Returns 0 with
 * cqe_ptr filled in on success, -errno on failure.
 */
template<uint64_t uring_flags>
inline int uring<uring_flags>::peek_cq_entry(const cq_entry *(&cqe_ptr)
) noexcept {
    auto [cqe, available_num, err] = __peek_cq_entry();
    if (!err && cqe != nullptr) {
        cqe_ptr = cqe;
        return 0;
    }

    return wait_cq_entry_num(cqe_ptr, 0);
}

/**
 * @brief Fill in an array of IO completions up to count, if any are available.
 * Returns the amount of IO completions filled.
 */
template<uint64_t uring_flags>
unsigned
uring<uring_flags>::peek_batch_cq_entries(std::span<const cq_entry *> cqes
) noexcept {
    bool overflow_checked = false;
    constexpr int shift = bool(uring_flags & IORING_SETUP_CQE32) ? 1 : 0;

again:
    unsigned ready = cq_ready_acquire();
    if (ready != 0) {
        unsigned head = *cq.khead;
        const unsigned mask = cq.ring_mask;
        const unsigned count = std::min<unsigned>(cqes.size(), ready);
        const unsigned last = head + count;
        for (int i = 0; head != last; ++head, ++i) {
            cqes[i] = cq.cqes + ((head & mask) << shift);
        }

        return count;
    }

    if (overflow_checked) {
        return 0;
    }

    if (is_cq_ring_need_flush()) {
        get_events();
        overflow_checked = true;
        goto again;
    }

    return 0;
}

template<uint64_t uring_flags>
inline int uring<uring_flags>::wait_cq_entry_num(
    const cq_entry *(&cqe_ptr), unsigned wait_num
) noexcept {
    return __get_cq_entry(
        cqe_ptr, /* submit */ 0, wait_num, /* sigmask */ nullptr
    );
}

#if LIBURINGCXX_IS_KERNEL_REACH(5, 11)
/**
 * Kernel version 5.11 or newer is required!
 */
template<uint64_t uring_flags>
inline int uring<uring_flags>::wait_cq_entries(
    const cq_entry *(&cqe_ptr),
    unsigned wait_num,
    const __kernel_timespec &ts,
    sigset_t *sigmask
) noexcept {
    assert(this->features & IORING_FEAT_EXT_ARG);
    return wait_cq_entries_new(cqe_ptr, wait_num, ts, sigmask);
}
#endif

template<uint64_t uring_flags>
inline void
uring<uring_flags>::seen_cq_entry([[maybe_unused]] const cq_entry *cqe
) noexcept {
    assert(cqe != nullptr);
    cq_advance(1);
}

template<uint64_t uring_flags>
int uring<uring_flags>::register_ring_fd() {
    assert(config::using_register_ring_fd && "kernel version < 5.18"); // NOLINT

    struct io_uring_rsrc_update up = {
        .offset = -1U,
        .resv = 0,
        .data = (uint64_t)this->ring_fd,
    };

    const int ret = __sys_io_uring_register(
        this->ring_fd, IORING_REGISTER_RING_FDS, &up, 1
    );

    if (ret == 1) [[likely]] {
        this->enter_ring_fd = up.offset;
        this->int_flags |= INT_FLAG_REG_RING;
    } else if (ret < 0) {
        throw std::system_error{
            -ret, std::system_category(), "uring::register_ring_fd"
        };
    }

    return ret;
}

template<uint64_t uring_flags>
int uring<uring_flags>::unregister_ring_fd() {
    assert(config::using_register_ring_fd && "kernel version < 5.18"); // NOLINT

    struct io_uring_rsrc_update up = {
        .offset = this->enter_ring_fd,
    };

    const int ret = __sys_io_uring_register(
        this->ring_fd, IORING_UNREGISTER_RING_FDS, &up, 1
    );

    if (ret == 1) [[likely]] {
        this->enter_ring_fd = this->ring_fd;
        this->int_flags &= ~INT_FLAG_REG_RING;
    } else if (ret < 0) {
        throw std::system_error{
            -ret, std::system_category(), "uring::unregister_ring_fd"
        };
    }

    return ret;
}

template<uint64_t uring_flags>
void uring<uring_flags>::init(unsigned entries, params &params) {
    assert(this->ring_fd == -1 && "The uring may be inited twice.");

    // override the params.flags
    params.flags = static_cast<uint32_t>(uring_flags);

    const int fd = __sys_io_uring_setup(entries, &params);
    if (fd < 0) [[unlikely]] {
        throw std::system_error{
            -fd, std::system_category(), "uring()::__sys_io_uring_setup"
        };
    }

    std::memset(this, 0, sizeof(*this)); // NOLINT

    this->ring_fd = this->enter_ring_fd = fd;
    this->features = params.features;
    this->int_flags = 0;
    try {
        mmap_queue(fd, params);
        this->sq.init_free_queue();
        if constexpr (config::using_register_ring_fd) {
            register_ring_fd();
        }
    } catch (...) {
        __sys_close(fd);
        std::rethrow_exception(std::current_exception());
    }
}

template<uint64_t uring_flags>
void uring<uring_flags>::init(unsigned entries, params &&params) {
    init(entries, params);
}

template<uint64_t uring_flags>
void uring<uring_flags>::init(unsigned entries) {
    init(entries, params{static_cast<uint32_t>(uring_flags)});
}

template<uint64_t uring_flags>
uring<uring_flags>::~uring() noexcept {
    if (this->ring_fd == -1) {
        return;
    }
    __sys_munmap(sq.sqes, sq.ring_entries * sizeof(io_uring_sqe));
    unmap_rings();
    __sys_close(ring_fd);
}

/**
 * @brief Submit sqes acquired from get_sq_entry() to the kernel.
 *
 * @return number of sqes submitted
 */
template<uint64_t uring_flags>
int uring<uring_flags>::__submit(
    unsigned submitted, unsigned wait_num, bool getevents
) noexcept {
    bool is_cq_need_enter = (getevents | wait_num) || is_cq_ring_need_enter();
    unsigned flags = config::default_enter_flags_registered_ring;

    if (is_sq_ring_need_enter(submitted, flags) || is_cq_need_enter) {
        if (is_cq_need_enter) {
            flags |= IORING_ENTER_GETEVENTS;
        }

        // HACK see config::default_enter_flags_registered_ring.
        // if (this->int_flags & INT_FLAG_REG_RING)
        //     flags |= IORING_ENTER_REGISTERED_RING;

        const int consumed_num = __sys_io_uring_enter(
            this->enter_ring_fd, submitted, wait_num, flags, nullptr
        );

        return consumed_num;
    }

    return (int)submitted;
}

/**
 * @brief Create mapping from kernel to SQ and CQ.
 *
 * @param fd fd of io_uring in kernel
 * @param p params describing the shape of ring
 */
template<uint64_t uring_flags>
void uring<uring_flags>::mmap_queue(int fd, params &p) {
    sq.ring_sz = p.sq_off.array + p.sq_entries * sizeof(unsigned);
    cq.ring_sz = p.cq_off.cqes + p.cq_entries * sizeof(io_uring_cqe);

    if (p.features & IORING_FEAT_SINGLE_MMAP) {
        sq.ring_sz = cq.ring_sz = std::max(sq.ring_sz, cq.ring_sz);
    }

    sq.ring_ptr = __sys_mmap(
        nullptr, sq.ring_sz, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE,
        fd, IORING_OFF_SQ_RING
    );
    if (sq.ring_ptr == MAP_FAILED) /*NOLINT*/ [[unlikely]] {
        throw std::system_error{
            errno, std::system_category(), "sq.ring MAP_FAILED"
        };
    }

    if (p.features & IORING_FEAT_SINGLE_MMAP) {
        cq.ring_ptr = sq.ring_ptr;
    } else {
        cq.ring_ptr = __sys_mmap(
            nullptr, cq.ring_sz, PROT_READ | PROT_WRITE,
            MAP_SHARED | MAP_POPULATE, fd, IORING_OFF_CQ_RING
        );
        if (cq.ring_ptr == MAP_FAILED) /*NOLINT*/ [[unlikely]] {
            // don't forget to clean up sq
            cq.ring_ptr = nullptr;
            unmap_rings();
            throw std::system_error{
                errno, std::system_category(), "cq.ring MAP_FAILED"
            };
        }
    }

    sq.set_offset(p.sq_off);

    const size_t sqes_size = p.sq_entries * sizeof(io_uring_sqe);
    sq.sqes = reinterpret_cast<sq_entry *>(__sys_mmap(
        nullptr, sqes_size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE,
        fd, IORING_OFF_SQES
    ));
    if (sq.sqes == MAP_FAILED) /*NOLINT*/ [[unlikely]] {
        unmap_rings();
        throw std::system_error{
            errno, std::system_category(), "sq.sqes MAP_FAILED"
        };
    }

    cq.set_offset(p.cq_off);
}

template<uint64_t uring_flags>
inline void uring<uring_flags>::unmap_rings() noexcept {
    __sys_munmap(sq.ring_ptr, sq.ring_sz);
    if (cq.ring_ptr && cq.ring_ptr != sq.ring_ptr) {
        __sys_munmap(cq.ring_ptr, cq.ring_sz);
    }
}

template<uint64_t uring_flags>
inline constexpr bool uring<uring_flags>::is_sq_ring_need_enter(
    unsigned submit, unsigned &enter_flags
) const noexcept {
    if (submit == 0) {
        return false;
    }

    if constexpr (!(uring_flags & IORING_SETUP_SQPOLL)) {
        return true;
    }

    /*
     * Ensure the kernel can see the store to the SQ tail before we read
     * the flags.
     * See https://github.com/axboe/liburing/issues/541
     */
    io_uring_smp_mb();

    if (IO_URING_READ_ONCE(*sq.kflags) & IORING_SQ_NEED_WAKEUP) [[unlikely]] {
        enter_flags |= IORING_ENTER_SQ_WAKEUP;
        return true;
    }

    return false;
}

template<uint64_t uring_flags>
inline bool uring<uring_flags>::is_cq_ring_need_flush() const noexcept {
    return IO_URING_READ_ONCE(*sq.kflags)
           & (IORING_SQ_CQ_OVERFLOW | IORING_SQ_TASKRUN);
}

template<uint64_t uring_flags>
inline constexpr bool
uring<uring_flags>::is_cq_ring_need_enter() const noexcept {
    if constexpr (uring_flags & IORING_SETUP_IOPOLL) {
        return true;
    } else {
        return is_cq_ring_need_flush();
    }
}

template<uint64_t uring_flags>
inline void uring<uring_flags>::cq_advance(unsigned num) noexcept {
    assert(num > 0 && "cq_advance: num must be positive.");
    io_uring_smp_store_release(cq.khead, *cq.khead + num);
}

template<uint64_t uring_flags>
inline void
uring<uring_flags>::buf_ring_cq_advance(buf_ring &br, unsigned count) noexcept {
    br.tail += count;
    cq_advance(count);
}

/*
 * Internal helper, don't use directly in applications.
 */
template<uint64_t uring_flags>
__peek_cq_entry_return_type uring<uring_flags>::__peek_cq_entry() noexcept {
    __peek_cq_entry_return_type ret;
    ret.err = 0;

    while (true) {
        const unsigned tail = io_uring_smp_load_acquire(cq.ktail);
        const unsigned head = *cq.khead;

        ret.cqe = nullptr;
        ret.available_num = tail - head;
        if (ret.available_num == 0) {
            break;
        }

        ret.cqe = &cq.cqe_at<uring_flags>(head);
        if (!(this->features & IORING_FEAT_EXT_ARG)
            && ret.cqe->user_data == LIBURING_UDATA_TIMEOUT) [[unlikely]] {
            if (ret.cqe->res < 0) [[unlikely]] {
                ret.err = ret.cqe->res;
            }
            cq_advance(1);
            if (ret.err == 0) {
                continue;
            }
            ret.cqe = nullptr;
        }

        break;
    }

    return ret;
}

template<uint64_t uring_flags>
template<bool has_ts>
int uring<uring_flags>::_get_cq_entry /*NOLINT*/ (
    const cq_entry *(&cqe_ptr), detail::cq_entry_getter &data
) noexcept {
    __peek_cq_entry_return_type peek_result;
    bool is_looped = false;
    int err = 0;

    do {
        bool is_need_enter = false;
        unsigned flags = 0;

        peek_result = __peek_cq_entry();

        if (peek_result.err != 0) [[unlikely]] {
            if (err == 0) {
                err = peek_result.err;
            }
            break;
        }

        if (peek_result.cqe == nullptr && data.wait_num == 0
            && data.submit == 0) {
            /*
             * If we already looped once, we already entererd
             * the kernel. Since there's nothing to submit or
             * wait for, don't keep retrying.
             */
            if (is_looped || !is_cq_ring_need_enter()) {
                if (err == 0) {
                    err = -EAGAIN;
                }
                break;
            }
            is_need_enter = true;
        }
        if (data.wait_num > peek_result.available_num || is_need_enter) {
            flags = IORING_ENTER_GETEVENTS | data.get_flags;
            is_need_enter = true;
        }
        if (is_sq_ring_need_enter(data.submit, flags)) {
            is_need_enter = true;
        }
        if (!is_need_enter) {
            break;
        }
        if constexpr (has_ts) {
            if (is_looped) {
                const auto &arg =
                    *static_cast<io_uring_getevents_arg *>(data.arg);
                if (peek_result.cqe == nullptr && arg.ts != 0 && err == 0) {
                    err = -ETIME;
                }
                break;
            }
        }

        // HACK this assumes app will use registered ring.
        // if (this->int_flags & INT_FLAG_REG_RING)
        //     flags |= IORING_ENTER_REGISTERED_RING;
        if constexpr (config::using_register_ring_fd) {
            flags |= IORING_ENTER_REGISTERED_RING;
        }

        const int result = __sys_io_uring_enter2(
            enter_ring_fd, data.submit, data.wait_num, flags,
            (sigset_t *)data.arg, data.size
        );

        if (result < 0) [[unlikely]] {
            if (err == 0) {
                err = -result;
            }
            break;
        }
        data.submit -= result;
        if (peek_result.cqe != nullptr) {
            break;
        }
        if (!is_looped) {
            is_looped = true;
            err = result;
        }
    } while (true);

    cqe_ptr = peek_result.cqe;
    return err;
}

template<uint64_t uring_flags>
inline int uring<uring_flags>::__get_cq_entry(
    const cq_entry *(&cqe_ptr),
    unsigned submit,
    unsigned wait_num,
    sigset_t *sigmask
) noexcept {
    detail::cq_entry_getter data{
        .submit = submit,
        .wait_num = wait_num,
        .get_flags = 0,
        .size = _NSIG / 8,
        .arg = sigmask
    };
    return _get_cq_entry<false>(cqe_ptr, data);
}

#if LIBURINGCXX_IS_KERNEL_REACH(5, 11)
/*
 * If we have kernel support for IORING_ENTER_EXT_ARG, then we can use that
 * more efficiently than queueing an internal timeout command.
 */
template<uint64_t uring_flags>
inline int uring<uring_flags>::wait_cq_entries_new(
    const cq_entry *(&cqe_ptr),
    unsigned wait_num,
    const __kernel_timespec &ts,
    sigset_t *sigmask
) noexcept {
    io_uring_getevents_arg arg = {
        .sigmask = (uint64_t)sigmask,
        .sigmask_sz = _NSIG / 8,
        .ts = (uint64_t)(&ts)
    };

    detail::cq_entry_getter data = {
        .wait_num = wait_num,
        .get_flags = IORING_ENTER_EXT_ARG,
        .size = sizeof(arg),
        .arg = &arg
    };

    return _get_cq_entry<true>(cqe_ptr, data);
}
#endif

} // namespace liburingcxx
