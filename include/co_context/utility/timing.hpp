#pragma once

#include <concepts>
#include <chrono>
#include "co_context/log/log.hpp"

template<std::invocable F>
void hostTiming(const F &func) {
    auto start = std::chrono::steady_clock::now();

    func();

    auto end = std::chrono::steady_clock::now();
    std::chrono::duration<double, std::micro> duration = end - start;
    printf("Host Time = %7.3f us.\n", duration.count());
}

// 2e9 ~ 4138.81ms -> 2.069ns/times
