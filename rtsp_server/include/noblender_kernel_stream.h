// noblender_kernel_stream.h
#pragma once
#include <cuda_runtime.h>
#include <cstddef>

#ifdef __cplusplus
extern "C" {
#endif

// BGR (uchar3)
void launchNoBlenderKernel(
    const uchar3* src, size_t src_pitch,
    const unsigned char* mask, size_t mask_pitch,
    uchar3* dst, size_t dst_pitch,
    unsigned char* dst_mask, size_t dst_mask_pitch,
    int dx, int dy, int width, int height,
    int dst_w, int dst_h,
    cudaStream_t stream);

// RGBA (uchar4)
void launchNoBlenderKernelRGBA(
    const uchar4* src, size_t src_pitch,
    const unsigned char* mask, size_t mask_pitch,
    uchar4* dst, size_t dst_pitch,
    unsigned char* dst_mask, size_t dst_mask_pitch,
    int dx, int dy, int width, int height,
    int dst_w, int dst_h,
    cudaStream_t stream);

void launchApplyGainBlocksRGBA_SeamScale_IO(
    const uchar4* src, size_t srcStep,
    uchar4* dst,       size_t dstStep,
    const unsigned char* validMask, size_t maskStep,
    int w, int h,
    const float* gainGrid, int gridW, int gridH,
    int blockSizeSmall,
    float seamScale,
    int cornerFullX, int cornerFullY,
    int roiFullX, int roiFullY,
    cudaStream_t stream);

void launchApplyGainBlocksRGBA_LocalGrid_IO_Bilinear(
    const uchar4* src, size_t srcStep,
    uchar4* dst,       size_t dstStep,
    const unsigned char* validMask, size_t maskStep,
    int w, int h,
    const float* gainGrid, size_t gainStepBytes,  // ✅ NEW
    int gridW, int gridH,
    int blockSizeSmall,
    float seamScale,
    cudaStream_t stream);

#ifdef __cplusplus
}
#endif
