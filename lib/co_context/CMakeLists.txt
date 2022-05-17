
find_package(mimalloc 2.0 REQUIRED)


include_directories(${PROJECT_SOURCE_DIR}/include)
file(GLOB_RECURSE sources CONFIGURE_DEPENDS *.cpp)

add_library(co_context OBJECT ${sources})

target_link_libraries(co_context PUBLIC mimalloc)