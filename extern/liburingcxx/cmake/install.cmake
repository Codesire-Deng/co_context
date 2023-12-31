include(GNUInstallDirs)

install(TARGETS liburingcxx
        EXPORT liburingcxx_targets
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(DIRECTORY "${liburingcxx_SOURCE_DIR}/include/uring"
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(EXPORT liburingcxx_targets
        FILE liburingcxx_targets.cmake
        NAMESPACE liburingcxx::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/liburingcxx
)

include(CMakePackageConfigHelpers)

configure_package_config_file(${liburingcxx_SOURCE_DIR}/cmake/template/Config.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/liburingcxx-config.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/liburingcxx
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/liburingcxx-config-version.cmake"
    COMPATIBILITY SameMinorVersion)

install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/liburingcxx-config.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/liburingcxx-config-version.cmake"
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/liburingcxx
)

# uninstall target
if(NOT TARGET uninstall)
  configure_file(
    "${liburingcxx_SOURCE_DIR}/cmake/template/cmake_uninstall.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake"
    IMMEDIATE @ONLY)

  add_custom_target(uninstall
    COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake)
endif()
