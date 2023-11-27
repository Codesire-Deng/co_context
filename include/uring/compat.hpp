#pragma once

#ifdef CO_CONTEXT_OFFER_KERNEL_RWF_T
typedef int __kernel_rwf_t;
#endif

#if __has_include(<linux/time_types.h>)
#include <linux/time_types.h>
/* <linux/time_types.h> is included above and not needed again */
#define UAPI_LINUX_IO_URING_H_SKIP_LINUX_TIME_TYPES_H 1
#else
#include <cstdint>

struct __kernel_timespec {
    int64_t tv_sec;
    long long tv_nsec;
};

/* <linux/time_types.h> is not available, so it can't be included */
#define UAPI_LINUX_IO_URING_H_SKIP_LINUX_TIME_TYPES_H 1
#endif

#if __has_include(<linux/openat2.h>)
#include <linux/openat2.h>
#define LIBURINGCXX_HAS_OPENAT2
#endif
