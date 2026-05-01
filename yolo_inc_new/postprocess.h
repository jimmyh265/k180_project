#pragma once
#ifndef POSTPROCESS_H
#define POSTPROCESS_H

#include <vector>
#include <opencv2/opencv.hpp>
#include "NvInfer.h"
#include "types.h"
#include <cuda_runtime.h>
// -------------------- CPU utility --------------------
cv::Rect get_rect(cv::Mat& img, float bbox[4]);

void nms(std::vector<Detection>& res, float* output, float conf_thresh, float nms_thresh = 0.5);

void batch_nms(std::vector<std::vector<Detection>>& batch_res, float* output, int batch_size, int output_size,
               float conf_thresh, float nms_thresh = 0.5);

void draw_bbox(std::vector<cv::Mat>& img_batch, std::vector<std::vector<Detection>>& res_batch);

void draw_bbox_keypoints_line(std::vector<cv::Mat>& img_batch, std::vector<std::vector<Detection>>& res_batch);

void batch_process(std::vector<std::vector<Detection>>& res_batch, const float* decode_ptr_host, int batch_size,
                   int bbox_element, const std::vector<cv::Mat>& img_batch);

void process_decode_ptr_host(std::vector<Detection>& res, const float* decode_ptr_host, int bbox_element, cv::Mat& img, int count);

// void process_decode_ptr_host_gpu(std::vector<Detection>& res, const float* decode_ptr_host, int bbox_element, cv::cuda::GpuMat& img, int count);

// -------------------- GPU interface --------------------
// 你的 decode buffer layout:
// [count][left][top][right][bottom][conf][class_id][keep_flag]...
static constexpr int kDecodeBBoxElement = 7;

void cuda_decode(float* predict,
                 int num_bboxes,
                 float confidence_threshold,
                 float* parray,
                 int predict_element,
                 int bbox_element,
                 int max_objects,
                 cudaStream_t stream);

void cuda_nms(float* parray,
              int bbox_element,
              float nms_threshold,
              int max_objects,
              cudaStream_t stream);

void cuda_process_decode_ptr_device(const float* decode_ptr_device,
                                    int bbox_element,
                                    Detection* out,
                                    int* out_count,
                                    int max_output,
                                    cudaStream_t stream);

// 新增：把 decode_ptr_device 上的結果直接整理成 final Detection array（仍在 GPU）
void cuda_finalize_detections(const float* decode_ptr_device,
                              int bbox_element,
                              int img_w,
                              int img_h,
                              int input_w,
                              int input_h,
                              Detection* out_device,
                              int* out_count_device,
                              int max_output,
                              cudaStream_t stream);

// -------------------- Mask drawing --------------------
void draw_mask_bbox(cv::Mat& img, std::vector<Detection>& dets, std::vector<cv::Mat>& masks,
                    std::unordered_map<int, std::string>& labels_map);
#endif