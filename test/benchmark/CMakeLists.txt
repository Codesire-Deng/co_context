find_package(benchmark REQUIRED)

# for each "test/benchmark/x.cpp", generate "x"
file(GLOB all_benchmarks CONFIGURE_DEPENDS *.cpp)
foreach(item ${all_benchmarks})
    get_filename_component(target_name ${item} NAME_WE)

    add_executable(${target_name} ${item})
    target_link_libraries(${target_name} PRIVATE co_context)
    target_link_libraries(${target_name} PRIVATE benchmark::benchmark)
endforeach()

add_test(NAME lazy_yield COMMAND lazy_yield)

add_test(NAME co_await COMMAND co_await)
