add_executable(netcat netcat.cpp)
target_link_libraries(netcat PUBLIC mimalloc)

add_executable(fingerprint fingerprint.cpp)
target_link_libraries(fingerprint PUBLIC mimalloc)

add_executable(fingerprint_blocking fingerprint_blocking.cpp)
target_link_libraries(fingerprint_blocking PUBLIC mimalloc)

add_executable(fingerprint_client fingerprint_client.cpp)
target_link_libraries(fingerprint_client PUBLIC mimalloc)

add_executable(rand4kr rand4kr.cpp)
target_link_libraries(rand4kr PUBLIC mimalloc)

add_executable(rand4kr_blocking rand4kr_blocking.cpp)
target_link_libraries(rand4kr_blocking PUBLIC mimalloc)

add_executable(rand4kr_callback rand4kr_callback.cpp)
target_link_libraries(rand4kr_callback PUBLIC mimalloc)

add_executable(rand4kw_callback rand4kw_callback.cpp)
target_link_libraries(rand4kw_callback PUBLIC mimalloc)

add_executable(nop nop.cpp)
target_link_libraries(nop PUBLIC mimalloc)

add_executable(nop_thread nop_thread.cpp)

