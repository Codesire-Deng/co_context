# ----------------------------------------------------------------------------
#   check options
# ----------------------------------------------------------------------------
include(${PROJECT_SOURCE_DIR}/cmake/check/check_compile.cmake)
include(${PROJECT_SOURCE_DIR}/cmake/check/check_system.cmake)

# ----------------------------------------------------------------------------
#   compile options
# ----------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release")
    message(NOTICE "Setting default CMAKE_BUILD_TYPE to Release")
endif()

set(CMAKE_CXX_FLAGS_RELEASE)
if(CMAKE_BUILD_TYPE MATCHES Release)
    add_compile_options(-march=native)
endif()

# Get the linux kernel version to liburingcxx
message(STATUS "target_kernel_version = ${CMAKE_SYSTEM_VERSION}")
set(kernel_version ${CMAKE_SYSTEM_VERSION})
string(REGEX MATCH "^([0-9]+)\.([0-9]+)" kernel_version ${kernel_version})
string(REGEX MATCH "^([0-9]+)" kernel_version_major ${kernel_version})
string(REGEX REPLACE "^([0-9]+)\\." "" kernel_version_minor ${kernel_version})
message(STATUS "kernel_version_major = ${kernel_version_major}")
message(STATUS "kernel_version_minor = ${kernel_version_minor}")
add_compile_definitions(LIBURINGCXX_KERNEL_VERSION_MAJOR=${kernel_version_major})
add_compile_definitions(LIBURINGCXX_KERNEL_VERSION_MINOR=${kernel_version_minor})
unset(kernel_version)

# Optional IPO/LTO.
include(CheckIPOSupported)
check_ipo_supported(RESULT is_support_IPO OUTPUT output_support_IPO)
if(is_support_IPO)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
else()
    message(WARNING "IPO is not supported: ${output_support_IPO}")
endif()

# Optional mimalloc.
if(WITH_MIMALLOC)
    find_package(mimalloc 2.0 REQUIRED)
else()
    find_package(mimalloc QUIET)
endif()

if (mi_version)
    add_compile_definitions(CO_CONTEXT_USE_MIMALLOC)
    set(USE_MIMALLOC ON)
    message(NOTICE "mimalloc ${mi_version} enabled")
else()
    set(USE_MIMALLOC OFF)
    message(WARNING "mimalloc disabled")
endif()
