#include "co_context/all.hpp"

using namespace co_context;
using namespace std;

channel<int, 1> chan;

task<> prod() {
    for (int i = 0;; ++i) {
        co_await timeout(1s);
        co_await chan.release(i);
    }
}

task<> cons() {
    while (true) {
        int x = co_await chan.acquire();
        printf("%d\n", x);
    }
}

int main() {
    io_context ctx;
    ctx.co_spawn(prod());
    ctx.co_spawn(cons());
    ctx.start();
    ctx.join();
    return 0;
}
