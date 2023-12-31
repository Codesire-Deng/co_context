# liburingcxx

A header-only C++ implementation of axboe/liburing. Reordered submissions are supported.
Higher performance is achieved by compile-time configure.

axboe/liburing 的 header-only C++ 实现，与原版不同，支持了 sqe 的乱序申请和提交，以更灵活地支持多线程应用；使用编译期配置，以提高运行性能。

# Installation(Optional)

You may install this library to enable `find_package(liburingcxx)` for other CMake projects.

```bash
cmake -B build .
sudo cmake --build build --target install
```

# Usage

With CMake:
1. do `find_package(liburingcxx)` or `add_subdirectory(liburingcxx)`;
2. link `liburingcxx::liburingcxx` to your target, e.g. `target_link_libraries(my_target PUBLIC liburingcxx::liburingcxx)`.
