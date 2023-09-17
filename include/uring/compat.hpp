#pragma once

#include <linux/time_types.h>

#if __has_include(<linux/openat2.h>)
#include <linux/openat2.h>
#define LIBURINGCXX_HAS_OPENAT2
#endif
