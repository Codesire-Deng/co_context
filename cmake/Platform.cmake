# ----------------------------------------------------------------------------
#   Linux
# ----------------------------------------------------------------------------
# Get the linux kernel version to liburingcxx
message(STATUS "target_kernel_version = ${CMAKE_SYSTEM_VERSION}")
set(kernel_version ${CMAKE_SYSTEM_VERSION})
string(REGEX MATCH "^([0-9]+)\.([0-9]+)" kernel_version ${kernel_version})
string(REGEX MATCH "^([0-9]+)" kernel_version_major ${kernel_version})
string(REGEX REPLACE "^([0-9]+)\\." "" kernel_version_minor ${kernel_version})
message(STATUS "kernel_version_major = ${kernel_version_major}")
message(STATUS "kernel_version_minor = ${kernel_version_minor}")
target_compile_definitions(co_context
    PUBLIC "$<BUILD_INTERFACE:LIBURINGCXX_KERNEL_VERSION_MAJOR=${kernel_version_major}>"
    PUBLIC "$<BUILD_INTERFACE:LIBURINGCXX_KERNEL_VERSION_MINOR=${kernel_version_minor}>"
)
unset(kernel_version)

# ----------------------------------------------------------------------------
#   Gcc
# ----------------------------------------------------------------------------

# ----------------------------------------------------------------------------
#   Clang
# ----------------------------------------------------------------------------
if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(co_context PUBLIC -fsized-deallocation)
endif()

if(WITH_LIBCXX AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(co_context PRIVATE -stdlib=libc++)
    target_link_options(co_context
                        PRIVATE -stdlib=libc++
                        PRIVATE -lc++abi)
endif()
