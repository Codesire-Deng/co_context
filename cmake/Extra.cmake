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
        add_compile_options(-fprofile-arcs -ftest-coverage --coverage)
    else()
        add_compile_options(-fprofile-instr-generate -fcoverage-mapping)
    endif()
endif()

if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(${PROJECT_SOURCE_DIR}/test)
endif()

if(BUILD_PERF_TESTS)
    enable_testing()
endif()

# ----------------------------------------------------------------------------
#   Examples
# ----------------------------------------------------------------------------
if(BUILD_EXAMPLE)
    add_subdirectory(${PROJECT_SOURCE_DIR}/example)
endif()
