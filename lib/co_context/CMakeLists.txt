include_directories(${PROJECT_SOURCE_DIR}/include)
file(GLOB_RECURSE sources CONFIGURE_DEPENDS *.cpp)

add_library(co_context OBJECT ${sources})

if (mi_version)
    target_link_libraries(co_context PUBLIC mimalloc)
endif()