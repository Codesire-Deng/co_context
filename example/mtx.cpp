#include "co_context/io_context.hpp"
#include "co_context/co/mutex.hpp"
#include "co_context/eager_io.hpp"
#include <iostream>

using namespace co_context;

co_context::mutex mtx;

int cnt = 0;

task<> add() {
    co_await mtx.lock();
    for (int i = 0; i < 1000000; ++i) ++cnt;
    std::cout << cnt << std::endl;
    mtx.unlock();

    co_await eager::nop();
}

int main() {
    io_context ctx{2048};
    for (int i = 0; i < 1000; ++i) ctx.co_spawn(add());
    ctx.run();
}
