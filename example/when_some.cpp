#include <co_context/all.hpp>

#include <iostream>
#include <variant>
using namespace co_context;

task<int> f0() {
    printf("f0 start.\n");
    co_await timeout(std::chrono::seconds{1});
    printf("f0 done.\n");
    co_return 0;
}

task<const char *> f1() {
    printf("f1 done.\n");
    co_return "f1 Great!";
}

task<void> f2() {
    printf("f2 start.\n");
    co_await timeout(std::chrono::seconds{2});
    printf("f2 done.\n");
    co_return;
}

task<> run() {
    auto results = co_await some(2, f0(), f1(), f2());
    for (const auto &[idx, var] : results) {
        std::cout << "get the result of f" << idx << ": ";
        std::visit(
            overload{
                [](std::monostate) { std::cout << "(void)\n"; },
                [](int x) { std::cout << x << " : int\n"; },
                [](const char *s) { std::cout << s << " : string\n"; },
            },
            var
        );
    }
}

int main() {
    io_context ctx;
    ctx.co_spawn(run());
    ctx.start();
    ctx.join();
    return 0;
}
