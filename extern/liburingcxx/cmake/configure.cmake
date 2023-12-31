include(CheckCXXSourceRuns)

message(STATUS "target_kernel_version = ${CMAKE_SYSTEM_VERSION}")
set(kernel_version ${CMAKE_SYSTEM_VERSION})
string(REGEX MATCH "^([0-9]+)\.([0-9]+)" kernel_version ${kernel_version})
string(REGEX MATCH "^([0-9]+)" LIBURINGCXX_KERNEL_VERSION_MAJOR ${kernel_version})
string(REGEX REPLACE "^([0-9]+)\\." "" LIBURINGCXX_KERNEL_VERSION_MINOR ${kernel_version})
message(STATUS "LIBURINGCXX_KERNEL_VERSION_MAJOR = ${LIBURINGCXX_KERNEL_VERSION_MAJOR}")
message(STATUS "LIBURINGCXX_KERNEL_VERSION_MINOR = ${LIBURINGCXX_KERNEL_VERSION_MINOR}")
unset(kernel_version)

check_cxx_source_runs(
    [====[
    #include <linux/fs.h>
    int main(int argc, char **argv)
    {
      __kernel_rwf_t x;
      x = 0;
      return x;
    }
    ]====]
    LIBURINGCXX_HAS_KERNEL_RWF_T
)

check_cxx_source_runs(
    [====[
    #include <linux/time_types.h>
    int main(){ return 0; }
    ]====]
    LIBURINGCXX_HAS_TIME_TYPES
)

check_cxx_source_runs(
    [====[
    #include <linux/openat2.h>
    int main(){ return 0; }
    ]====]
    LIBURINGCXX_HAS_OPENAT2
)

configure_file(
    ${PROJECT_SOURCE_DIR}/include/uring/config.hpp.in
    ${PROJECT_SOURCE_DIR}/include/uring/config.hpp
    @ONLY
)
