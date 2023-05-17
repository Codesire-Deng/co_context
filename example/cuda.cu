#include <algorithm>
#include <cstdio>
#include <cuda_runtime.h>
#include "co_context/lazy_io.hpp"

#define CHECK(callee)                                            \
    do {                                                         \
        const cudaError_t err = callee;                          \
        if (err == cudaSuccess) break;                           \
        printf("CUDA error at %s(%d)\n", __FILE__, __LINE__);    \
        printf("    Function:   %s\n", __FUNCTION__);            \
        printf("    Error code: %d\n", err);                     \
        printf("    Error hint: %s\n", cudaGetErrorString(err)); \
        exit(1);                                                 \
    } while (0)

constexpr unsigned int FULL_MASK = 0xffffffff;

void __global__ reduce_cp(const double *d_x, double *d_y, const int N) {
    const int tid = threadIdx.x;
    const int bid = blockIdx.x;
    extern __shared__ double s_y[];

    double y = 0.0;
    const int stride = blockDim.x * gridDim.x; // 以网格大小为跨度
    for (int n = bid * blockDim.x + tid; n < N; n += stride) {
        y += d_x[n]; // 确保一个网格能覆盖所有数据
    }
    s_y[tid] = y;
    __syncthreads();

    // 线程块内，跨线程束折半归约
    for (int offset = blockDim.x >> 1; offset >= 32; offset >>= 1) {
        if (tid < offset) {
            s_y[tid] += s_y[tid + offset];
        }
        __syncthreads();
    }

    y = s_y[tid];

    for (int offset = 16; offset > 0; offset >>= 1) {
        y += __shfl_down_sync(FULL_MASK, y, offset);
    }

    if (tid == 0) {
        d_y[bid] = y; // 返回线程块结果
    }
}

constexpr int N = 1e8;
constexpr int BLOCK_LEN = 128;
constexpr int GRID_SIZE = 10240;
__device__ double d_input[N];
__device__ double d_output[GRID_SIZE];

co_context::task<double> reduce(const double *d_x) {
    double *d_y;
    CHECK(cudaGetSymbolAddress((void **)&d_y, d_output));
    constexpr int shared_size = sizeof(double) * BLOCK_LEN;
    reduce_cp<<<GRID_SIZE, BLOCK_LEN, shared_size>>>(d_x, d_y, N);
    reduce_cp<<<1, 1024, sizeof(double) * 1024>>>(d_y, d_y, GRID_SIZE);

    double h_y[1] = {0};
    CHECK(cudaMemcpy(h_y, d_y, sizeof(double), cudaMemcpyDeviceToHost));
    // CHECK(cudaMemcpyFromSymbol(h_y, d_output, sizeof(double)));

    co_return h_y[0];
}

int main() {
    static double input[N];
    std::fill_n(input, N, 1.23f);
    CHECK(cudaMemcpyToSymbol(d_input, input, N * sizeof(double)));
    double *d_x;
    CHECK(cudaGetSymbolAddress((void **)&d_x, d_input));
    printf("%f\n", reduce(d_x));
    return 0;
}
