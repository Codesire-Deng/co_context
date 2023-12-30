if(ENABLE_CCACHE)
    find_program(CCACHE-FOUND ccache)
    if(CCACHE-FOUND)
        set(CMAKE_CXX_COMPILER_LAUNCHER ccache)
        set(CMAKE_C_COMPILER_LAUNCHER ccache)
    endif()
endif()

if(ENABLE_SANITIZER AND NOT MSVC)
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        include(${CMAKE_CURRENT_LIST_DIR}/check/check_asan.cmake)
        check_asan(HAS_ASAN)
        if(HAS_ASAN)
            target_compile_options(co_context
                PUBLIC -fsanitize=undefined,address,leak
                PUBLIC -fno-omit-frame-pointer)
            target_link_options(co_context
                PUBLIC -fsanitize=undefined,address,leak)
        else()
            message(WARNING "sanitizer is no supported with current tool-chains")
        endif()
    else()
        message(WARNING "Sanitizer supported only for debug type")
    endif()
endif()

if(ENABLE_WARNING)
    target_compile_options(co_context
                        PRIVATE -Wall
                        PRIVATE -Wextra
                        # -Wconversion
                        # -pedantic
                        # -Werror
                        # -Wfatal-errors
                        )
endif()
