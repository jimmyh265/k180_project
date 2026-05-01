#include <cuda_runtime.h>
#include <device_launch_parameters.h>
#include <opencv2/core/types.hpp>

__global__ void noBlenderKernel(const uchar3* src, size_t src_pitch,
                                const uchar* mask, size_t mask_pitch,
                                uchar3* dst, size_t dst_pitch,
                                uchar* dst_mask, size_t dst_mask_pitch,
                                int dx, int dy, int width, int height)
{
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= width || y >= height) return;

    const uchar3* src_row = (const uchar3*)((const char*)src + y * src_pitch);
    uchar3* dst_row       = (uchar3*)((char*)dst + (y + dy) * dst_pitch);

    const uchar* mask_row = (const uchar*)((const char*)mask + y * mask_pitch);
    uchar* dst_mask_row   = (uchar*)((char*)dst_mask + (y + dy) * dst_mask_pitch);

    if (mask_row[x])
        dst_row[x + dx] = src_row[x];
    dst_mask_row[x + dx] |= mask_row[x];
}

extern "C" void launchNoBlenderKernel(const uchar3* src, size_t src_pitch,
                                      const uchar* mask, size_t mask_pitch,
                                      uchar3* dst, size_t dst_pitch,
                                      uchar* dst_mask, size_t dst_mask_pitch,
                                      int dx, int dy, int width, int height)
{
    dim3 block(16, 16);
    dim3 grid((width + block.x - 1) / block.x,
              (height + block.y - 1) / block.y);

    noBlenderKernel<<<grid, block>>>(src, src_pitch,
                                     mask, mask_pitch,
                                     dst, dst_pitch,
                                     dst_mask, dst_mask_pitch,
                                     dx, dy, width, height);

//    cudaDeviceSynchronize(); // 可選，方便 debug
}