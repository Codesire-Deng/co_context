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

task<S &> coro_ref(S x) {
    co_return x;
}

task<> coro(S s) {
    printf("coro_copy:\n");
    S res0 = co_await coro_copy(s);
    printf("coro_ref:\n");
    S res1 = co_await coro_ref(s);
    printf("coro finished\n");
    co_return;
}

int main() {
    io_context ctx;
    ctx.co_spawn(coro(S{1}));
    ctx.run();
    return 0;
}