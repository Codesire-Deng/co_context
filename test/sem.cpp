#include "co_context.hpp"
#include "co_context/co/semaphore.hpp"
#include "co_context/eager_io.hpp"
#include <iostream>

using namespace co_context;

std::atomic_int32_t cnt{0};

semaphore sem{10};

main_task factory() {
    log::v("factory() running\n");
    for (int i = 0; i < 10000; ++i) {
        log::v("factory acquiring\n");
        co_await sem.acquire();
        log::d("factory &sem=%lx\n", &sem);
        log::v("factory acquired\n");
        co_spawn([]() -> main_task {
            log::d("internal &se=%lx\n", &sem);
            cnt.fetch_add(1, std::memory_order_relaxed);
            std::cout << cnt << std::endl;
            sem.release();
            co_await eager::nop();
        }());
    }
}

int main() {
    io_context ctx{32};
    ctx.co_spawn(factory());
    ctx.run();
    return 0;
}
