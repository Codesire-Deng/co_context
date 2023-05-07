if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    message(FATAL_ERROR "[g++/clang] is required, but the compiler id is ${CMAKE_CXX_COMPILER_ID}.${CMAKE_CXX_COMPILER_ID} is not supported now")
endif()

if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS "11.3.0")
    message(FATAL_ERROR "Insufficient gcc version, requires gcc 11.3 or above")
endif()

if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS "14.0.0")
    message(FATAL_ERROR "Insufficient clang version, requires clang 14.0 or above")
endif()
