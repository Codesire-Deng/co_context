# ----------------------------------------------------------------------------
#   Linux
# ----------------------------------------------------------------------------

# ----------------------------------------------------------------------------
#   Gcc
# ----------------------------------------------------------------------------

# ----------------------------------------------------------------------------
#   Clang
# ----------------------------------------------------------------------------
if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(co_context PUBLIC -fsized-deallocation)
endif()

if(WITH_LIBCXX AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(co_context PRIVATE -stdlib=libc++)
    target_link_options(co_context
                        PRIVATE -stdlib=libc++
                        PRIVATE -lc++abi)
endif()
