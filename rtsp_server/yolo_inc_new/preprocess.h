#pragma once

#include <map>
#include <opencv2/opencv.hpp>
#include "NvInfer.h"
#include "types.h"

void cuda_preprocess_init(int max_image_size);

void cuda_preprocess_destroy();

void cuda_preprocess(uint8_t* src, int src_width, int src_height, float* dst, int dst_width, int dst_height,
                     cudaStream_t stream);

void cuda_batch_preprocess(std::vector<cv::Mat>& img_batch, float* dst, int dst_width, int dst_height,
                           cudaStream_t stream);

// void cuda_preprocess_gpu(uint8_t* src_device, size_t src_step, int src_width, int src_height, float* dst_device, int dst_width, int dst_height, cudaStream_t stream);
void cuda_preprocess_gpu(const uint8_t* src_device, size_t src_step,
                         int src_width, int src_height,
                         float* dst_device, int dst_width, int dst_height,
                         cudaStream_t stream);