#pragma once

#include <concepts>
#include <chrono>

template<std::invocable F>
[[maybe_unused]] auto host_timing(const F &func) {
    auto start = std::chrono::steady_clock::now();

    func();

    auto end = std::chrono::steady_clock::now();
    std::chrono::duration<double, std::micro> duration = end - start;
    return duration;
}

// 2e9 ~ 4138.81ms -> 2.069ns/times
