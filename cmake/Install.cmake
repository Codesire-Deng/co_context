include(GNUInstallDirs)

install(TARGETS co_context
        EXPORT co_context_targets
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(DIRECTORY "${co_context_SOURCE_DIR}/include/co_context"
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(EXPORT co_context_targets
        FILE co_context_targets.cmake
        NAMESPACE co_context::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/co_context
)

include(CMakePackageConfigHelpers)

configure_package_config_file(${co_context_SOURCE_DIR}/cmake/templates/Config.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/co_context-config.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/co_context
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/co_context-config-version.cmake"
    COMPATIBILITY SameMinorVersion)

install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/co_context-config.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/co_context-config-version.cmake"
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/co_context
)

# uninstall target
if(NOT TARGET uninstall)
  configure_file(
    "${co_context_SOURCE_DIR}/cmake/templates/cmake_uninstall.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake"
    IMMEDIATE @ONLY)

  add_custom_target(uninstall
    COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake)
endif()
