cmake_minimum_required(VERSION 3.20)

project(co_context
        VERSION 0.8.3
        LANGUAGES CXX)

include_directories(./include)

include(cmake/Option.cmake)
include(cmake/CompileOption.cmake)
include(cmake/Platform.cmake)
include(cmake/Develop.cmake)
include(cmake/Extra.cmake)

add_subdirectory(./lib)
