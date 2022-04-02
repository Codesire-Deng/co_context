#include "co_context.hpp"
#include "co_context/co/semaphore.hpp"
#include "co_context/eager_io.hpp"
#include <iostream>

using namespace co_context;

std::atomic_int32_t cnt{0};

main_task factory() {
    semaphore sem{10};
    for (int i=0; i<10000; ++i) {
        co_spawn([&sem]() -> main_task{
            co_await sem.acquire();
            cnt.fetch_add(1, std::memory_order_relaxed);
            sem.release();
        }());
    }
    co_await eager::nop();
}

int main() {
    io_context ctx{32};
    ctx.co_spawn(factory());
    ctx.run();
    return 0;
}
