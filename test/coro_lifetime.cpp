#include "co_context/all.hpp"
using namespace co_context;

struct S {
    int x;

    S(int x) : x(x) { printf("S(%d)\n", x); }

    S(const S &s) : x(s.x * 10) { printf("S(copy S(%d)) -> %d\n", s.x, x); }

    S(S &&s) : x(s.x * 10) { printf("S(move S(%d)) -> %d\n", s.x, x); }

    ~S() { printf("~S(%d)\n", x); }
};

task<> coro(S s) {
    printf("coro running\n");
    printf("coro finished\n");
    co_return;
}

int main() {
    io_context ctx;
    ctx.co_spawn(coro(S{1}));
    ctx.run();
    return 0;
}