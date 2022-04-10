cmake_minimum_required(VERSION 3.20.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 17)

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
    message("Setting default build type to Release")
endif()

if (${CMAKE_BUILD_TYPE} MATCHES Debug)
    message("Enable -fsanitize for gcc")
    set(CMAKE_CXX_FLAGS "-fsanitize=undefined,address,leak -fno-omit-frame-pointer")
else()
    message(${CMAKE_BUILD_TYPE})
    message("Disable -fsanitize for gcc")
endif()


project(co_context VERSION 0.2.0 LANGUAGES C CXX)

include_directories(${PROJECT_SOURCE_DIR}/include)

find_package(mimalloc 2.0 REQUIRED)

add_subdirectory(./lib)

add_subdirectory(./test)
add_subdirectory(./example)
# add_subdirectory(./src)

# include(CTest)
# enable_testing()

# set(CPACK_PROJECT_NAME ${PROJECT_NAME})
# set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
# include(CPack)
