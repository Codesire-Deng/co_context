cmake_minimum_required(VERSION 3.20.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 17)
option(FORCE_TO_USE_MIMALLOC "Force the use of mimalloc version 2.0 or above" OFF)

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
    message("Setting default build type to Release")
endif()

project(co_context VERSION 0.5.0 LANGUAGES C CXX)

if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")

    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall")

    if (${CMAKE_BUILD_TYPE} MATCHES Debug)
        message("Enable -fsanitize for gcc")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=undefined,address,leak -fno-omit-frame-pointer")
    else()
        message(${CMAKE_BUILD_TYPE})
        message("Disable -fsanitize for gcc")
    endif()

    if (${CMAKE_BUILD_TYPE} MATCHES Release)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=native -flto")
    endif()

    message("CMAKE_CXX_FLAGS = ${CMAKE_CXX_FLAGS}")
else()
    message(WARNING "Not using GNU g++ for CXX")
endif()

include_directories(${PROJECT_SOURCE_DIR}/include)

if (FORCE_TO_USE_MIMALLOC)
    find_package(mimalloc 2.0 REQUIRED)
else()
    find_package(mimalloc QUIET)
endif()

if (mi_version)
    add_definitions(-DUSE_MIMALLOC)
    message(NOTICE "mimalloc ${mi_version} enabled")
else()
    message(WARNING "mimalloc disabled")
endif()

add_subdirectory(./lib)

add_subdirectory(./test)

add_subdirectory(./example)
