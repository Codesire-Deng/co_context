# for each "example/x.cpp", generate "x"
file(GLOB_RECURSE all_examples *.cpp)
foreach(example ${all_examples})
    get_filename_component(target_name ${example} NAME_WE)

    add_executable(${target_name} ${example})
    target_link_libraries(${target_name} PRIVATE co_context)
endforeach()
