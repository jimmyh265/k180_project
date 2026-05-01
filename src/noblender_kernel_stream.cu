// noblender_kernel.cu
#include <cuda_runtime.h>
#include <device_launch_parameters.h>
#include "noblender_kernel_stream.h"
#include <stdint.h>
#include <math.h>
#include <stddef.h>

// ------------------------------
// BGR version (uchar3)
// ------------------------------
__global__ void noBlenderKernel(
    const uchar3* src, size_t src_pitch,
    const unsigned char* mask, size_t mask_pitch,
    uchar3* dst, size_t dst_pitch,
    unsigned char* dst_mask, size_t dst_mask_pitch,
    int dx, int dy, int width, int height,
    int dst_w, int dst_h)
{
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= width || y >= height) return;

    int ox = x + dx;
    int oy = y + dy;
    if (ox < 0 || oy < 0 || ox >= dst_w || oy >= dst_h) return;

    const unsigned char* mask_row =
        (const unsigned char*)((const char*)mask + (size_t)y * mask_pitch);
    unsigned char mv = mask_row[x];
    if (!mv) return;

    const uchar3* src_row = (const uchar3*)((const char*)src + (size_t)y * src_pitch);
    uchar3*       dst_row = (uchar3*)((char*)dst + (size_t)oy * dst_pitch);

    unsigned char* dst_mask_row =
        (unsigned char*)((char*)dst_mask + (size_t)oy * dst_mask_pitch);

    dst_row[ox] = src_row[x];
    dst_mask_row[ox] |= mv;
}

extern "C" void launchNoBlenderKernel(
    const uchar3* src, size_t src_pitch,
    const unsigned char* mask, size_t mask_pitch,
    uchar3* dst, size_t dst_pitch,
    unsigned char* dst_mask, size_t dst_mask_pitch,
    int dx, int dy, int width, int height,
    int dst_w, int dst_h,
    cudaStream_t stream)
{
    dim3 block(16, 16);
    dim3 grid((width  + block.x - 1) / block.x,
              (height + block.y - 1) / block.y);

    noBlenderKernel<<<grid, block, 0, stream>>>(
        src, src_pitch,
        mask, mask_pitch,
        dst, dst_pitch,
        dst_mask, dst_mask_pitch,
        dx, dy, width, height,
        dst_w, dst_h);

    // 可選：debug 時才做
    // cudaGetLastError();
    // cudaStreamSynchronize(stream);
}

// ------------------------------
// RGBA version (uchar4)
// ------------------------------
__global__ void noBlenderKernelRGBA(
    const uchar4* src, size_t src_pitch,
    const unsigned char* mask, size_t mask_pitch,
    uchar4* dst, size_t dst_pitch,
    unsigned char* dst_mask, size_t dst_mask_pitch,
    int dx, int dy, int width, int height,
    int dst_w, int dst_h)
{
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= width || y >= height) return;

    int ox = x + dx;
    int oy = y + dy;
    if (ox < 0 || oy < 0 || ox >= dst_w || oy >= dst_h) return;

    const unsigned char* mask_row =
        (const unsigned char*)((const char*)mask + (size_t)y * mask_pitch);
    unsigned char mv = mask_row[x];
    if (!mv) return;

    const uchar4* src_row = (const uchar4*)((const char*)src + (size_t)y * src_pitch);
    uchar4*       dst_row = (uchar4*)((char*)dst + (size_t)oy * dst_pitch);

    unsigned char* dst_mask_row =
        (unsigned char*)((char*)dst_mask + (size_t)oy * dst_mask_pitch);

    dst_row[ox] = src_row[x];
    dst_mask_row[ox] |= mv;
}

extern "C" void launchNoBlenderKernelRGBA(
    const uchar4* src, size_t src_pitch,
    const unsigned char* mask, size_t mask_pitch,
    uchar4* dst, size_t dst_pitch,
    unsigned char* dst_mask, size_t dst_mask_pitch,
    int dx, int dy, int width, int height,
    int dst_w, int dst_h,
    cudaStream_t stream)
{
    dim3 block(16, 16);
    dim3 grid((width  + block.x - 1) / block.x,
              (height + block.y - 1) / block.y);

    noBlenderKernelRGBA<<<grid, block, 0, stream>>>(
        src, src_pitch,
        mask, mask_pitch,
        dst, dst_pitch,
        dst_mask, dst_mask_pitch,
        dx, dy, width, height,
        dst_w, dst_h);

    // 可選：debug 時才做
    // cudaGetLastError();
    // cudaStreamSynchronize(stream);
}

// ------------------------------
// compensator with scale
// ------------------------------
// apply_gain_blocks_rgba.cu

static __device__ __forceinline__ unsigned char sat_u8(float v) {
    v = v < 0.f ? 0.f : v;
    v = v > 255.f ? 255.f : v;
    return (unsigned char)(v + 0.5f);
}

static __device__ __forceinline__ float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

__global__ void kApplyGainBlocksRGBA_LocalGrid_IO_Bilinear(
    const uchar4* __restrict__ src, size_t srcStep,
    uchar4* __restrict__ dst,       size_t dstStep,
    const unsigned char* __restrict__ mask, size_t maskStep,
    int w, int h,
    const float* __restrict__ gainGrid, size_t gainStepBytes, // step-aware
    int gridW, int gridH,
    int blockSizeSmall,
    float seamScale)
{
    int x = (int)(blockIdx.x * blockDim.x + threadIdx.x);
    int y = (int)(blockIdx.y * blockDim.y + threadIdx.y);
    if (x >= w || y >= h) return;

    // load src pixel
    const uchar4* srow = (const uchar4*)((const char*)src + (size_t)y * srcStep);
    uchar4 p = srow[x];

    // mask gating (invalid => copy-through)
    const unsigned char* mrow = (const unsigned char*)((const char*)mask + (size_t)y * maskStep);
    if (mrow[x] == 0) {
        uchar4* drow = (uchar4*)((char*)dst + (size_t)y * dstStep);
        drow[x] = p;
        return;
    }

    // Map full-res local pixel -> seam-scale local pixel
    // Then to "block grid coordinate" in floating point
    float xs = (float)x * seamScale;
    float ys = (float)y * seamScale;

    float gx = xs / (float)blockSizeSmall; // grid-space x
    float gy = ys / (float)blockSizeSmall; // grid-space y

    // Clamp so that x0+1, y0+1 are valid
    gx = clampf(gx, 0.f, (float)(gridW - 1));
    gy = clampf(gy, 0.f, (float)(gridH - 1));

    int x0 = (int)floorf(gx);
    int y0 = (int)floorf(gy);
    int x1 = x0 + 1; if (x1 >= gridW) x1 = gridW - 1;
    int y1 = y0 + 1; if (y1 >= gridH) y1 = gridH - 1;

    float fx = gx - (float)x0;
    float fy = gy - (float)y0;

    // step-aware row pointers
    const float* row0 = (const float*)((const char*)gainGrid + (size_t)y0 * gainStepBytes);
    const float* row1 = (const float*)((const char*)gainGrid + (size_t)y1 * gainStepBytes);

    float g00 = row0[x0];
    float g10 = row0[x1];
    float g01 = row1[x0];
    float g11 = row1[x1];

    // Bilinear interpolation
    float g0 = g00 + fx * (g10 - g00);
    float g1 = g01 + fx * (g11 - g01);
    float g  = g0  + fy * (g1  - g0);

    // (optional) clamp gain to avoid pathological values
    // If you want: g = clampf(g, 0.1f, 10.0f);

    // Apply (GAIN_BLOCKS: same gain for RGB, alpha keep)
    p.x = sat_u8((float)p.x * g);
    p.y = sat_u8((float)p.y * g);
    p.z = sat_u8((float)p.z * g);

    uchar4* drow = (uchar4*)((char*)dst + (size_t)y * dstStep);
    drow[x] = p;
}

extern "C" void launchApplyGainBlocksRGBA_LocalGrid_IO_Bilinear(
    const uchar4* src, size_t srcStep,
    uchar4* dst,       size_t dstStep,
    const unsigned char* validMask, size_t maskStep,
    int w, int h,
    const float* gainGrid, size_t gainStepBytes,
    int gridW, int gridH,
    int blockSizeSmall,
    float seamScale,
    cudaStream_t stream)
{
    dim3 block(16, 16);
    dim3 grid((w + block.x - 1) / block.x,
              (h + block.y - 1) / block.y);

    kApplyGainBlocksRGBA_LocalGrid_IO_Bilinear<<<grid, block, 0, stream>>>(
        src, srcStep,
        dst, dstStep,
        validMask, maskStep,
        w, h,
        gainGrid, gainStepBytes,
        gridW, gridH,
        blockSizeSmall,
        seamScale);
}