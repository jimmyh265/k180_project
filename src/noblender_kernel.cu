#include <cuda_runtime.h>
#include <device_launch_parameters.h>
#include <opencv2/core/types.hpp>
#include "noblender_kernel.h"   // ✅ 建議 include header，確保簽名一致

// ------------------------------
// BGR version (uchar3) - keep
// ------------------------------
__global__ void noBlenderKernel(const uchar3* src, size_t src_pitch,
                                const uchar* mask, size_t mask_pitch,
                                uchar3* dst, size_t dst_pitch,
                                uchar* dst_mask, size_t dst_mask_pitch,
                                int dx, int dy, int width, int height,
                                int dst_w, int dst_h)
{
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= width || y >= height) return;

    // output coords
    int ox = x + dx;
    int oy = y + dy;
    if (ox < 0 || oy < 0 || ox >= dst_w || oy >= dst_h) return;

    const uchar* mask_row = (const uchar*)((const char*)mask + (size_t)y * mask_pitch);
    const uchar mv = mask_row[x];
    if (!mv) return;

    const uchar3* src_row = (const uchar3*)((const char*)src + (size_t)y * src_pitch);
    uchar3* dst_row       = (uchar3*)((char*)dst + (size_t)oy * dst_pitch);

    uchar* dst_mask_row   = (uchar*)((char*)dst_mask + (size_t)oy * dst_mask_pitch);

    dst_row[ox] = src_row[x];
    dst_mask_row[ox] |= mv;   // mv 通常是 0/255
}

extern "C" void launchNoBlenderKernel(const uchar3* src, size_t src_pitch,
                                      const uchar* mask, size_t mask_pitch,
                                      uchar3* dst, size_t dst_pitch,
                                      uchar* dst_mask, size_t dst_mask_pitch,
                                      int dx, int dy, int width, int height)
{
    // ✅ block 16x16 OK
    dim3 block(16, 16);
    dim3 grid((width + block.x - 1) / block.x,
              (height + block.y - 1) / block.y);

    // 這裡假設 dst 大小至少覆蓋 (width+dx, height+dy)；
    // 但為了安全，我們仍然在 kernel 做 dst_w/dst_h 檢查。
    // 你如果有 dst 的實際寬高，可在呼叫端傳進來更準。
    const int dst_w = width + max(dx, 0);
    const int dst_h = height + max(dy, 0);

    noBlenderKernel<<<grid, block>>>(src, src_pitch,
                                     mask, mask_pitch,
                                     dst, dst_pitch,
                                     dst_mask, dst_mask_pitch,
                                     dx, dy, width, height,
                                     dst_w, dst_h);

    // cudaDeviceSynchronize(); // 可選 debug
}

// ------------------------------
// RGBA version (uchar4) - NEW
// ------------------------------
__global__ void noBlenderKernelRGBA(const uchar4* src, size_t src_pitch,
                                    const uchar* mask, size_t mask_pitch,
                                    uchar4* dst, size_t dst_pitch,
                                    uchar* dst_mask, size_t dst_mask_pitch,
                                    int dx, int dy, int width, int height,
                                    int dst_w, int dst_h)
{
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= width || y >= height) return;

    int ox = x + dx;
    int oy = y + dy;
    if (ox < 0 || oy < 0 || ox >= dst_w || oy >= dst_h) return;

    const uchar* mask_row = (const uchar*)((const char*)mask + (size_t)y * mask_pitch);
    const uchar mv = mask_row[x];
    if (!mv) return;

    const uchar4* src_row = (const uchar4*)((const char*)src + (size_t)y * src_pitch);
    uchar4* dst_row       = (uchar4*)((char*)dst + (size_t)oy * dst_pitch);

    uchar* dst_mask_row   = (uchar*)((char*)dst_mask + (size_t)oy * dst_mask_pitch);

    dst_row[ox] = src_row[x];     // ✅ RGBA whole pixel copy
    dst_mask_row[ox] |= mv;
}

extern "C" void launchNoBlenderKernelRGBA(const uchar4* src, size_t src_pitch,
                                          const uchar* mask, size_t mask_pitch,
                                          uchar4* dst, size_t dst_pitch,
                                          uchar* dst_mask, size_t dst_mask_pitch,
                                          int dx, int dy, int width, int height)
{
    dim3 block(16, 16);
    dim3 grid((width + block.x - 1) / block.x,
              (height + block.y - 1) / block.y);

    const int dst_w = width + max(dx, 0);
    const int dst_h = height + max(dy, 0);

    noBlenderKernelRGBA<<<grid, block>>>(src, src_pitch,
                                         mask, mask_pitch,
                                         dst, dst_pitch,
                                         dst_mask, dst_mask_pitch,
                                         dx, dy, width, height,
                                         dst_w, dst_h);

    // cudaDeviceSynchronize(); // 可選 debug
}

