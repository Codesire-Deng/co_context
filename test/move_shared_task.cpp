#include <co_context/shared_task.hpp>
#include <iostream>
using namespace co_context;

shared_task<std::string> f() {
    co_return "abcdefg";
}

task<> run() {
    auto t = f();
    std::cout << co_await t << "\n";
    std::cout << co_await t << "\n";
    auto str = co_await std::move(t);
    std::cout << "str: " << str << "\n";
    std::cout << co_await t << "\n";
    str = std::move(co_await t);
    std::cout << "str: " << str << "\n";
    std::cout << co_await t << "\n"; // empty string
}

int main() {
    io_context ctx;
    ctx.co_spawn(run());
    ctx.start();
    ctx.join();
    return 0;
}
