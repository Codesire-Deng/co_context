if (NOT "${CMAKE_SYSTEM_NAME}" STREQUAL "Linux")
    message(FATAL_ERROR "co_context only supports Linux currently, but the target OS is ${CMAKE_SYSTEM_NAME}.")
endif()
