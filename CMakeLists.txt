cmake_minimum_required(VERSION 3.20.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
option(FORCE_TO_USE_MIMALLOC "Force the use of mimalloc version 2.0 or above" OFF)
option(FORCE_TO_USE_LIBCXX "Force the use of LLVM libc++. Requires clang." OFF)
option(BUILD_EXAMPLES "Build the examples" ON)
option(BUILD_TESTS "Build the tests" OFF)

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
    message("Setting default CMAKE_BUILD_TYPE to Release")
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(co_context VERSION 0.8.2 LANGUAGES CXX)

if (NOT "${CMAKE_SYSTEM_NAME}" STREQUAL "Linux")
    message(WARNING "co_context only supports Linux currently, but the target OS is ${CMAKE_SYSTEM_NAME}.")
endif()

# Get the linux kernel version to liburingcxx
message("target_kernel_version = ${CMAKE_SYSTEM_VERSION}")
set(kernel_version ${CMAKE_SYSTEM_VERSION})
string(REGEX MATCH "^([0-9]+)\.([0-9]+)" kernel_version ${kernel_version})
string(REGEX MATCH "^([0-9]+)" kernel_version_major ${kernel_version})
string(REGEX REPLACE "^([0-9]+)\\." "" kernel_version_patchlevel ${kernel_version})
message("kernel_version_major = ${kernel_version_major}")
message("kernel_version_patchlevel = ${kernel_version_patchlevel}")
add_compile_definitions(LIBURINGCXX_KERNEL_VERSION_MAJOR=${kernel_version_major})
add_compile_definitions(LIBURINGCXX_KERNEL_VERSION_PATCHLEVEL=${kernel_version_patchlevel})

# Optional IPO/LTO.
include(CheckIPOSupported)
check_ipo_supported(RESULT is_support_IPO OUTPUT output_support_IPO)
if(is_support_IPO)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
else()
    message(WARNING "IPO is not supported: ${output_support_IPO}")
endif()

if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS "11.3.0")
        message(FATAL_ERROR "Insufficient gcc version, requires gcc 11.3 or above")
    endif()

    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall")

    if (${CMAKE_BUILD_TYPE} MATCHES Debug)
        message("Enable -fsanitize for gcc")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=undefined,address,leak -fno-omit-frame-pointer")
    else()
        message(${CMAKE_BUILD_TYPE})
        message("Disable -fsanitize for gcc")
    endif()

    if (${CMAKE_BUILD_TYPE} MATCHES Release)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=native")
    endif()

    message("CMAKE_CXX_FLAGS = ${CMAKE_CXX_FLAGS}")

elseif ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS "14.0.0")
        message(FATAL_ERROR "Insufficient clang version, requires clang 14.0 or above")
    endif()

    if (FORCE_TO_USE_LIBCXX)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libc++")
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -stdlib=libc++ -lc++abi")    
    endif()

    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsized-deallocation -std=c++20")

    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall")

    if (${CMAKE_BUILD_TYPE} MATCHES Debug)
        message("Enable -fsanitize for clang")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=undefined,address")
    else()
        message(${CMAKE_BUILD_TYPE})
        message("Disable -fsanitize for clang")
    endif()

    if (${CMAKE_BUILD_TYPE} MATCHES Release)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=native")
    endif()

    message("CMAKE_CXX_FLAGS = ${CMAKE_CXX_FLAGS}")
else()
    message(FATAL_ERROR "[g++/clang] is required, but the compiler id is ${CMAKE_CXX_COMPILER_ID}.")
endif()

if (FORCE_TO_USE_MIMALLOC)
    find_package(mimalloc 2.0 REQUIRED)
else()
    find_package(mimalloc QUIET)
endif()

if (mi_version)
    add_compile_definitions(USE_MIMALLOC)
    message(NOTICE "mimalloc ${mi_version} enabled")
else()
    message(WARNING "mimalloc disabled")
endif()

include_directories(${PROJECT_SOURCE_DIR}/include)

add_subdirectory(./lib)

if(BUILD_TESTS)
    add_subdirectory(./test)
endif()

if(BUILD_EXAMPLES)
    add_subdirectory(./example)
endif()
