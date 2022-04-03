#pragma once

#include <concepts>
#include <chrono>
#include "co_context/log/log.hpp"

template<std::invocable F>
void hostTiming(const F &func) {
    auto start = std::chrono::steady_clock::now();

    func();

    auto end = std::chrono::steady_clock::now();
    std::chrono::duration<double, std::milli> duration = end - start;
    printf("Host Time = %g ms.\n", duration.count());
}