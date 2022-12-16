#include "co_context/all.hpp"
#include "co_context/lazy_io.hpp"
#include "co_context/utility/polymorphism.hpp"
#include <iostream>
#include <variant>
using namespace co_context;

task<int> f1() {
    printf("f1 start.\n");
    co_await timeout(std::chrono::seconds{1});
    printf("f1 done.\n");
    co_return 1;
}

task<const char *> f2() {
    printf("f2 done.\n");
    co_return "f2 Great!";
}

task<void> f3() {
    printf("f3 start.\n");
    co_await timeout(std::chrono::seconds{2});
    printf("f3 done.\n");
    co_return;
}

task<> run() {
    // var : std::variant<std::monostate, int, const char *>;
    auto [idx, var] = co_await any(f1(), f2(), f3());
    std::cout << idx << " finished!\n";
    std::visit(
        overloaded{
            [](std::monostate) { std::cout << "impossible\n"; },
            [](int x) { std::cout << x << " : int\n"; },
            [](const char *s) { std::cout << s << " : string\n"; },
        },
        var
    );
}

int main() {
    io_context ctx;
    ctx.co_spawn(run());
    ctx.start();
    ctx.join();
    return 0;
}