
#pragma once

#include <string>
#include <vector>
#include <iostream>
#include <opencv2/imgproc/imgproc.hpp>

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/core/utility.hpp>  // FileStorage
#include <opencv2/stitching/detail/camera.hpp>
#include <opencv2/stitching/detail/matchers.hpp>
#include <opencv2/stitching/detail/autocalib.hpp>

#include "k180_runtime.h"


namespace k180::runtime::stitchapi {

void saveMasks(const std::vector<cv::UMat>& masks_warped, const std::string& folder_path);
std::vector<cv::Mat> loadMasks(const std::string& folder_path, size_t num_images);

// vector<Point>
void saveCorners(const std::string& filename, const std::vector<cv::Point>& corners);
void loadCorners(const std::string& filename, std::vector<cv::Point>& corners);

// vector<Size>
void saveSizes(const std::string& filename, const std::vector<cv::Size>& sizes);
void loadSizes(const std::string& filename, std::vector<cv::Size>& sizes);

void saveWarpedImageScale(const std::string& filename, float warped_image_scale);
void loadWarpedImageScale(const std::string& filename, float& warped_image_scale);

void saveCameraParams(const std::string& folder, const std::vector<cv::detail::CameraParams>& cameras);
void loadCameraParams(const std::string& folder, std::vector<cv::detail::CameraParams>& cameras, int num_images);

std::vector<cv::detail::ImageFeatures>
computeFeaturesFromImages(const std::vector<cv::Mat>& images);

std::vector<cv::detail::MatchesInfo>
computepairwisematches(std::vector<cv::detail::ImageFeatures>& features);

void estimatorCamera(std::vector<cv::detail::ImageFeatures>& features,
                     std::vector<cv::detail::MatchesInfo>& pairwise_matches,
                     std::vector<cv::detail::CameraParams>& cameras);

bool adjusterCamera(std::vector<cv::detail::ImageFeatures>& features,
                    std::vector<cv::detail::MatchesInfo>& pairwise_matches,
                    std::vector<cv::detail::CameraParams>& cameras,
                    float& warped_image_scale);

} // namespace k180::runtime::stitchapi

