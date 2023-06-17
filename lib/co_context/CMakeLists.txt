file(GLOB_RECURSE sources CONFIGURE_DEPENDS *.cpp)

target_sources(co_context PRIVATE ${sources})
