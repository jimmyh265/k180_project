#pragma once

#include <cuda_runtime.h>
#include <opencv2/core/cuda.hpp>

bool launchRemapRGBA_Bilinear_ConstBorder(
    const cv::cuda::GpuMat& src_rgba,
    cv::cuda::GpuMat& dst_rgba,
    const cv::cuda::GpuMat& map_x,
    const cv::cuda::GpuMat& map_y,
    cudaStream_t stream);
