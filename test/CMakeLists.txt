set(co_context_tests
        cat
        coro_lifetime
        httpd
        httpd_MT
        pingpong_client
        streaming_client
        send_client
        recv_perf
        timer_accuracy
        liburingcxx_netcat
        move_shared_task
        mpl_type_list
        generator_test
)

foreach(test_target ${co_context_tests})
    add_executable(${test_target} ${test_target}.cpp)
    target_link_libraries(${test_target} PRIVATE co_context)
endforeach()

set(liburing_tests
        liburing_accept
        liburing_netcat
)

find_library(liburing uring)
if (NOT ${liburing} STREQUAL "liburing-NOTFOUND")
    message(NOTICE "liburing = ${liburing}")
    foreach(test_target ${liburing_tests})
        add_executable(${test_target} ${test_target}.cpp)
        target_link_libraries(${test_target} PRIVATE ${liburing} co_context)
    endforeach()
endif()
