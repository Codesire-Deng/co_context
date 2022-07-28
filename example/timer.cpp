#include "co_context/io_context.hpp"
#include "co_context/lazy_io.hpp"
using namespace co_context;

task<> cycle(int sec, const char * message) {
    while (true) {
        co_await timeout(std::chrono::seconds{sec});
        printf("%s\n", message);
    }
}

int main() {
    io_context ctx;
    ctx.co_spawn(cycle(1, "1 sec"));
    ctx.co_spawn(cycle(3, "\t3 sec"));
    ctx.run();
    return 0;
}