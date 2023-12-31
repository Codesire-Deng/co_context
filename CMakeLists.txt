cmake_minimum_required(VERSION 3.13)

project(co_context
        VERSION 0.10.0
        LANGUAGES CXX)

include(cmake/Policy.cmake NO_POLICY_SCOPE)

add_subdirectory(extern/liburingcxx)

add_library(co_context STATIC)
add_library(co_context::co_context ALIAS co_context)
target_link_libraries(co_context PUBLIC liburingcxx::liburingcxx)
target_include_directories(co_context
                           PUBLIC
                           "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
                           "$<INSTALL_INTERFACE:include>"
)

include(cmake/Option.cmake)
include(cmake/CompileOption.cmake)
include(cmake/Platform.cmake)
include(cmake/Develop.cmake)
include(cmake/Extra.cmake)
include(cmake/Configure.cmake)
include(cmake/Install.cmake)

add_subdirectory(lib)
