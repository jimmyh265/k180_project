// noblender_kernel.h
#pragma once
#include <cuda_runtime.h>
#include <opencv2/core/cuda.hpp>

#ifdef __cplusplus
extern "C" {
#endif

void launchNoBlenderKernel(
    const uchar3* src, size_t src_pitch,
    const uchar* mask, size_t mask_pitch,
    uchar3* dst, size_t dst_pitch,
    uchar* dst_mask, size_t dst_mask_pitch,
    int dx, int dy, int width, int height
);

// RGBA (CV_8UC4) version
void launchNoBlenderKernelRGBA(
    const uchar4* src, size_t src_pitch,
    const uchar*  mask, size_t mask_pitch,
    uchar4*       dst, size_t dst_pitch,
    uchar*        dst_mask, size_t dst_mask_pitch,
    int dx, int dy, int width, int height
);

#ifdef __cplusplus
}
#endif

