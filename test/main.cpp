#include "co_context.hpp"

int main(int argc, char *argv[]) {
    co_context::co_context<0> io_context{8};

    io_context.run_thread_pool();
    
    return 0;
}