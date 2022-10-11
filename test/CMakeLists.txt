# for each "test/x.cpp", generate target "x"
file(GLOB_RECURSE all_tests *.cpp)
foreach(v ${all_tests})
    string(REGEX MATCH "test/.*" relative_path ${v})
    # message(${relative_path})
    string(REGEX REPLACE "test/" "" target_name ${relative_path})
    string(REGEX REPLACE ".cpp" "" target_name ${target_name})

    add_executable(${target_name} ${v} $<TARGET_OBJECTS:co_context>)
    if (mi_version)
        target_link_libraries(${target_name} PUBLIC mimalloc)
    endif()
    # target_link_libraries(${target_name} PUBLIC uring) // link axboe/liburing
endforeach()
