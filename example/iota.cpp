#if defined(__GNUG__) && !defined(__clang__)

#include "co_context/generator.hpp"
#include <iostream>
#include <ranges>

co_context::generator<int> iota(int x) {
    while (true) {
        co_yield x;
        ++x;
    }
}

int main() {
    using std::views::drop, std::views::take;

    for (auto &&x : iota(1) | drop(5) | take(3)) {
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