include(CheckCXXSourceRuns)

if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    message(FATAL_ERROR "[g++/clang] is required, but the compiler id is ${CMAKE_CXX_COMPILER_ID}.${CMAKE_CXX_COMPILER_ID} is not supported now")
endif()

if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
    if (CMAKE_CXX_COMPILER_VERSION VERSION_LESS "11.2.0")
        message(FATAL_ERROR "Insufficient gcc version, requires gcc 11.2 or above")
    endif()
    if (CMAKE_CXX_COMPILER_VERSION VERSION_LESS "11.3.0")
        message(NOTICE "co_context::generator will be disabled for insufficient gcc version (requires gcc 11.3 or above).")
        set(co_context_no_generator ON)
    endif()
endif()

if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
    if (CMAKE_CXX_COMPILER_VERSION VERSION_LESS "14.0.0")
        message(FATAL_ERROR "Insufficient clang version, requires clang 14.0 or above")
    endif()
    if (CMAKE_CXX_COMPILER_VERSION VERSION_LESS "17.0.0")
        message(NOTICE "co_context::generator will be disabled for insufficient clang version (requires clang 17 or above).")
        set(co_context_no_generator ON)
    endif()
endif()

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
    co_context_has_kernel_rwf_t
)
