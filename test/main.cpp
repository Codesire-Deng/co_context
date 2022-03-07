#include "co_context.hpp"

int main(int argc, char *argv[]) {
    co_context::co_context<0, 6, true, 256> io_context{8};

    io_context.probe();

    io_context.make_test_thread_pool();

    io_context.run_test_swap();
    
    return 0;
}