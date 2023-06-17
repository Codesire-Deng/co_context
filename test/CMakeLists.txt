# for each "test/x.cpp", generate "x"
file(GLOB_RECURSE all_tests *.cpp)
foreach(test ${all_tests})
    get_filename_component(target_name ${test} NAME_WE)

    add_executable(${target_name} ${test})
    target_link_libraries(${target_name} PRIVATE co_context)
endforeach()
