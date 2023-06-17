# ----------------------------------------------------------------------------
#   Uninstall target, for "make uninstall"
# ----------------------------------------------------------------------------
#if(NOT TARGET uninstall)
#    configure_file(
#        "${CMAKE_CURRENT_LIST_DIR}/templates/cmake_uninstall.cmake.in"
#        "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake"
#        @ONLY
#    )
#    add_custom_target(
#        uninstall
#        COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake"
#    )
#endif()

# ----------------------------------------------------------------------------
#   Build unit tests,performance and coverage test.
# ----------------------------------------------------------------------------
if(ENABLE_COVERAGE_TEST)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(co_context
            PUBLIC -fprofile-arcs
            PUBLIC -ftest-coverage
            PUBLIC --coverage)
    else()
        target_compile_options(co_context
            PUBLIC -fprofile-instr-generate
            PUBLIC -fcoverage-mapping)
    endif()
endif()

if(BUILD_TEST)
    enable_testing()
    add_subdirectory(${PROJECT_SOURCE_DIR}/test)
endif()

if(BUILD_PERF_TEST)
    enable_testing()
endif()

# ----------------------------------------------------------------------------
#   Examples
# ----------------------------------------------------------------------------
if(BUILD_EXAMPLE)
    add_subdirectory(${PROJECT_SOURCE_DIR}/example)
endif()
