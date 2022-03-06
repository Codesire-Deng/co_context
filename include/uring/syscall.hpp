/* SPDX-License-Identifier: MIT */
// #define _DEFAULT_SOURCE

/*
 * Will go away once libc support is there
 */
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <signal.h>

#ifdef __alpha__
/*
 * alpha and mips are exception, other architectures have
 * common numbers for new system calls.
 */
#ifndef __NR_io_uring_setup
#define __NR_io_uring_setup 535
#endif
#ifndef __NR_io_uring_enter
#define __NR_io_uring_enter 536
#endif
#ifndef __NR_io_uring_register
#define __NR_io_uring_register 537
#endif
#elif defined __mips__
#ifndef __NR_io_uring_setup
#define __NR_io_uring_setup (__NR_Linux + 425)
#endif
#ifndef __NR_io_uring_enter
#define __NR_io_uring_enter (__NR_Linux + 426)
#endif
#ifndef __NR_io_uring_register
#define __NR_io_uring_register (__NR_Linux + 427)
#endif
#else /* !__alpha__ and !__mips__ */
#ifndef __NR_io_uring_setup
#define __NR_io_uring_setup 425
#endif
#ifndef __NR_io_uring_enter
#define __NR_io_uring_enter 426
#endif
#ifndef __NR_io_uring_register
#define __NR_io_uring_register 427
#endif
#endif

namespace liburingcxx {

namespace detail {

    inline int __sys_io_uring_register(
        int fd, unsigned opcode, const void *arg, unsigned nr_args) {
        return syscall(__NR_io_uring_register, fd, opcode, arg, nr_args);
    }

    inline int __sys_io_uring_setup(unsigned entries, struct io_uring_params *p) {
        return syscall(__NR_io_uring_setup, entries, p);
    }

    inline int __sys_io_uring_enter2(
        int fd,
        unsigned to_submit,
        unsigned min_complete,
        unsigned flags,
        sigset_t *sig,
        int sz) {
        return syscall(
            __NR_io_uring_enter, fd, to_submit, min_complete, flags, sig, sz);
    }

    inline int __sys_io_uring_enter(
        int fd,
        unsigned to_submit,
        unsigned min_complete,
        unsigned flags,
        sigset_t *sig) {
        return __sys_io_uring_enter2(
            fd, to_submit, min_complete, flags, sig, _NSIG / 8);
    }

} // namespace detail

} // namespace liburingcxx
