/* SPDX-License-Identifier: MIT */
#pragma once

#include <cerrno>
#include <csignal>
#include <cstdint>
#include <unistd.h>
#include <cstdbool>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/resource.h>

/*
 * Don't put this below the #include "arch/$arch/syscall.h", that
 * file may need it.
 */
struct io_uring_params;

static inline void *ERR_PTR(intptr_t n) {
    return (void *)n;
}

static inline int PTR_ERR(const void *ptr) {
    return (int)(intptr_t)ptr;
}

static inline bool IS_ERR(const void *ptr) {
    return uintptr_t(ptr) >= uintptr_t(-4095UL);
}

#if defined(__x86_64__) || defined(__i386__)
#include "arch/x86/syscall.h"
#elif defined(__aarch64__)
#include "arch/aarch64/syscall.h"
#else
/*
 * We don't have native syscall wrappers
 * for this arch. Must use libc!
 */
#ifdef CONFIG_NOLIBC
#error "This arch doesn't support building liburing without libc"
#endif
/* libc syscall wrappers. */
#include "arch/generic/syscall.h"
#endif
