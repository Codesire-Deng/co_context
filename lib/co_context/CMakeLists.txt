include_directories(${PROJECT_SOURCE_DIR}/include)
file(GLOB_RECURSE sources CONFIGURE_DEPENDS *.cpp)

add_library(co_context OBJECT ${sources})

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
target_link_libraries(co_context PUBLIC Threads::Threads)

if (USE_MIMALLOC)
    target_link_libraries(co_context PUBLIC mimalloc)
endif()
