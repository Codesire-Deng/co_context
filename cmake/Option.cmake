# ----------------------------------------------------------------------------
#   Build options
# ----------------------------------------------------------------------------
option(WITH_LIBCXX "Force to build with LLVM libc++. Clang only" OFF)
option(WITH_MIMALLOC "Force to build with mimalloc version >= 2.0" OFF)

# ----------------------------------------------------------------------------
#   Develop options
# ----------------------------------------------------------------------------
option(ENABLE_SANITIZER "Enable address sanitizer(Debug+Gcc/Clang/AppleClang)" OFF)
option(ENABLE_WARNING "Enable warning for all project " OFF)
option(ENABLE_CCACHE "Use ccache to faster compile when develop" OFF)
option(ENABLE_COVERAGE_TEST "Build test with coverage" OFF)

# ----------------------------------------------------------------------------
#   Extra options
# ----------------------------------------------------------------------------
option(CMAKE_EXPORT_COMPILE_COMMANDS "Generate compile commands" ON)
option(BUILD_EXAMPLE "Build examples" ON)
option(BUILD_TEST "Build tests" OFF)
option(BUILD_PERF_TEST "Build benchmark" OFF)


