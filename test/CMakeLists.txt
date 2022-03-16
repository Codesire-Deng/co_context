add_executable(cat cat.cpp)

add_executable(httpd httpd.cpp)

add_executable(main main.cpp)
target_link_libraries(main PUBLIC mimalloc)

add_executable(netcat netcat.cpp)
target_link_libraries(netcat PUBLIC mimalloc)