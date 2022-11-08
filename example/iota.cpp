#if defined(__GNUG__) && !defined(__clang__)

#include <co_context/generator.hpp>
#include <iostream>
#include <cassert>
#include <ranges>

co_context::generator<int> gen_iota(int x) {
    while (true) {
        co_yield x++;
    }
}

int main() {
    using namespace std::views;

    for (auto &&x : gen_iota(1) | drop(5) | take(3)) {
        std::cout << x << " ";
    }

    return 0;
}

#else

#include <iostream>

int main() {
    std::cout << "This program requires g++ as the compiler. exit..."
              << std::endl;
    return 0;
}

#endif