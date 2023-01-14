#include "co_context/io_context.hpp"
#include "co_context/lazy_io.hpp"
#include <cstdint>
#include <iostream>
using namespace co_context;
using namespace std;

io_context ctx[4];

task<> jockey(const char *name) {
    printf("%10s at \t%lx\n", name, uintptr_t(&this_io_context()));
    co_await resume_on(ctx[3]);
    printf("%10s resume_on \t%lx\n", name, uintptr_t(&this_io_context()));
}

int main() {
    ctx[0].co_spawn(jockey("Alice"));
    ctx[1].co_spawn(jockey("Box"));
    ctx[2].co_spawn(jockey("Charles"));

    for (auto &c : ctx) {
        c.start();
    }

    ctx[3].join(); // never stop;

    return 0;
}

/*
Output:
       Box at   55ca5d43ea00
     Alice at   55ca5d41e880
   Charles at   55ca5d45eb80
       Box resume_on    55ca5d47ed00
     Alice resume_on    55ca5d47ed00
   Charles resume_on    55ca5d47ed00
*/
