#include "co_context/all.hpp"
using namespace co_context;

struct S {
    int x;

    S(int x) : x(x) { printf("S(%d)\n", x); }

    S(const S &s) : x(s.x * 10) { printf("S(copy S(%d)) -> %d\n", s.x, x); }

    S(S &&s) : x(s.x * 10) { printf("S(move S(%d)) -> %d\n", s.x, x); }

    ~S() { printf("~S(%d)\n", x); }
};

task<S> coro_copy(S x) {
    co_return x;
}

/* Error
task<S &> coro_ref(S x) {
    co_return x; // Dangling reference!
}
*/

task<> coro(S s) {
    printf("coro_copy:\n");
    S res0 = co_await coro_copy(s);
    printf("coro finished\n");
    co_return;
}

int main() {
    io_context ctx;
    ctx.co_spawn(coro(S{1}));
    ctx.start();
    ctx.join();
    return 0;
}