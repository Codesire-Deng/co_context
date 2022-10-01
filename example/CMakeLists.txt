# for each "example/x.cpp", generate "x"
file(GLOB_RECURSE all_examples *.cpp)
foreach(v ${all_examples})
    string(REGEX MATCH "example/.*" relative_path ${v})
    # message(${relative_path})
    string(REGEX REPLACE "example/" "" target_name ${relative_path})
    string(REGEX REPLACE "\\.cpp" "" target_name ${target_name})

    add_executable(${target_name} ${v} $<TARGET_OBJECTS:co_context>)
    if (mi_version)
        target_link_libraries(${target_name} PUBLIC mimalloc)
    endif()
endforeach()
