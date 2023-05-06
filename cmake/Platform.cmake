# ----------------------------------------------------------------------------
#   Gcc
# ----------------------------------------------------------------------------

# ----------------------------------------------------------------------------
#   Clang
# ----------------------------------------------------------------------------
if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsized-deallocation -std=c++20")
endif()

if(WITH_LIBCXX AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    add_compile_options(-stdlib=libc++)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -stdlib=libc++ -lc++abi")
endif()
