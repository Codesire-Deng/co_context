# ----------------------------------------------------------------------------
#   check options
# ----------------------------------------------------------------------------
include(${PROJECT_SOURCE_DIR}/cmake/check/check_compile.cmake)
include(${PROJECT_SOURCE_DIR}/cmake/check/check_system.cmake)

# ----------------------------------------------------------------------------
#   compile options
# ----------------------------------------------------------------------------
target_compile_features(co_context PUBLIC cxx_std_20)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release")
    message(NOTICE "Setting default CMAKE_BUILD_TYPE to Release")
endif()

if(CMAKE_BUILD_TYPE MATCHES Release AND NOT CMAKE_CROSSCOMPILING)
    target_compile_options(co_context PUBLIC -march=native)
endif()

# Optional IPO/LTO.
include(CheckIPOSupported)
check_ipo_supported(RESULT is_support_IPO OUTPUT output_support_IPO)
if(is_support_IPO)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
    set_target_properties(co_context PROPERTIES INTERPROCEDURAL_OPTIMIZATION TRUE)
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
    set(CO_CONTEXT_USE_MIMALLOC ON)
    message(NOTICE "mimalloc ${mi_version} enabled")
else()
    set(CO_CONTEXT_USE_MIMALLOC OFF)
    message(WARNING "mimalloc disabled")
endif()

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
target_link_libraries(co_context PUBLIC Threads::Threads)

if (CO_CONTEXT_USE_MIMALLOC)
    target_link_libraries(co_context PUBLIC mimalloc)
endif()
