cmake_minimum_required(VERSION 3.20.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_C_STANDARD 17)


project(NeedProjectName VERSION 0.1.0 LANGUAGES C CXX)

add_definitions(-DDEBUG)

include_directories(${PROJECT_SOURCE_DIR}/include)

# add_subdirectory(./test)
# add_subdirectory(./example)
# add_subdirectory(./src)

# include(CTest)
# enable_testing()

# set(CPACK_PROJECT_NAME ${PROJECT_NAME})
# set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
# include(CPack)
