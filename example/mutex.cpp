#include "co_context/io_context.hpp"
#include "co_context/co/mutex.hpp"
#include <iostream>

using namespace co_context;
co_context::mutex mtx;
int cnt = 0;

task<> add() {
    auto lock = co_await mtx.lock_guard();
    for (int i = 0; i < 1000000; ++i) {
        ++cnt;
    }
    std::cout << cnt << std::endl;
}

int main() {
    io_context ctx[10];
    for (int i = 0; i < 1000; ++i) {
        ctx[i % 10].co_spawn(add());
    }
    for (auto &c : ctx) {
        c.start();
    }

    ctx[0].join(); // never stop
    return 0;
}
