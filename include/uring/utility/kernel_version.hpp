#pragma once

#ifndef LIBURINGCXX_KERNEL_VERSION_MAJOR
#error Fail to detect kernel version. Please check CMakeLists.txt (at root level).
#endif

#ifndef LIBURINGCXX_KERNEL_VERSION_MINOR
#error Fail to detect kernel version. Please check CMakeLists.txt (at root level).
#endif

namespace liburingcxx {

consteval bool is_kernel_reach(int major, int minor) noexcept {
    if (LIBURINGCXX_KERNEL_VERSION_MAJOR != major) {
        return LIBURINGCXX_KERNEL_VERSION_MAJOR > major;
    }
    return LIBURINGCXX_KERNEL_VERSION_MINOR >= minor;
}

} // namespace liburingcxx

#define LIBURINGCXX_IS_KERNEL_REACH(major, minor)    \
    ((LIBURINGCXX_KERNEL_VERSION_MAJOR > (major))    \
     || (LIBURINGCXX_KERNEL_VERSION_MAJOR == (major) \
         && LIBURINGCXX_KERNEL_VERSION_MINOR >= (minor)))
