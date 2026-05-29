#pragma once

#include <pthread.h>

#include <atomic>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <vector>
#include <string>
#include <array>
#include <memory>
#include <cstdint>
#include <unistd.h> // close()
#include <queue>
#include <iostream>

#include <opencv2/core.hpp>
#include <opencv2/core/cuda.hpp>
#include <condition_variable>

// stitching/detail headers
#include <opencv2/stitching/detail/autocalib.hpp>
#include <opencv2/stitching/detail/blenders.hpp>
#include <opencv2/stitching/detail/timelapsers.hpp>
#include <opencv2/stitching/detail/camera.hpp>	   // cv::detail::CameraParams
#include <opencv2/stitching/detail/exposure_compensate.hpp>
#include <opencv2/stitching/detail/matchers.hpp>
#include <opencv2/stitching/detail/motion_estimators.hpp>
#include <opencv2/stitching/detail/seam_finders.hpp>
#include <opencv2/stitching/detail/warpers.hpp>  // cv::detail::RotationWarper
#include <opencv2/stitching/warpers.hpp>

#include "user_def_json.h"
#include "k180_constants.h" // k180::constants::CAM_NUMBER
#include "k180_stage_queue.h"

namespace k180::runtime {

inline UserConfig cfggg;

inline std::array<bool, k180::constants::CAM_NUMBER> SD{};
inline std::array<bool, k180::constants::CAM_NUMBER> S_16S{};
// inline std::array<bool, k180::constants::CAM_NUMBER> S_SEAM{};
// inline std::array<bool, k180::constants::CAM_NUMBER> S_TRIG{};
// inline std::array<bool, k180::constants::CAM_NUMBER> S_FA{};


struct SDSharedVar
{
    static constexpr int N = k180::constants::CAM_NUMBER;

    // --- IO / device ---
    std::vector<int> g_fds;
	std::array<std::mutex, N> g_cam_mu;

    // --- geometry / corners / sizes ---
    std::vector<cv::Point> corners_sd;
    std::vector<cv::Point> corners_resize;
    std::vector<cv::Size>  sizes_sd;
    cv::Rect dst_roi;

    // --- camera params ---
    std::vector<cv::detail::CameraParams> cameras_sd;

    // --- masks / warped buffers ---
    // std::vector<cv::cuda::GpuMat> mask_warped_g;
	std::array<std::array<cv::cuda::GpuMat,4>,2> mask_warped_g; // [2][4]
	std::atomic<int> mask_front{0}; // 0 or 1
	
    std::vector<cv::cuda::GpuMat> meta_mask_warped_g;

    std::vector<cv::UMat> meta_masks_warp_resize_u;
	std::vector<cv::Mat>  meta_masks_warp_resize;
    std::vector<cv::UMat> meta_masks_warp_comp_u;
    std::vector<cv::Mat>  meta_masks_warp_comp_c;
	
    std::vector<cv::UMat> meta_masks_warp_orig_u;
    std::vector<cv::Mat>  meta_masks_warp_orig_c;

    // originally: cv::cuda::GpuMat img_remap_g[CAM_NUMBER];
    std::array<cv::cuda::GpuMat, N> img_remap_g;

    std::vector<cv::UMat> images_warped_resize_f;
    std::vector<cv::UMat> images_warped_resize_u;
    std::vector<cv::Mat>  img_warp_comp_tmp;

    // --- scales / warper ---
    float warped_image_scale     = 1.0f;
    float warped_image_scale_sd  = 1.0f;

    std::shared_ptr<cv::detail::RotationWarper> warper_init_cam;
	
    // --- seam state flags ---
	// cv::Ptr<cv::detail::SeamFinder> seam_finder;
	// Ptr<SeamFinder> seam_finder;
    bool find_seam_ready   = false;
    bool SEAM_COUNT_RESET  = false;
    int  seam_count        = 0;
    int  seam_c_base       = 1;

    SDSharedVar() {
        resize_all();
    }

    void resize_all() {
        corners_sd.resize(N);
        corners_resize.resize(N);
        sizes_sd.resize(N);

        // mask_warped_g.resize(N);
        meta_mask_warped_g.resize(N);

        meta_masks_warp_resize_u.resize(N);
		meta_masks_warp_resize.resize(N);
        meta_masks_warp_comp_u.resize(N);
        meta_masks_warp_comp_c.resize(N);

        meta_masks_warp_orig_u.resize(N);
        meta_masks_warp_orig_c.resize(N);
		
        images_warped_resize_f.resize(N);
        images_warped_resize_u.resize(N);
        img_warp_comp_tmp.resize(N);
        // img_remap_g 是 std::array，不用 resize
    }

    void reset_seam_state() {
        find_seam_ready  = false;
        SEAM_COUNT_RESET = false;
        seam_count       = 0;
        seam_c_base      = 1;
    }
#if 0
// vector<Mat> meta_masks_warped_sd(tmmm_nu);
// vector<Mat> img_warped_s(tmmm_nu);
// vector<Mat> mask_warped(tmmm_nu);
// vector<Mat> img_warp_resize(tmmm_nu);
// vector<UMat> img_warp_resize_u(tmmm_nu);
// vector<UMat> img_warp_comp_u(tmmm_nu);
// vector<cv::cuda::GpuMat>  img_remap_g_s(tmmm_nu);
// int remap_g_index = 0;
#endif
};

struct SD_process {
    using WaveCorrectKind     = cv::detail::WaveCorrectKind;
    using ExposureCompensator = cv::detail::ExposureCompensator;
    using Blender             = cv::detail::Blender;

    bool try_cuda = true;
    float conf_thresh = 1.f;

// #ifdef HAVE_OPENCV_XFEATURES2D
    std::string features_type = "surf";
    float match_conf = 0.65f;
// #else
    // std::string features_type = "orb";
    // float match_conf = 0.3f;
// #endif
    std::string matcher_type = "homography";
    std::string estimator_type = "homography";
    std::string ba_cost_func = "ray";
    std::string ba_refine_mask = "xxxxx";
    // bool do_wave_correct = false;
    // WaveCorrectKind wave_correct = cv::detail::WAVE_CORRECT_HORIZ;
    // bool save_graph = false;
    // std::string save_graph_to;
    // std::string warp_type = "spherical";
    int expos_comp_type = ExposureCompensator::GAIN_BLOCKS;
    int expos_comp_nr_feeds = 1;

    int expos_comp_nr_filtering = 2;
    int expos_comp_block_size = 32;

    std::string seam_find_type = "gc_color";	//會當, 20260206 thread_seam_find 裡，CV_32F設好之後就不會當了
    // std::string seam_find_type = "dp_colorgrad";
    // std::string seam_find_type = "voronoi";
    int blend_type = cv::detail::Blender::MULTI_BAND;
	// int blend_type = Blender::FEATHER;
    int range_width      = -1;
	double work_scale    = 1.0;
	double seam_scale    = 0.1;
	// double compose_scale = 1.0;

};

struct K180Runtime {
	SD_process sdp;
    SDSharedVar sdv;
	// CC_entry_Ctx cec;
};

K180Runtime& rt();
void init_cuda_primary_ctx_once();
void cuda_set_current_for_thread(const char* tag = nullptr);

} // namespace k180::runtime
