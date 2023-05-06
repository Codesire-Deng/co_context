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
            add_compile_options(-fsanitize=undefined,address,leak
                                -fno-omit-frame-pointer)
        else()
            message(WARNING "sanitizer is no supported with current tool-chains")
        endif()
    else()
        message(WARNING "Sanitizer supported only for debug type")
    endif()
endif()

if(ENABLE_WARNING)
    add_compile_options(-Wall
                        -Wextra
                        # -Wconversion
                        # -pedantic
                        # -Werror
                        # -Wfatal-errors
                        )
endif()
