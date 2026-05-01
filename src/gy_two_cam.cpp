#include <unistd.h>		// usleep
#include <chrono>

#include <mutex>
#include <condition_variable>
#include <queue>
#include <vector>
#include <string>
#include <iostream>
#include <thread>
#include <sys/file.h>
#include "ImgUDPGateway.hpp"
#include "cuda_utils.h"

#include "types.h"	// yolo_inc/types.h : Detection, Object
#include "postprocess.h"
#include "netcmd.hpp"
#include "logging.h"
#include "model.h"
#include "utils.h"

#include <stdio.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <json-c/json.h>
#include <shared_mutex>
#include <glib-unix.h>   // g_unix_signal_add
#include <atomic>
#include <csignal>

#include <fmt/format.h>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>		// cv::remap

#include <opencv2/core.hpp>
#include <opencv2/core/cuda.hpp>
#include <opencv2/cudawarping.hpp>
#include <opencv2/cudaimgproc.hpp>		// cv::cuda::cvtColor (？好像matchtemplate or gpumat 才需要？)
#include <opencv2/cudaarithm.hpp>		// cv::cuda::normalize
#include <cuda_runtime.h>
#include "gstreamer-1.0/gst/gst.h"
#include "gstreamer-1.0/gst/gstmessage.h"
#include "gstreamer-1.0/gst/rtsp-server/rtsp-server.h"

#include "glib-2.0/glib.h"
#include <gstreamer-1.0/gst/app/app.h>
#include <linux/videodev2.h>
#include <opencv2/videoio/videoio_c.h>	// CV_CAP_PROP_EXPOSURE
// #include "opencv2/opencv_modules.hpp"
// #include <opencv2/core/utility.hpp>
#include "opencv2/imgcodecs.hpp"
#include "opencv2/stitching/detail/autocalib.hpp"
#include "opencv2/stitching/detail/blenders.hpp"
#include "opencv2/stitching/detail/timelapsers.hpp"
#include "opencv2/stitching/detail/camera.hpp"
#include "opencv2/stitching/detail/exposure_compensate.hpp"
#include "opencv2/stitching/detail/matchers.hpp"
#include "opencv2/stitching/detail/motion_estimators.hpp"
#include "opencv2/stitching/detail/seam_finders.hpp"
#include "opencv2/stitching/detail/warpers.hpp"
#include "opencv2/stitching/warpers.hpp"

#include <gstreamer-1.0/gst/app/gstappsrc.h>		//gst_app_src_end_of_stream
// #include <execinfo.h>   // backtrace, backtrace_symbols_fd
#include <cstring>      // strlen
// #include <exception>    // std::set_terminate
// #include <sstream>
// #include <iomanip>

#include <opencv2/opencv.hpp>
#include "BYTETracker.h"

#include "noblender_kernel_stream.h"
#include "my_seamfinder.hpp"
#include "gy_logging.h"
#include "user_def_json.h"
#include "k180_constants.h"
#include "k180_runtime.h"
#include "k180_tracking.h"
#include "k180_perf_stats.h"
#include "k180_bright_tuner.h"
// #include "k180_user_cfg_dump.h"
#include "k180_stitch_api.h"
#include "k180_rec.h"
#include "k180_h265_hub.h"
#include "k180_gst_dbg.h" 
#include "k180_rtsp_attach.h" 
#include "k180_stream_builder.h" 
#include "k180_stream_key.h" 
#include "k180_stage_queue.h"
#include "k180_stage_sync.h"
#include "k180_frame_item.h" 
#include "k180_osd_shared.h"
// #include "k180_osd_publish.h"
#include "k180_osd_meta.h"
// #include "k180_infer_bridge.h"
#include "k180_frame_tag_meta.h"
#include "k180_osd_slots.h"
#include "k180_ai_runtime.h"
#include "k180_dbg_timing.h"
#include "remap_rgba_kernel.h"

#include "gstreamer-1.0/gst/app/gstappsink.h"
#include <gstreamer-1.0/gst/allocators/gstdmabuf.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <cuda.h>
#include <cudaEGL.h>
#include <nvbufsurface.h>
#include "gstnvdsmeta.h"   // declares gst_buffer_get_nvds_batch_meta
#include "nvdsmeta.h"      // declares NvDsBatchMeta / NvDs* types
#include "gstnvdsbufferpool.h"   // DeepStream
#include <opencv2/core/cuda_stream_accessor.hpp>
#include <unordered_map>

#undef HAVE_OPENCV_CUDALEGACY
#define ENABLE_LOG 1
#define FW_VER "1.60.9"	// 1.22 fix program exit procedure, 1.23 減少 調光 log, 
// 1.60.4  60fps with inference
/*
1.60.5 加入了 infer thread
1.60.7 測試 cur 釋放時機
1.60.9 這是一個中途版本，好像已經修正了 FPS減半的 issue，透過 在 acquire 時不 block 來達成 (GstBuffer* acquire()) 先暫存這版，因為我要讓 codex 直接進來改，故備份
*/
#define tmmm_nu	4

using namespace std;
using namespace cv;
using namespace cv::detail;
using namespace k180;
using namespace k180::constants;
// using namespace k180::brighttuner;
using namespace k180::runtime;
using namespace k180::runtime::tracking;
using namespace k180::runtime::stitchapi;
using namespace k180::rec;
using namespace k180::pipeline;
using namespace k180::streambuilder;
using namespace k180::osd;
// using namespace k180::dbgtime;

using k180::gstdbg::RateMon;
using k180::gstdbg::padprobe_rate;
// using namespace k180::runtime;
// using namespace k180::K180Runtime;
static thread_local TimeTesting tt;

HubManager mgr;

std::thread trig_thread;
std::shared_mutex mask_r_g_mutex[4];
std::mutex bd_pool_mtx, bd_prep_mtx;
std::condition_variable blend_prep;

std::atomic<bool> keep_running(true);


#if 0
// GPU NoBlender , CV_8UC3, no stream
class NoBlenderGPU : public cv::detail::Blender {
public:
    NoBlenderGPU() {}

    // 初始化 GPU buffer
    void prepare(const std::vector<Point> &corners, const std::vector<Size> &sizes) override {
        dst_roi_ = resultRoi(corners, sizes);
        dst_.create(dst_roi_.size(), CV_8UC3);
        dst_.setTo(Scalar::all(0));
        dst_mask_.create(dst_roi_.size(), CV_8U);
        dst_mask_.setTo(Scalar::all(0));
    }

	void feed(const cv::cuda::GpuMat& d_img, const cv::cuda::GpuMat& d_mask, Point tl) {
		CV_Assert(d_img.type()  == CV_8UC3);
		CV_Assert(d_mask.type() == CV_8UC1);

		int dx = tl.x - dst_roi_.x;
		int dy = tl.y - dst_roi_.y;

		launchNoBlenderKernel(
			d_img.ptr<uchar3>(), d_img.step,
			d_mask.ptr<uchar>(), d_mask.step,
			dst_.ptr<uchar3>(), dst_.step,
			dst_mask_.ptr<uchar>(), dst_mask_.step,
			dx, dy, d_img.cols, d_img.rows
		);

	}

	cv::cuda::GpuMat blend() {
		return dst_;
	}

private:
    cv::cuda::GpuMat dst_, dst_mask_;
    Rect dst_roi_;
};

// GPU NoBlender , CV_8UC4, no stream
class NoBlenderGPU : public cv::detail::Blender {
public:
    void prepare(const std::vector<Point> &corners, const std::vector<Size> &sizes) override {
        dst_roi_ = resultRoi(corners, sizes);
        dst_.create(dst_roi_.size(), CV_8UC4);
        dst_.setTo(Scalar::all(0));
        dst_mask_.create(dst_roi_.size(), CV_8U);
        dst_mask_.setTo(Scalar::all(0));
    }

    void feed(const cv::cuda::GpuMat& d_img, const cv::cuda::GpuMat& d_mask, Point tl) {
        CV_Assert(d_img.type()  == CV_8UC4);
        CV_Assert(d_mask.type() == CV_8UC1);

        int dx = tl.x - dst_roi_.x;
        int dy = tl.y - dst_roi_.y;

        launchNoBlenderKernelRGBA(
            d_img.ptr<uchar4>(), (int)d_img.step,
            d_mask.ptr<uchar>(), (int)d_mask.step,
            dst_.ptr<uchar4>(), (int)dst_.step,
            dst_mask_.ptr<uchar>(), (int)dst_mask_.step,
            dx, dy, d_img.cols, d_img.rows
        );
    }

    cv::cuda::GpuMat blend() { return dst_; }

private:
    cv::cuda::GpuMat dst_, dst_mask_;
    Rect dst_roi_;
};
#endif

// GPU NoBlender , CV_8UC4, with stream
class NoBlenderGPU : public cv::detail::Blender {
public:
    void prepare(const std::vector<Point> &corners, const std::vector<Size> &sizes) override {
        dst_roi_ = resultRoi(corners, sizes);
        dst_.create(dst_roi_.size(), CV_8UC4);
        dst_.setTo(Scalar::all(0));
        dst_mask_.create(dst_roi_.size(), CV_8U);
        dst_mask_.setTo(Scalar::all(0));
    }

    void feed(const cv::cuda::GpuMat& d_img,
              const cv::cuda::GpuMat& d_mask,
              Point tl,
              cv::cuda::Stream& stream)
    {
        CV_Assert(d_img.type()  == CV_8UC4);
        CV_Assert(d_mask.type() == CV_8UC1);

        int dx = tl.x - dst_roi_.x;
        int dy = tl.y - dst_roi_.y;

        cudaStream_t s = cv::cuda::StreamAccessor::getStream(stream);

        launchNoBlenderKernelRGBA(
            d_img.ptr<uchar4>(), (size_t)d_img.step,
            d_mask.ptr<unsigned char>(), (size_t)d_mask.step,
            dst_.ptr<uchar4>(), (size_t)dst_.step,
            dst_mask_.ptr<unsigned char>(), (size_t)dst_mask_.step,
            dx, dy, d_img.cols, d_img.rows,
            dst_.cols, dst_.rows,
            s
        );
    }

    cv::cuda::GpuMat blend() { return dst_; }

private:
    cv::cuda::GpuMat dst_, dst_mask_;
    Rect dst_roi_;
};


// using BlenderPtr = Ptr<Blender>;
using BlenderPtr = Ptr<NoBlenderGPU>;
std::queue<BlenderPtr> blender_ptr_pool;
std::queue<BlenderPtr> blender_prepare_queue;    // 等待 prepare 的 blender

struct InferPanoSlot {
    cv::cuda::GpuMat pano_rgba;              // fixed device buffer
    cudaEvent_t copy_done = nullptr;         // recorded after apply thread copies into this slot
    std::atomic<bool> busy{false};           // true: owned by worker / queued
};

struct InferJob {
    int slot_idx = -1;
    std::uint64_t frame_seq = 0;
};

struct GainBlocksSlot {
    int block_size = 4;                    // 跟 compensator 設定一致
    int grid_w = 0, grid_h = 0;             // 以 pano ROI（dst_roi）算出來
    std::array<cv::cuda::GpuMat, 4> g_gain; // CV_32FC1, size = (grid_h, grid_w)
    std::shared_mutex mtx;                  // 更新/讀取用
};

struct SeamShared {
	std::atomic<uint64_t> seam_epoch{0};
    std::atomic<uint64_t> seam_count{0};
    std::atomic<uint64_t> seam_c_base{30};          // 依你原本邏輯調整
    std::atomic<bool> SEAM_COUNT_RESET{true};
    // cam0 用來算 MSE 的上一張 gray（seam scale），只在 cam0 thread 用即可
    cv::Mat prev_gray;
    bool prev_gray_valid = false;
};

struct CamCtx {
    // ---- NEW: device ownership ----
    std::string dev;
    int fd = -1;                 // owns this fd, closed in cc_stop_all()

    GstElement* pipeline = nullptr;
    GstElement* sink_elem = nullptr;
    GstAppSink* sink = nullptr;

    // std::unique_ptr<StageQueue<k180::brighttuner::BrightJobPtr>> cc2bright;
    std::unique_ptr<StageQueue<FramePtr>> cc2bright;
    std::unique_ptr<StageQueue<FramePtr>> q_blend;
    std::unique_ptr<StageQueue<FramePtr>> q_seam;

    std::thread t_cap;
    std::atomic<uint64_t> seq{0};
	
    int in_w = 0;
    int in_h = 0;

    int warp_w = 0;
    int warp_h = 0;

    cv::cuda::GpuMat g_map1, g_map2; // GPU maps for remap
    cv::Size img_size{1920,1080};
    cv::cuda::Stream stream;         // per-cam stream
    GpuMatPool warp_pool;
    int warp_pool_slots = 6;
};

struct CC_entry_Ctx {
    std::atomic<bool> running{false};
    std::array<CamCtx, 4> cams;

    SeamShared seam_shared;
    std::array<std::thread, 4> t_seam_prep;
    std::thread t_seam;

    // cv::Ptr<cv::detail::ExposureCompensator> compensator;
	Ptr<SeamFinder> seam_finder;
    Ptr<ExposureCompensator> compensator;
    int compensator_block = 4;	//32;

    std::array<GainBlocksSlot, 2> gain_shared;
    std::atomic<int> gain_front{0};              // 目前可讀的 slot
    std::atomic<uint64_t> gain_epoch{0};         // 對外公告「已有新版本」
	
    std::thread _t_bri_adj;
    std::vector<std::thread> prepare_threads;
    std::vector<std::thread> apply_threads;
	
	std::unique_ptr<ImgUDPGateway> srv;

    static constexpr int infer_pool_size = 4;
    std::array<InferPanoSlot, infer_pool_size> infer_pool;
    std::unique_ptr<StageQueue<InferJob>> infer_q;
	
	std::thread _t_inf;
	k180::osd::OsdShared osd_shared;
};

double computeMSE_gray_gpu(const cv::cuda::GpuMat& a,
                           const cv::cuda::GpuMat& b,
                           cv::cuda::Stream& stream)
{
    CV_Assert(a.type() == b.type());
    CV_Assert(a.size() == b.size());
    CV_Assert(a.channels() == 1);

    cv::cuda::GpuMat af, bf, diff;

    if (a.depth() == CV_32F) {
        af = a; bf = b;
    } else {
        a.convertTo(af, CV_32F, 1.0, 0.0, stream);
        b.convertTo(bf, CV_32F, 1.0, 0.0, stream);
    }

    cv::cuda::subtract(af, bf, diff, cv::noArray(), CV_32F, stream);

    // sqrSum 在 4.10 沒有 stream 版本，所以要先確保 diff 這個 stream 的工作已完成
    stream.waitForCompletion();

    cv::Scalar sq = cv::cuda::sqrSum(diff); // (src, mask=noArray())
    return sq[0] / (static_cast<double>(a.rows) * a.cols);
}

static bool init_stitch_assets_all(CC_entry_Ctx& cec)
{
    auto& s = k180::runtime::rt().sdp;
    auto& v = k180::runtime::rt().sdv;

    for (int id = 0; id < (int)cec.cams.size(); ++id) {
        CamCtx& cam = cec.cams[id];

        // 1) prepare mask
        cv::Mat mask(cam.img_size, CV_8U);
        mask.setTo(cv::Scalar::all(255));
		// overlap 大約 270
int x_1 = cfggg.mask_cut; // 小小跳,   200:會一直重算 , 220:還是會跳  20:會把不必要的 補償帶到另一張圖的overlap位置，20 也就是 沒「卡」的概念
if( id == 1 || id == 2 || id == 3 ) {
	mask.colRange(0, x_1).setTo(0);
}
if( id == 0 || id == 1 || id == 2 ) {
	mask.colRange((mask.cols-x_1), mask.cols).setTo(0);
}
        // camera params (copy)
        cv::detail::CameraParams local_cameras = v.cameras_sd[id];
        cv::Mat K_u;
        local_cameras.K().convertTo(K_u, CV_32F);
        cv::Mat R = local_cameras.R.clone();

        // 2) warp mask -> seam mask (CPU)
        cv::Mat local_mask_warped;

        v.warper_init_cam->warp(mask, K_u, R,
                                cv::INTER_NEAREST, cv::BORDER_CONSTANT,
                                local_mask_warped);

        cv::Mat comp_scaled;
        cv::resize(local_mask_warped, comp_scaled,
                   cv::Size(), s.seam_scale, s.seam_scale,
                   cv::INTER_LINEAR_EXACT);
		comp_scaled.copyTo(v.meta_masks_warp_comp_u[id]);

		// int x_1 = 200; // 小小跳,   200:會一直重算 , 220:還是會跳  20:會把不必要的 補償帶到另一張圖的overlap位置，20 也就是 沒「卡」的概念
		// if( id == 1 || id == 2 || id == 3 ) {
			// local_mask_warped.colRange(0, x_1).setTo(0);
		// }
		// if( id == 0 || id == 1 || id == 2 ) {
			// local_mask_warped.colRange((local_mask_warped.cols-x_1), local_mask_warped.cols).setTo(0);
		// }
		local_mask_warped.copyTo( v.meta_masks_warp_orig_u[id] );
		local_mask_warped.copyTo( v.meta_masks_warp_orig_c[id] );
		
        cv::Mat seam_scaled;
        cv::resize(local_mask_warped, seam_scaled,
                   cv::Size(), s.seam_scale, s.seam_scale,
                   cv::INTER_LINEAR_EXACT);
		seam_scaled.copyTo(v.meta_masks_warp_resize_u[id]);

        // cam.seam_mask_u = seam_scaled;
v.mask_warped_g[0][id].upload(local_mask_warped);
v.mask_warped_g[1][id].upload(local_mask_warped);
v.meta_mask_warped_g[id].upload(local_mask_warped);

        // 3) build maps -> upload to GPU
        cv::Ptr<cv::WarperCreator> warper_creator_cc = cv::makePtr<cv::SphericalWarperGpu>();
        cv::Ptr<cv::detail::RotationWarper> warper_cc =
            warper_creator_cc->create(static_cast<float>(v.warped_image_scale_sd));

        cv::Mat map_x, map_y;
        warper_cc->buildMaps(mask.size(), K_u, R, map_x, map_y);
if (map_x.empty() || map_y.empty() || map_x.size() != map_y.size()) {
    fprintf(stderr, "[CAM%d] invalid warp maps\n", id);
    return false;
}
if (map_x.type() != CV_32FC1 || map_y.type() != CV_32FC1) {
    fprintf(stderr, "[CAM%d] warp map type must be CV_32FC1\n", id);
    return false;
}
cam.warp_w = map_x.cols;
cam.warp_h = map_x.rows;

        cam.g_map1.upload(map_x);
        cam.g_map2.upload(map_y);
    }
    return true;
}

static inline Ptr<SeamFinder>
create_seam_finder(const SD_process& s)
{
    using namespace cv;
    using namespace cv::detail;

    Ptr<SeamFinder> seam_finder;

    if (s.seam_find_type == "no")
        seam_finder = makePtr<NoSeamFinder>();
    else if (s.seam_find_type == "voronoi")
        seam_finder = makePtr<VoronoiSeamFinder>();
    else if (s.seam_find_type == "gc_color")
    {
// #ifdef HAVE_OPENCV_CUDALEGACY
        // if (s.try_cuda && cv::cuda::getCudaEnabledDeviceCount() > 0)
            // seam_finder = makePtr<GraphCutSeamFinderGpu>(GraphCutSeamFinderBase::COST_COLOR);
        // else
// #endif
            seam_finder = makePtr<GraphCutSeamFinder>(GraphCutSeamFinderBase::COST_COLOR);
    }
    else if (s.seam_find_type == "gc_colorgrad")
    {
// #ifdef HAVE_OPENCV_CUDALEGACY
        // if (s.try_cuda && cv::cuda::getCudaEnabledDeviceCount() > 0)
            // seam_finder = makePtr<GraphCutSeamFinderGpu>(GraphCutSeamFinderBase::COST_COLOR_GRAD);
        // else
// #endif
            seam_finder = makePtr<GraphCutSeamFinder>(GraphCutSeamFinderBase::COST_COLOR_GRAD);
    }
    else if (s.seam_find_type == "dp_color")
        seam_finder = makePtr<DpSeamFinder>(DpSeamFinder::COLOR);
    else if (s.seam_find_type == "dp_colorgrad")
        seam_finder = makePtr<DpSeamFinder>(DpSeamFinder::COLOR_GRAD);

    if (!seam_finder) {
        std::cerr << "Can't create seam finder: '" << s.seam_find_type << "'\n";
    }
    return seam_finder;
}

struct NvmmGstPool {
    GstBufferPool* pool_ = nullptr;
    GstCaps* caps_ = nullptr;

    bool init(int w, int h, int minb, int maxb) {
        // caps: NVMM RGBA + memory:NVMM
        caps_ = gst_caps_new_simple("video/x-raw",
            "format", G_TYPE_STRING, "RGBA",
            "width",  G_TYPE_INT, w,
            "height", G_TYPE_INT, h,
            NULL);
        GstCapsFeatures* f = gst_caps_features_new("memory:NVMM", NULL);
        gst_caps_set_features(caps_, 0, f); // caps takes ownership of f

        pool_ = gst_nvds_buffer_pool_new();
        if (!pool_) {
            gst_caps_unref(caps_);
            caps_ = nullptr;
            return false;
        }

        GstStructure* cfg = gst_buffer_pool_get_config(pool_);

        // ✅ 注意：這裡 size=sizeof(NvBufSurface) 是 DeepStream nvds pool 的常見作法
        //    （你的 map 出來會得到 NvBufSurface*）
        gst_buffer_pool_config_set_params(cfg, caps_, sizeof(NvBufSurface), minb, maxb);

        // optional
        gst_buffer_pool_config_add_option(cfg, GST_BUFFER_POOL_OPTION_NVDS_META);
        gst_buffer_pool_config_add_option(cfg, GST_BUFFER_POOL_OPTION_VIDEO_META);

        if (!gst_buffer_pool_set_config(pool_, cfg)) return false;
        if (!gst_buffer_pool_set_active(pool_, TRUE)) return false;
        return true;
    }

    GstBuffer* acquire() {
        if (!pool_) return nullptr;
        GstBuffer* b = nullptr;
		//  if (gst_buffer_pool_acquire_buffer(pool_, &b, nullptr) != GST_FLOW_OK) return nullptr; // -- 然後加入下面三行
        GstBufferPoolAcquireParams params{};
        params.flags = GST_BUFFER_POOL_ACQUIRE_FLAG_DONTWAIT;
        if (gst_buffer_pool_acquire_buffer(pool_, &b, &params) != GST_FLOW_OK) return nullptr;
        return b;
    }

    void destroy() {
        if (pool_) {
            gst_buffer_pool_set_active(pool_, FALSE);
            gst_object_unref(pool_);
            pool_ = nullptr;
        }
        if (caps_) {
            gst_caps_unref(caps_);
            caps_ = nullptr;
        }
    }

    ~NvmmGstPool() { destroy(); }
};

// =========================================================
// GstBuffer (NVMM RGBA) -> CUDA GpuMat (zero copy)
// =========================================================
// 修法B：以 GstBuffer* 當 key（camera buffer pool 重用，指標通常穩定）
struct InCuCacheEntry {
    bool inited = false;

    NvBufSurface* surf = nullptr;
    int idx = 0;

    EGLImageKHR egl = nullptr;
    CUgraphicsResource res = nullptr;

    int w = 0;
    int h = 0;
};

static thread_local std::unordered_map<GstBuffer*, InCuCacheEntry> tl_in_cu_cache;

static inline void clear_input_cures_cache()
{
    for (auto& kv : tl_in_cu_cache) {
        InCuCacheEntry& e = kv.second;
        if (!e.inited) continue;

        if (e.res) {
            cuGraphicsUnregisterResource(e.res);
            e.res = nullptr;
        }
        if (e.surf) {
            // 我們在 init 時 MapEglImage 後「長期保持」，所以要在這裡統一 UnMap
            NvBufSurfaceUnMapEglImage(e.surf, e.idx);
            e.surf = nullptr;
        }
        e.egl = nullptr;
        e.inited = false;
    }
    tl_in_cu_cache.clear();
}

static bool gstbuffer_to_gpu_rgba(GstBuffer* buf, FrameGpuRGBA& out, int cam_id)
{
    // 這裡仍然先清掉 out（但我們不會再用 out 來做 UnMap/Unregister）
    release_frame(out);
    if (!buf) return false;

    // 1) map GstBuffer -> NvBufSurface*（只為了拿 surf/eglImage，立刻 unmap）
    GstMapInfo mi = GST_MAP_INFO_INIT;
    if (!gst_buffer_map(buf, &mi, GST_MAP_READ)) {
        GSTD("[CAM%d][MAP] gst_buffer_map failed\n", cam_id);
        return false;
    }
    if (mi.size < sizeof(NvBufSurface)) {
        GSTD("[CAM%d][MAP] map size too small: %zu\n", cam_id, (size_t)mi.size);
        gst_buffer_unmap(buf, &mi);
        return false;
    }

    NvBufSurface* surf = (NvBufSurface*)mi.data;
    const int idx = 0;

    // 2) cache entry（key = GstBuffer*）
    InCuCacheEntry& e = tl_in_cu_cache[buf];

    auto reinit_entry = [&](){
        // 清掉舊的（若有）
        if (e.inited) {
            if (e.res) {
                cuGraphicsUnregisterResource(e.res);
                e.res = nullptr;
            }
            if (e.surf) {
                NvBufSurfaceUnMapEglImage(e.surf, e.idx);
                e.surf = nullptr;
            }
            e.egl = nullptr;
            e.inited = false;
        }

        // Map EGLImage「一次並保持」
        int rc = NvBufSurfaceMapEglImage(surf, idx);
        if (rc != 0) {
            GSTD("[CAM%d][MAP] NvBufSurfaceMapEglImage rc=%d\n", cam_id, rc);
            return false;
        }

        EGLImageKHR egl = (EGLImageKHR)surf->surfaceList[idx].mappedAddr.eglImage;
        if (!egl) {
            GSTD("[CAM%d][MAP] eglImage null\n", cam_id);
            NvBufSurfaceUnMapEglImage(surf, idx);
            return false;
        }

        CUgraphicsResource res = nullptr;
        CUresult cr = cuGraphicsEGLRegisterImage(&res, egl,
                                                 CU_GRAPHICS_MAP_RESOURCE_FLAGS_READ_ONLY);
        if (cr != CUDA_SUCCESS || !res) {
            const char* s = nullptr; cuGetErrorString(cr, &s);
            GSTD("[CAM%d][MAP] cuGraphicsEGLRegisterImage failed: %s\n", cam_id, s ? s : "unknown");
            NvBufSurfaceUnMapEglImage(surf, idx);
            return false;
        }

        // 記錄 entry（長期持有）
        e.inited = true;
        e.surf = surf;
        e.idx  = idx;
        e.egl  = egl;
        e.res  = res;
        e.w    = (int)surf->surfaceList[idx].width;
        e.h    = (int)surf->surfaceList[idx].height;
        return true;
    };

    // 3) 若 entry 尚未 init，或 surf/eglImage 變了就重建
    //    （通常 GstBuffer* 對應同一塊 backing，但保守做驗證）
    EGLImageKHR cur_egl = (EGLImageKHR)surf->surfaceList[idx].mappedAddr.eglImage;

    bool need_reinit = false;
    if (!e.inited) need_reinit = true;
    else if (e.surf != surf) need_reinit = true;
    else if (cur_egl && e.egl != cur_egl) need_reinit = true;

    if (need_reinit) {
        if (!reinit_entry()) {
            gst_buffer_unmap(buf, &mi);
            return false;
        }
    }

    // 4) mapinfo 不需要保留（我們不靠 out.release_frame 來管理 input）
    gst_buffer_unmap(buf, &mi);

    // 5) 每幀取 mapped egl frame（必要）
    CUeglFrame eglFrame{};
    CUresult cr = cuGraphicsResourceGetMappedEglFrame(&eglFrame, e.res, 0, 0);
    if (cr != CUDA_SUCCESS) {
        const char* s = nullptr; cuGetErrorString(cr, &s);
        GSTD("[CAM%d][MAP] cuGraphicsResourceGetMappedEglFrame failed: %s\n", cam_id, s ? s : "unknown");

        // 失敗代表 entry 可能壞了：丟掉，讓下一幀重建
        clear_input_cures_cache(); // 最粗暴但最乾淨；你也可只清掉此 buf 的 entry
        return false;
    }
    if (eglFrame.frameType != CU_EGL_FRAME_TYPE_PITCH) {
        GSTD("[CAM%d][MAP] eglFrame not PITCH (type=%d)\n", cam_id, (int)eglFrame.frameType);
        return false;
    }

    unsigned char* devPtr = (unsigned char*)eglFrame.frame.pPitch[0];
    size_t pitch = eglFrame.pitch;
    if (!devPtr || pitch == 0) {
        GSTD("[CAM%d][MAP] devPtr=%p pitch=%zu invalid\n", cam_id, devPtr, pitch);
        return false;
    }

    // 6) 重要：把 out 設成「不負責 UnMap/Unregister」
    out.cudaRes = e.res;

    out.rgba = cv::cuda::GpuMat(e.h, e.w, CV_8UC4, devPtr, pitch);
    return true;
}


struct NvmmCudaCacheEntry {
    bool inited = false;

    // backing surface
    NvBufSurface* surf = nullptr;
    int idx = 0;

    // egl/cuda interop
    EGLImageKHR eglImage = nullptr;
    CUgraphicsResource cuRes = nullptr;

    // mapped destination (device pointer + pitch)
    uint8_t* dstDev = nullptr;
    size_t   dstPitch = 0;

    // frame geometry (debug)
    int w = 0;
    int h = 0;

    // sync fence
    cudaEvent_t done = nullptr;

    // meta added once
    bool meta_added = false;
};

static thread_local std::unordered_map<GstBuffer*, NvmmCudaCacheEntry> tl_nvmm_cache;

static bool nvmm_cache_init_for_outbuf(
    GstBuffer* outbuf,
    int tid_for_log,
    NvmmCudaCacheEntry& e)
{
    if (!outbuf) return false;

    GstMapInfo mi = GST_MAP_INFO_INIT;
    if (!gst_buffer_map(outbuf, &mi, GST_MAP_READWRITE)) {
        fprintf(stderr, "[V1234][T%d] gst_buffer_map failed (init cache)\n", tid_for_log);
        return false;
    }

    NvBufSurface* surf = (NvBufSurface*)mi.data;
    if (!surf) {
        gst_buffer_unmap(outbuf, &mi);
        fprintf(stderr, "[V1234][T%d] surf==nullptr (init cache)\n", tid_for_log);
        return false;
    }

    // defensive
    if (surf->batchSize == 0) surf->batchSize = 1;
    if (surf->numFilled == 0) surf->numFilled = 1;

    const int idx = 0;
    // 1) EGL map ONCE and KEEP it mapped (do NOT unmap per-frame)
    int rc = NvBufSurfaceMapEglImage(surf, idx);
    if (rc != 0) {
        gst_buffer_unmap(outbuf, &mi);
        fprintf(stderr, "[V1234][T%d] NvBufSurfaceMapEglImage failed rc=%d\n", tid_for_log, rc);
        return false;
    }

    EGLImageKHR eglImage = (EGLImageKHR)surf->surfaceList[idx].mappedAddr.eglImage;
    if (!eglImage) {
        // cleanup map egl (since init failed)
        NvBufSurfaceUnMapEglImage(surf, idx);
        gst_buffer_unmap(outbuf, &mi);
        fprintf(stderr, "[V1234][T%d] eglImage==nil after MapEglImage (init cache)\n", tid_for_log);
        return false;
    }

    // 2) register ONCE
    CUgraphicsResource cuRes = nullptr;
    CUresult cr = cuGraphicsEGLRegisterImage(&cuRes, eglImage, CU_GRAPHICS_MAP_RESOURCE_FLAGS_NONE);
    if (cr != CUDA_SUCCESS || !cuRes) {
        NvBufSurfaceUnMapEglImage(surf, idx);
        gst_buffer_unmap(outbuf, &mi);
        fprintf(stderr, "[V1234][T%d] cuGraphicsEGLRegisterImage failed cr=%d\n", tid_for_log, (int)cr);
        return false;
    }

    // 3) get mapped egl frame ONCE
    CUeglFrame eglFrame{};
    cr = cuGraphicsResourceGetMappedEglFrame(&eglFrame, cuRes, 0, 0);
    if (cr != CUDA_SUCCESS || eglFrame.frameType != CU_EGL_FRAME_TYPE_PITCH) {
        cuGraphicsUnregisterResource(cuRes);
        NvBufSurfaceUnMapEglImage(surf, idx);
        gst_buffer_unmap(outbuf, &mi);
        fprintf(stderr, "[V1234][T%d] cuGraphicsResourceGetMappedEglFrame failed cr=%d type=%d\n",
                tid_for_log, (int)cr, (int)eglFrame.frameType);
        return false;
    }

    uint8_t* dstDev = (uint8_t*)eglFrame.frame.pPitch[0];
    size_t   dstPitch = (size_t)eglFrame.pitch;
    if (!dstDev || dstPitch == 0) {
        cuGraphicsUnregisterResource(cuRes);
        NvBufSurfaceUnMapEglImage(surf, idx);
        gst_buffer_unmap(outbuf, &mi);
        fprintf(stderr, "[V1234][T%d] dstDev/dstPitch invalid (init cache)\n", tid_for_log);
        return false;
    }

    // 4) create event ONCE
    cudaEvent_t ev = nullptr;
    cudaError_t ce = cudaEventCreateWithFlags(&ev, cudaEventDisableTiming);
    if (ce != cudaSuccess) {
        cuGraphicsUnregisterResource(cuRes);
        NvBufSurfaceUnMapEglImage(surf, idx);
        gst_buffer_unmap(outbuf, &mi);
        fprintf(stderr, "[V1234][T%d] cudaEventCreate failed: %s\n", tid_for_log, cudaGetErrorString(ce));
        return false;
    }

    // fill cache entry
    e.inited = true;
    e.surf = surf;
    e.idx = idx;
    e.eglImage = eglImage;
    e.cuRes = cuRes;
    e.dstDev = dstDev;
    e.dstPitch = dstPitch;
    e.w = (int)surf->surfaceList[idx].width;
    e.h = (int)surf->surfaceList[idx].height;
    e.done = ev;
    e.meta_added = false;

    // IMPORTANT:
    // - We KEEP NvBufSurfaceMapEglImage mapped (no UnMap here).
    // - But gst_buffer_map is only for getting pointer; unmap it now.
    gst_buffer_unmap(outbuf, &mi);
    return true;
}

static bool nvmm_cache_validate_or_reinit(GstBuffer* outbuf, int tid, NvmmCudaCacheEntry& e)
{
    GstMapInfo mi = GST_MAP_INFO_INIT;
    if (!gst_buffer_map(outbuf, &mi, GST_MAP_READWRITE)) return false;

    NvBufSurface* surf = (NvBufSurface*)mi.data;
    const int idx = 0;

    // 確保有 eglImage（若你 init 時 MapEglImage 長期保持，通常這裡會有）
    EGLImageKHR egl = (EGLImageKHR)surf->surfaceList[idx].mappedAddr.eglImage;

    gst_buffer_unmap(outbuf, &mi);

    if (!e.inited) return nvmm_cache_init_for_outbuf(outbuf, tid, e);

    if (egl && e.eglImage == egl) return true;

    // eglImage 變了：清掉舊 cache，重建
    if (e.cuRes) cuGraphicsUnregisterResource(e.cuRes);
    if (e.done)  cudaEventDestroy(e.done);
    e = NvmmCudaCacheEntry{};
    return nvmm_cache_init_for_outbuf(outbuf, tid, e);
}

// ---- per-frame copy using cached dstDev/dstPitch + event fence ----
static bool nvmm_copy_rgba_to_outbuf_cached(
    const cv::cuda::GpuMat& srcRGBA,
    GstBuffer* outbuf,
    int tid_for_log,
    cudaStream_t stream /* can be nullptr */)
{
    if (!outbuf) return false;
    if (srcRGBA.empty() || srcRGBA.type() != CV_8UC4) return false;

    NvmmCudaCacheEntry& e = tl_nvmm_cache[outbuf];
#if 0
    if (!e.inited) {
        if (!nvmm_cache_init_for_outbuf(outbuf, tid_for_log, e)) {
            // remove failed entry to avoid reusing broken state
            tl_nvmm_cache.erase(outbuf);
            return false;
        }
    }
#endif
	if (!nvmm_cache_validate_or_reinit(outbuf, tid_for_log, e)) {
		tl_nvmm_cache.erase(outbuf);
		return false;
	}
    // sanity size (optional but recommended)
    const size_t rowBytes = (size_t)srcRGBA.cols * 4;
    const size_t rows     = (size_t)srcRGBA.rows;
    if (rowBytes == 0 || rows == 0) return false;

    cudaStream_t use_stream = stream ? stream : (cudaStream_t)0;

    cudaError_t ce = cudaMemcpy2DAsync(
        e.dstDev, e.dstPitch,
        srcRGBA.ptr<uint8_t>(), (size_t)srcRGBA.step,
        rowBytes, rows,
        cudaMemcpyDeviceToDevice,
        use_stream);

    if (ce != cudaSuccess) {
        fprintf(stderr, "[V1234][T%d] cudaMemcpy2DAsync failed: %s\n",
                tid_for_log, cudaGetErrorString(ce));
        return false;
    }

#if NVMM_COPY_USE_EVENT_FENCE
    ce = cudaEventRecord(e.done, use_stream);
    if (ce != cudaSuccess) {
        fprintf(stderr, "[V1234][T%d] cudaEventRecord failed: %s\n",
                tid_for_log, cudaGetErrorString(ce));
        return false;
    }

    ce = cudaEventSynchronize(e.done);
    if (ce != cudaSuccess) {
        fprintf(stderr, "[V1234][T%d] cudaEventSynchronize failed: %s\n",
                tid_for_log, cudaGetErrorString(ce));
        return false;
    }
	// NvBufSurfaceSyncForDevice(e.surf, e.idx, 0);
#else
    ce = cudaStreamSynchronize(use_stream);
    if (ce != cudaSuccess) {
        fprintf(stderr, "[V1234][T%d] cudaStreamSynchronize failed: %s\n",
                tid_for_log, cudaGetErrorString(ce));
        return false;
    }
#endif

    return true;
}

// ---- add VideoMeta ONCE per outbuf (avoid stacking metas) ----
static void nvmm_add_video_meta_once(
    GstBuffer* outbuf,
    int tid_for_log,
    int width, int height,
    int pitch)
{
    if (!outbuf) return;

    auto it = tl_nvmm_cache.find(outbuf);
    if (it == tl_nvmm_cache.end()) return;
    NvmmCudaCacheEntry& e = it->second;
    if (!e.inited || e.meta_added) return;

    gsize offsets[GST_VIDEO_MAX_PLANES] = {0,0,0,0};
    gint  strides[GST_VIDEO_MAX_PLANES] = {pitch,0,0,0};

    gst_buffer_add_video_meta_full(
        outbuf,
        (GstVideoFrameFlags)0,
        GST_VIDEO_FORMAT_RGBA,
        (guint)width,
        (guint)height,
        1,
        offsets,
        strides
    );

    e.meta_added = true;
}

static bool init_infer_pool(CC_entry_Ctx& cec)
{
    for (auto& s : cec.infer_pool) {
        s.pano_rgba.create(stream_out_h_1234_1080,
                           stream_out_w_1234_1080,
                           CV_8UC4);

        cudaError_t ce = cudaEventCreateWithFlags(&s.copy_done, cudaEventDisableTiming);
        if (ce != cudaSuccess) {
            fprintf(stderr,
                    "[infer_pool] cudaEventCreateWithFlags failed: %s\n",
                    cudaGetErrorString(ce));
            return false;
        }

        s.busy.store(false, std::memory_order_relaxed);
    }

    cec.infer_q = std::make_unique<StageQueue<InferJob>>(CC_entry_Ctx::infer_pool_size);
    return true;
}

static int try_acquire_infer_slot(CC_entry_Ctx& cec)
{
    for (int i = 0; i < CC_entry_Ctx::infer_pool_size; ++i) {
        bool expected = false;
        if (cec.infer_pool[i].busy.compare_exchange_strong(
                expected, true, std::memory_order_acq_rel)) {
            return i;
        }
    }
    return -1;
}

static void thread_infer_worker(
    CC_entry_Ctx& cec,
    std::atomic<bool>& running)
{
    k180::runtime::cuda_set_current_for_thread("thread_infer_worker");

    std::vector<Detection> res;
    std::vector<Detection> res_track;
    BYTETracker tracker_(cfggg.track_fps, cfggg.track_frame_rate);

    InferJob job;

    while (running.load(std::memory_order_relaxed)) {
        if (!cec.infer_q->pop(job)) break;

        if (job.slot_idx < 0 || job.slot_idx >= CC_entry_Ctx::infer_pool_size) {
            continue;
        }

        auto& slot = cec.infer_pool[job.slot_idx];

        cudaError_t ce = cudaEventSynchronize(slot.copy_done);
        if (ce != cudaSuccess) {
            fprintf(stderr,
                    "[thread_infer_worker] cudaEventSynchronize(copy_done) failed: %s\n",
                    cudaGetErrorString(ce));
            slot.busy.store(false, std::memory_order_release);
            continue;
        }

        res.clear();
        res_track.clear();

        res = cec.srv->_inference_gpu(slot.pano_rgba, slot.copy_done);

        if (k180::ai::ai_should_draw_det(k180::ai::g_ai_rt)) {
            cec.srv->publish_detections_to_slot(res, job.frame_seq, cec.osd_shared);
        }

        if (k180::ai::ai_should_run_tracking(k180::ai::g_ai_rt)) {
            auto objects = convertDetectionsToObjects(res);
            auto output_tracks = tracker_.update(objects);

            updateTrackTable(res, output_tracks);
            res_track = buildTrackRenderResult(output_tracks);

            if (k180::ai::ai_should_draw_track(k180::ai::g_ai_rt)) {
                cec.srv->publish_track_results_to_slot(
                    res_track, job.frame_seq, cec.osd_shared);
            }
        }

        slot.busy.store(false, std::memory_order_release);
    }

    log_info_fmt("thread_infer_worker exit");
}


static void thread_cc_prepare_seam_input(
    int id,
	StageQueue<FramePtr>* q,
    SeamShared& ss,
    std::atomic<bool>& running)
{
	auto& s = k180::runtime::rt().sdp;
    auto& v = k180::runtime::rt().sdv;
    FramePtr f;

    cv::cuda::Stream stream;

    cv::cuda::GpuMat g_small_rgba, g_small_bgr;   // seam-scale RGBA (GPU)
    cv::cuda::GpuMat g_small_gray;   // seam-scale GRAY (GPU) for MSE
    cv::cuda::GpuMat prev_gray;      // only used by cam0
    bool prev_valid = false;

    cv::Mat cpu_rgba, cpu_rgb;		// reused buffers for download path
    cv::Mat cpu_bgr;				// reused buffers for download path

    uint64_t last_done_epoch = 0;
 
    while (running.load(std::memory_order_relaxed)) {
        // ★ 本輪開始先 snapshot 一次 epoch（決定本輪要不要做 seam input）
        const uint64_t epoch_snapshot =
            ss.seam_epoch.load(std::memory_order_acquire);

		if (!q->pop(f)) break;		//  pop 會 wait，如果沒人，會在這邊等
		if (f->warp_done_valid) {
			stream.waitEvent(f->warp_done);
		}
		cv::cuda::GpuMat g_full_rgba = f->warp_rgba;  // 全尺寸

		bool need_small_for_seam = (epoch_snapshot != 0 && epoch_snapshot != last_done_epoch);
		bool need_small_for_mse  = (id == 0);

		if (need_small_for_seam || need_small_for_mse) {
		// 這個 resize 最長 6ms,最短 0.02ms 但其實不準，因為這是CPU時間。但這個resize是在GPU做的
			cv::cuda::resize(g_full_rgba, g_small_rgba, cv::Size(),
							 s.seam_scale, s.seam_scale,
							 cv::INTER_LINEAR, stream);
		}

        // 2) ★ 本輪是否要做 download/copyTo/signal？
        //    只看 epoch_snapshot（因此「本輪觸發」不會「本輪消耗」）
		if (epoch_snapshot != 0 && epoch_snapshot != last_done_epoch) {
			// g_small_rgba 已經 resize 好了
			cv::cuda::cvtColor(g_small_rgba, g_small_bgr, cv::COLOR_RGBA2BGR, 0, stream);
			// cv::cuda::cvtColor(g_small_rgba, g_small_bgr, cv::COLOR_RGBA2RGB, 0, stream);

			g_small_bgr.download(cpu_bgr, stream);
			stream.waitForCompletion();

			cpu_bgr.copyTo(v.images_warped_resize_u[id]);

			seam_find_sync.signal_id(id);
			last_done_epoch = epoch_snapshot;
		}

        if (id == 0) {
            cv::cuda::cvtColor(g_small_rgba, g_small_gray,
                               cv::COLOR_RGBA2GRAY, 0, stream);

            if (prev_valid) {
                double mse = computeMSE_gray_gpu(prev_gray, g_small_gray, stream);

                if (mse >= cfggg.seam_mse_thresh) {
                    int b = ss.seam_c_base.load(std::memory_order_relaxed);
                    if (b > cfggg.seam_rate) ss.seam_c_base.store(b - 1, std::memory_order_relaxed);

                    int sc_now = ss.seam_count.load(std::memory_order_relaxed);
                    int b2 = ss.seam_c_base.load(std::memory_order_relaxed);
                    if (b2 > 0 && sc_now > b2) {
                        ss.SEAM_COUNT_RESET.store(true, std::memory_order_relaxed);
                    }
                } else {
                    int b = ss.seam_c_base.load(std::memory_order_relaxed);
                    if (b < 60) ss.seam_c_base.store(b + 1, std::memory_order_relaxed);
                }
            }

            g_small_gray.copyTo(prev_gray, stream);
            prev_valid = true;

            // 4) cam0 決策：本輪是否「觸發」下一輪要算 seam？
            int sc = ss.seam_count.fetch_add(1, std::memory_order_relaxed) + 1;
            int base = ss.seam_c_base.load(std::memory_order_relaxed);
            bool reset = ss.SEAM_COUNT_RESET.load(std::memory_order_relaxed);

            bool trigger = ((base > 0) && (sc % base == 0)) || reset;

            if (trigger) {
                ss.seam_count.store(0, std::memory_order_relaxed);
                ss.SEAM_COUNT_RESET.store(false, std::memory_order_relaxed);
                ss.seam_epoch.fetch_add(1, std::memory_order_release);	
	
            }
        }
    }
	log_info_fmt("thread_cc_prepare_seam_input exit");
}

static void thread_seam_find(
	SeamShared& ss,
    Ptr<SeamFinder>& seam_finder,
	Ptr<ExposureCompensator>& compensator,
	std::array<GainBlocksSlot, 2>& gain_shared,
	std::atomic<int>& gain_front,
	std::atomic<uint64_t>& gain_epoch,
	int compensator_block,
	std::atomic<bool>& running,  
    int num_images)
{
    auto& v = k180::runtime::rt().sdv;
	std::vector<cv::UMat> masks_comp(num_images);
    std::vector<cv::UMat> masks(num_images);

    for (int i = 0; i < num_images; ++i) {
        v.meta_masks_warp_resize_u[i].copyTo(masks[i]);
    }

	for (int i = 0; i < num_images; ++i)
		v.meta_masks_warp_comp_u[i].copyTo(masks_comp[i]);
		
    // special ROI（沿用你的）
    cv::Rect roi_1((2 * v.meta_masks_warp_resize_u[1].cols) / 3, 0,
                   v.meta_masks_warp_resize_u[1].cols / 3,
                   v.meta_masks_warp_resize_u[1].rows);
    cv::Rect roi_2(0, 0,
                   v.meta_masks_warp_resize_u[2].cols / 3,
                   v.meta_masks_warp_resize_u[2].rows);

    cv::UMat mask_save_1, mask_save_2;
    int re_seam_12 = 0;

    cv::UMat dilated_u, seam_u, out_u;

    uint64_t last_seen_epoch = 0;

    while (running.load(std::memory_order_relaxed)) {

        if (!seam_find_sync.wait_all(pipeline_sync_stop)) break;

        uint64_t e = ss.seam_epoch.load(std::memory_order_acquire);
        if (e == 0 || e == last_seen_epoch) {
            continue;
        }

		// compensator start


		compensator->feed(v.corners_resize, v.images_warped_resize_u, masks_comp);

		std::vector<cv::Mat> gains;
		compensator->getMatGains(gains);

		int front_idx = gain_front.load(std::memory_order_acquire);
		int back_idx  = 1 - front_idx;

		{
			auto& gs = gain_shared[back_idx];
			// 

			gs.block_size = compensator_block;
			gs.grid_w = gains[0].cols;
			gs.grid_h = gains[0].rows;

			for (int i = 0; i < num_images; ++i) {
				gs.g_gain[i].upload(gains[i]);
			}
		}
		gain_front.store(back_idx, std::memory_order_release);
		gain_epoch.fetch_add(1, std::memory_order_release);
		// compensator end
		
		// seam finder start
		for (int i = 0; i < num_images; ++i){
			v.images_warped_resize_u[i].convertTo(v.images_warped_resize_f[i], CV_32F);
		}
        seam_finder->find(v.images_warped_resize_f, v.corners_resize, masks);

        if (ss.seam_c_base.load(std::memory_order_relaxed) <= 10) {
            if (re_seam_12 % 10 == 0) {
                masks[1](roi_1).copyTo(mask_save_1);
                masks[2](roi_2).copyTo(mask_save_2);
                re_seam_12 = 0;
            } else {
                mask_save_1.copyTo(masks[1](roi_1));
                mask_save_2.copyTo(masks[2](roi_2));
            }
            re_seam_12++;
        }
		
		int back = 1 - v.mask_front.load(std::memory_order_acquire);
        for (int i = 0; i < num_images; ++i) {
            cv::dilate(masks[i], dilated_u, cv::Mat());
            cv::resize(dilated_u, seam_u, v.meta_masks_warp_orig_u[i].size(),
                       0, 0, cv::INTER_LINEAR_EXACT);
            cv::bitwise_and(seam_u, v.meta_masks_warp_orig_u[i], out_u);

            cv::Mat out_cpu = out_u.getMat(cv::ACCESS_READ);
            v.mask_warped_g[back][i].upload(out_cpu);
        }
		v.mask_front.store(back, std::memory_order_release);

        // reset masks 回 meta
        for (int i = 0; i < num_images; ++i) {
            v.meta_masks_warp_resize_u[i].copyTo(masks[i]);
        }
		// seam finder end

        last_seen_epoch = e;
    }
	log_info_fmt("thread_seam_find exit");
}

static void thread_blender_apply(
    int id,
    StageQueue<FramePtr>* q0,
    StageQueue<FramePtr>* q1,
    StageQueue<FramePtr>* q2,
    StageQueue<FramePtr>* q3,
    std::array<GainBlocksSlot, 2>& gain_shared,
    std::atomic<int>& gain_front,
    std::atomic<uint64_t>& gain_epoch,
    k180::HubManager& mgr,
	CC_entry_Ctx& cec,
	std::atomic<bool>& running)
{
	k180::runtime::cuda_set_current_for_thread("thread_blender_apply");
	int	gain_idx = 0;
	int front = 0;
    auto& v = k180::runtime::rt().sdv;
	// auto& s = k180::runtime::rt().sdp;
    int gap_w = (stream_out_w_1234_1080 - cfggg.pic_w_1234) / 2;
    int gap_h = (stream_out_h_1234_1080 - cfggg.pic_h_1234) / 2;

    cv::cuda::GpuMat c1234_tmp(stream_out_h_1234_1080, stream_out_w_1234_1080, CV_8UC4, cv::Scalar::all(0));
    cv::Rect roi_rec(gap_w, gap_h, cfggg.pic_w_1234, cfggg.pic_h_1234);
    cv::Rect roi_src(0, cfggg.pic_h_cut, cfggg.pic_w_1234, cfggg.pic_h_1234);

    cv::cuda::GpuMat target = c1234_tmp(roi_rec);
    cv::cuda::GpuMat result;

	static thread_local cv::cuda::GpuMat warp_rgba_comp[4];
    cv::cuda::Stream apply_cv_stream;
    cudaStream_t apply_stream = (cudaStream_t)apply_cv_stream.cudaPtr();

    // FramePtr last[4];
    // bool last_valid[4] = { false, false, false, false };
    static thread_local NvmmGstPool nvmm_gstpool;
    static thread_local bool nvmm_gstpool_ok = false;
    if (!nvmm_gstpool_ok) {
        if (!nvmm_gstpool.init(stream_out_w_1234_1080, stream_out_h_1234_1080,
                               /*min*/6, /*max*/6)) {
            fprintf(stderr, "[V1234][T%d] nvmm_gstpool init failed\n", id);
            return;
        }
        nvmm_gstpool_ok = true;
    }
#if 0 //測試 cur 釋放時機
	auto push_one_v1234 = [&](StreamGroup g, std::uint64_t frame_seq) {
		const int sid = stream_index({ g, StreamView::V1234 });

		GstBuffer* outbuf = nvmm_gstpool.acquire();
		if (!outbuf) return;

		bool ok = nvmm_copy_rgba_to_outbuf_cached(c1234_tmp, outbuf, id, apply_stream);
		if (!ok) {
			gst_buffer_unref(outbuf);
			return;
		}

		// 保留：確保 copy 完成後，再補 video meta / frame tag / push
		cudaError_t ce = cudaStreamSynchronize(apply_stream);

		if (ce != cudaSuccess) {
			fprintf(stderr,
					"[V1234][T%d] cudaStreamSynchronize failed before meta attach: %s\n",
					id, cudaGetErrorString(ce));
			gst_buffer_unref(outbuf);
			return;
		}

		{
			auto it = tl_nvmm_cache.find(outbuf);
			if (it != tl_nvmm_cache.end() && it->second.inited) {
				const int pitch = static_cast<int>(it->second.dstPitch);
				nvmm_add_video_meta_once(outbuf, id,
										 stream_out_w_1234_1080,
										 stream_out_h_1234_1080,
										 pitch);
			} else {
				nvmm_add_video_meta_once(outbuf, id,
										 stream_out_w_1234_1080,
										 stream_out_h_1234_1080,
										 stream_out_w_1234_1080 * 4);
			}
		}

		// 新增：掛上 frame_seq，供 mux src probe 對齊 detection slot
		if (!k180_buffer_add_frame_tag_meta(outbuf, frame_seq)) {
			fprintf(stderr,
					"[V1234][T%d] failed to add frame tag meta, seq=%llu\n",
					id, (unsigned long long)frame_seq);
			// 不 return：影片仍然照送，只是這幀後續可能無法對應到 OSD
		}

		mgr.hubs[sid].push_nvmm_rgba_buffer(outbuf);

		// 保留原邏輯：
		// 若 push_nvmm_rgba_buffer 內部接手 GstBuffer ref，這裡不要 unref。
		// 目前依你既有程式風格，先維持不在此 gst_buffer_unref(outbuf)。
	};
#endif
static thread_local bool b_timing_init = false;
static thread_local char b_gstpool_acq[64];
static thread_local k180::dbgtime::StageTimingAcc b_gstpool_t;
if (!b_timing_init) {
	std::snprintf(b_gstpool_acq,  sizeof(b_gstpool_acq),  "nvmm_gstpool_acquire_%d", id);
	b_gstpool_t = k180::dbgtime::StageTimingAcc(b_gstpool_acq);
	b_timing_init = true;
}
    auto make_v1234_outbuf = [&]() -> GstBuffer* {
		
		
uint64_t b_acq_0 = k180::dbgtime::now_ns_raw();
        GstBuffer* outbuf = nvmm_gstpool.acquire();
        if (!outbuf) return nullptr;
uint64_t b_acq_1 = k180::dbgtime::now_ns_raw();
b_gstpool_t.add_ns(b_acq_1 - b_acq_0);

        bool ok = nvmm_copy_rgba_to_outbuf_cached(c1234_tmp, outbuf, id, apply_stream);

        if (!ok) {
            gst_buffer_unref(outbuf);
            return nullptr;
        }
		
		// start 這段是我自己加的 
/*		cudaError_t ce = cudaStreamSynchronize(apply_stream);

		if (ce != cudaSuccess) {
			fprintf(stderr,
					"[V1234][T%d] cudaStreamSynchronize failed before meta attach: %s\n",
					id, cudaGetErrorString(ce));
			gst_buffer_unref(outbuf);
			return nullptr;
		}
		// end 這段是我自己加的 
*/
  	// meta ONCE
        {
            auto it = tl_nvmm_cache.find(outbuf);
            if (it != tl_nvmm_cache.end() && it->second.inited) {
                const int pitch = (int)it->second.dstPitch;
                nvmm_add_video_meta_once(outbuf, id,
                                         stream_out_w_1234_1080,
                                         stream_out_h_1234_1080,
                                         pitch);
            } else {
                // defensive fallback: tight pitch
                nvmm_add_video_meta_once(outbuf, id,
                                         stream_out_w_1234_1080,
                                         stream_out_h_1234_1080,
                                         stream_out_w_1234_1080 * 4);
            }
        }

        return outbuf;
    };

	while (running.load(std::memory_order_relaxed) &&
		   gain_epoch.load(std::memory_order_acquire) == 0) {
		usleep(1000);
	}

    while (running.load(std::memory_order_relaxed)) {

        if (!blender_apply_sync.wait_all(pipeline_sync_stop)) break;
	
         // --- 先更新 last-good（不阻塞） ---
        FramePtr cur[4];	// = { last[0], last[1], last[2], last[3] };
        // bool cur_valid[4] = { last_valid[0], last_valid[1], last_valid[2], last_valid[3] };
        bool cur_valid[4] = {false,false,false,false};

        FramePtr tmp;
        // if (q0->try_pop_latest(tmp)) { cur[0] = tmp; last[0] = tmp; last_valid[0] = true; cur_valid[0] = true; }
        // if (q1->try_pop_latest(tmp)) { cur[1] = tmp; last[1] = tmp; last_valid[1] = true; cur_valid[1] = true; }
        // if (q2->try_pop_latest(tmp)) { cur[2] = tmp; last[2] = tmp; last_valid[2] = true; cur_valid[2] = true; }
        // if (q3->try_pop_latest(tmp)) { cur[3] = tmp; last[3] = tmp; last_valid[3] = true; cur_valid[3] = true; }
        if (q0->try_pop_latest(tmp)) { cur[0] = tmp; cur_valid[0] = true; }
        if (q1->try_pop_latest(tmp)) { cur[1] = tmp; cur_valid[1] = true; }
        if (q2->try_pop_latest(tmp)) { cur[2] = tmp; cur_valid[2] = true; }
        if (q3->try_pop_latest(tmp)) { cur[3] = tmp; cur_valid[3] = true; }
        // warm-up：四路都至少有一張才做 blender
        if (!cur_valid[0] || !cur_valid[1] || !cur_valid[2] || !cur_valid[3]) {

            continue;
        }

        BlenderPtr ptr;
        {
            std::unique_lock<std::mutex> lk(bd_pool_mtx);
            if (blender_ptr_pool.empty()) {
                continue;
            }
            ptr = blender_ptr_pool.front();
            blender_ptr_pool.pop();
        }

        // 4) 等 remap 完成
        for (int i = 0; i < 4; ++i) {
            if (cur[i] && cur[i]->warp_done_valid) {
                apply_cv_stream.waitEvent(cur[i]->warp_done);
            }
        }
		front = v.mask_front.load(std::memory_order_acquire);

		gain_idx = gain_front.load(std::memory_order_acquire);
		auto& gs = gain_shared[gain_idx];
		{
			cudaStream_t s = cv::cuda::StreamAccessor::getStream(apply_cv_stream);

			for (int i = 0; i < 4; ++i) {
				const auto& src        = cur[i]->warp_rgba;
				const auto& valid_mask = v.meta_mask_warped_g[i];
				const auto& gain_grid  = gs.g_gain[i];

				if (warp_rgba_comp[i].empty() ||
					warp_rgba_comp[i].size() != src.size() ||
					warp_rgba_comp[i].type() != src.type()) {
					warp_rgba_comp[i].create(src.size(), src.type());
				}

				launchApplyGainBlocksRGBA_LocalGrid_IO_Bilinear(
					src.ptr<uchar4>(), (size_t)src.step,
					warp_rgba_comp[i].ptr<uchar4>(), (size_t)warp_rgba_comp[i].step,
					valid_mask.ptr<unsigned char>(), (size_t)valid_mask.step,
					src.cols, src.rows,
					gain_grid.ptr<float>(), (size_t)gain_grid.step,
					gs.grid_w, gs.grid_h,
					gs.block_size,
					(float)k180::runtime::rt().sdp.seam_scale,
					s
				);
			}
		}

		for (int i = 0; i < 4; ++i) {
			ptr->feed(warp_rgba_comp[i], v.mask_warped_g[front][i], v.corners_sd[i], apply_cv_stream);
			// ptr->feed(cur[i]->warp_rgba, v.mask_warped_g[front][i], v.corners_sd[i], apply_cv_stream);
		}


        result = ptr->blend();
        result(roi_src).copyTo(target, apply_cv_stream);

        if (id == 0 &&
            k180::ai::ai_should_run_detector_this_frame(k180::ai::g_ai_rt, cur[0]->seq)) {

            const int infer_slot_idx = try_acquire_infer_slot(cec);
			
			if (infer_slot_idx < 0) {
                log_info_fmt("[infer_submit][DROP_NO_SLOT] seq=%llu",
                             (unsigned long long)cur[0]->seq);
			} else {				 
                auto& infer_slot = cec.infer_pool[infer_slot_idx];

                cudaError_t ce = cudaMemcpy2DAsync(
                    infer_slot.pano_rgba.ptr<unsigned char>(),
                    infer_slot.pano_rgba.step,
                    c1234_tmp.ptr<unsigned char>(),
                    c1234_tmp.step,
                    static_cast<size_t>(c1234_tmp.cols) * 4,
                    static_cast<size_t>(c1234_tmp.rows),
                    cudaMemcpyDeviceToDevice,
                    apply_stream);

                if (ce != cudaSuccess) {
                    fprintf(stderr,
                            "[thread_blender_apply] cudaMemcpy2DAsync to infer slot failed: %s\n",
                            cudaGetErrorString(ce));
                    infer_slot.busy.store(false, std::memory_order_release);
                } else {
                    ce = cudaEventRecord(infer_slot.copy_done, apply_stream);
                    if (ce != cudaSuccess) {
                        fprintf(stderr,
                                "[thread_blender_apply] cudaEventRecord(copy_done) failed: %s\n",
                                cudaGetErrorString(ce));
                        infer_slot.busy.store(false, std::memory_order_release);
                    } else {
                        InferJob job;
                        job.slot_idx = infer_slot_idx;
                        job.frame_seq = cur[0]->seq;

                        if (!cec.infer_q->try_push(job)) {
							log_info_fmt("[infer_submit][DROP_Q_FULL] seq=%llu slot=%d",
                                         (unsigned long long)cur[0]->seq,
                                         infer_slot_idx);
                            infer_slot.busy.store(false, std::memory_order_release);
                        }
                    }
                }
            }
        }
		
        const bool want_s1 = mgr.want_push({ StreamGroup::S1, StreamView::V1234 });
        const bool want_s2 = mgr.want_push({ StreamGroup::S2, StreamView::V1234 });
        GstBuffer* out_s1 = want_s1 ? make_v1234_outbuf() : nullptr;
        GstBuffer* out_s2 = want_s2 ? make_v1234_outbuf() : nullptr;

        // Release source camera frames before entering H265 downstream push.
        // nvmm_copy_rgba_to_outbuf_cached() has already synchronized apply_stream
        // for any non-null outbuf above.
        if (out_s1 || out_s2) {
            for (auto& p : cur) p.reset();
            tmp.reset();
        }

        if (out_s1) {
            mgr.hubs[stream_index({ StreamGroup::S1, StreamView::V1234 })]
                .push_nvmm_rgba_buffer(out_s1);
        }
        if (out_s2) {
            mgr.hubs[stream_index({ StreamGroup::S2, StreamView::V1234 })]
                .push_nvmm_rgba_buffer(out_s2);
        }
#if 0 //測試 cur 釋放時機
		if (want_s1) push_one_v1234(StreamGroup::S1, cur[0]->seq);
		if (want_s2) push_one_v1234(StreamGroup::S2, cur[0]->seq);
#endif

        {
            std::lock_guard<std::mutex> lock_(bd_prep_mtx);
            blender_prepare_queue.push(ptr);
        }
        blend_prep.notify_one();
// static thread_local k180::dbgtime::FpsMon s_fps("thread_blender_apply_loop");
// s_fps.tick();
    }
	log_info_fmt("thread_blender_apply exit");
}

static void thread_blender_prepare(
	int id,
    std::atomic<bool>& running)
{
	
	auto& v = k180::runtime::rt().sdv;
    while (running.load(std::memory_order_relaxed)) {
        std::unique_lock<std::mutex> lock(bd_prep_mtx);
        blend_prep.wait(lock, [] {
            return !blender_prepare_queue.empty() || !keep_running.load(std::memory_order_relaxed) ;
        });

		if (!keep_running.load(std::memory_order_relaxed)) {
            break;
        }
		
        BlenderPtr ptr = blender_prepare_queue.front();
        blender_prepare_queue.pop();
        lock.unlock();

        ptr->prepare(v.corners_sd, v.sizes_sd);
        {			
            std::lock_guard<std::mutex> pool_lock(bd_pool_mtx);
            blender_ptr_pool.push(ptr);
        }
	}
	log_info_fmt("thread_blender_prepare exit");
}



static void capture_thread(int id,
						   StageQueue<FramePtr>& cc2bright,
                           StageQueue<FramePtr>& q_blend,
                           StageQueue<FramePtr>& q_seam,
                           std::atomic<bool>& running,
                           std::atomic<uint64_t>& seq_counter,
                           k180::HubManager& mgr,
						   CamCtx& cam,
						   CC_entry_Ctx& cec)
{
    std::uint32_t sync_count = 0;
	k180::runtime::cuda_set_current_for_thread("capture_thread");
	// cam.warp_pool.init(cam.warp_pool_slots, cam.in_h, cam.in_w, CV_8UC4);
	cam.warp_pool.init(cam.warp_pool_slots, cam.warp_h, cam.warp_w, CV_8UC4);
    StreamView sv = static_cast<StreamView>(static_cast<int>(StreamView::V1) + id);


static thread_local char s_pull_tag[64];
static thread_local char s_wrap_tag[64];
static thread_local char s_pool_tag[64];
static thread_local char s_remap_tag[64];
static thread_local bool s_timing_init = false;

static thread_local k180::dbgtime::StageTimingAcc s_pull_t;
static thread_local k180::dbgtime::StageTimingAcc s_wrap_t;
static thread_local k180::dbgtime::StageTimingAcc s_pool_t;
static thread_local k180::dbgtime::StageTimingAcc s_remap_t;

if (!s_timing_init) {
	std::snprintf(s_pull_tag,  sizeof(s_pull_tag),  "capture_pull_%d", id);
	std::snprintf(s_wrap_tag,  sizeof(s_wrap_tag),  "capture_wrap_rgba_%d", id);
	std::snprintf(s_pool_tag,  sizeof(s_pool_tag),  "capture_warp_pool_%d", id);
	std::snprintf(s_remap_tag, sizeof(s_remap_tag), "capture_remap_submit_%d", id);

	s_pull_t  = k180::dbgtime::StageTimingAcc(s_pull_tag);
	s_wrap_t  = k180::dbgtime::StageTimingAcc(s_wrap_tag);
	s_pool_t  = k180::dbgtime::StageTimingAcc(s_pool_tag);
	s_remap_t = k180::dbgtime::StageTimingAcc(s_remap_tag);

	s_timing_init = true;
}
	
// char na_1[200], na_2[200], na_3[200], na_4[200];
// uint64_t iii = 0;
// cv::Mat cam_read_cMat;
// cuda::GpuMat cam_read_gMat;
    while (running.load(std::memory_order_relaxed)) {

        if (!camera_trig.wait(sync_count, pipeline_sync_stop)) break;
// if( id==0 ){
// tt.ts_fps_beg3 = std::chrono::steady_clock::now();
// }	
uint64_t t_pull_0 = k180::dbgtime::now_ns_raw();
GstSample* sample = gst_app_sink_try_pull_sample(cam.sink, 200 * GST_MSECOND);
uint64_t t_pull_1 = k180::dbgtime::now_ns_raw();
s_pull_t.add_ns(t_pull_1 - t_pull_0);
        // GstSample* sample = gst_app_sink_try_pull_sample(cam.sink, 200 * GST_MSECOND);
        if (!sample) continue;

static thread_local char s_cap_tag[64];
static thread_local bool s_cap_tag_init = false;
static thread_local k180::dbgtime::FpsMon s_cap_fps;
if (!s_cap_tag_init) {
    std::snprintf(s_cap_tag, sizeof(s_cap_tag), "capture_thread_%d_loop", id);
    s_cap_fps = k180::dbgtime::FpsMon(s_cap_tag);
    s_cap_tag_init = true;
}
s_cap_fps.tick();


// if( id==0 ){
// tt.ts_fps_end3 = std::chrono::steady_clock::now();
// tt.fps_duration3 = (std::chrono::duration_cast<std::chrono::microseconds>(tt.ts_fps_end3 - tt.ts_fps_beg3).count()/1000000.0);
// log_info_fmt("capture_thread = %f", tt.fps_duration3);
// tt.ts_fps_beg3 = tt.ts_fps_end3;
// }
        auto f = std::make_shared<FrameItem>();
        f->sample = sample;            // owning ref
        f->seq    = ++seq_counter;

        GstBuffer* buf = gst_sample_get_buffer(sample);
        if (!buf) {
            f.reset(); // unref sample
            continue;
        }


		// wrap NVMM->CUDA 到 s.fr.rgba, 12us
        // if (!gstbuffer_to_gpu_rgba(buf, f->fr, id)) {
            // f.reset(); // unref sample + release_frame()
            // continue;
        // }

uint64_t t_wrap_0 = k180::dbgtime::now_ns_raw();
bool wrap_ok = gstbuffer_to_gpu_rgba(buf, f->fr, id);
uint64_t t_wrap_1 = k180::dbgtime::now_ns_raw();
s_wrap_t.add_ns(t_wrap_1 - t_wrap_0);

if (!wrap_ok) {
	f.reset(); // unref sample + release_frame()
	continue;
}

        // ---- acquire warp slot from pool ----
        // int slot = cam.warp_pool.try_acquire();
uint64_t t_pool_0 = k180::dbgtime::now_ns_raw();
int slot = cam.warp_pool.try_acquire();
uint64_t t_pool_1 = k180::dbgtime::now_ns_raw();
s_pool_t.add_ns(t_pool_1 - t_pool_0);
        if (slot < 0) {
            f.reset();
            continue;
        }

        f->warp_pool = &cam.warp_pool;
        f->warp_pool_idx = slot;
        f->warp_rgba = cam.warp_pool.mat(slot); // shallow handle (no alloc)
#if 0
cudaStream_t s = cv::cuda::StreamAccessor::getStream(cam.stream);

if (!launchRemapRGBA_Bilinear_ConstBorder(
        f->fr.rgba,
        f->warp_rgba,
        cam.g_map1,
        cam.g_map2,
        s)) {
    f.reset();
    continue;
}
#endif
uint64_t t_remap_0 = k180::dbgtime::now_ns_raw();
		cv::cuda::remap(
			f->fr.rgba, f->warp_rgba, cam.g_map1, cam.g_map2,
			cv::INTER_LINEAR, cv::BORDER_REFLECT, cv::Scalar(), cam.stream
		);// cv::BORDER_CONSTANT,BORDER_REFLECT

		// 下面這四步需要 20us
		f->warp_done.record(cam.stream);
uint64_t t_remap_1 = k180::dbgtime::now_ns_raw();
s_remap_t.add_ns(t_remap_1 - t_remap_0);
		f->warp_done_valid = true;
		q_blend.try_push(f);
		
static thread_local char s_blend_tag[64];
static thread_local bool s_blend_tag_init = false;
static thread_local k180::dbgtime::FpsMon s_to_blend_fps;
if (!s_blend_tag_init) {
    std::snprintf(s_blend_tag, sizeof(s_blend_tag), "capture_to_blend_%d", id);
    s_to_blend_fps = k180::dbgtime::FpsMon(s_blend_tag);
    s_blend_tag_init = true;
}
s_to_blend_fps.tick();

		blender_apply_sync.signal_id(id);
		q_seam.try_push(f);
		if (id == 1) { cc2bright.try_push(f); }
// #endif

		// 下面這四步需要 30us
        if (mgr.want_push({StreamGroup::S1, sv})) {
            GstBuffer* pushbuf = gst_buffer_ref(buf);
            mgr.hubs[ stream_index({StreamGroup::S1, sv}) ].push_nvmm_rgba_buffer(pushbuf);
        }
        if (mgr.want_push({StreamGroup::S2, sv})) {
            GstBuffer* pushbuf = gst_buffer_ref(buf);
            mgr.hubs[ stream_index({StreamGroup::S2, sv}) ].push_nvmm_rgba_buffer(pushbuf);
        }
        if (mgr.want_push_h264({StreamGroup::S1, sv})) {
            GstBuffer* pushbuf = gst_buffer_ref(buf);
            mgr.hubs_h264[ h264_index(StreamGroup::S1, sv) ].push_nvmm_rgba_buffer(pushbuf);
        }
        if (mgr.want_push_h264({StreamGroup::S2, sv})) {
            GstBuffer* pushbuf = gst_buffer_ref(buf);
            mgr.hubs_h264[ h264_index(StreamGroup::S2, sv) ].push_nvmm_rgba_buffer(pushbuf);
        }

    }
	log_info_fmt("capture_thread exit");
}

static inline bool set_control_nolock(int fd, uint32_t id, int value, const char* tag)
{
    struct v4l2_control ctrl = {};
    ctrl.id = id;
    ctrl.value = value;
    if (ioctl(fd, VIDIOC_S_CTRL, &ctrl) == -1) {
        log_error_errno_fmt(
                "[set_control_nolock][%s] ioctl failed (id=%u, value=%d):",
                tag, id, value);
        return false;
    }
    return true;
}

bool single_mipi_camera_init(int cam_id, int fd_l)
{
    if (fd_l < 0) {
        log_error_errno_fmt("[CAM%d] single_mipi_camera_init: fd < 0", cam_id);
        return false;
    }

#ifdef HW_SHORT_VER
    if (cfggg.rotate180)        // short
#else
    if (!cfggg.rotate180)       // long
#endif
    {
        set_control_nolock(fd_l, 0x00980914, 0, "rotate"); // horizontal_flip = 0
        usleep(100000);
        set_control_nolock(fd_l, 0x00980914, 1, "rotate"); // horizontal_flip = 1
        usleep(100000);
        set_control_nolock(fd_l, 0x00980915, 0, "rotate"); // vertical_flip = 0
        usleep(100000);
        set_control_nolock(fd_l, 0x00980915, 1, "rotate"); // vertical_flip = 1
    }
    usleep(100000);

    set_control_nolock(fd_l, CTRL_TRIGGER_MODE, 0, "camera_init CTRL_TRIGGER_MODE1");
    sleep(2);
    set_control_nolock(fd_l, CTRL_TRIGGER_MODE, 2, "camera_init CTRL_TRIGGER_MODE2");
    sleep(1);

    set_control_nolock(fd_l, CTRL_TRIGGER_WB_MODE, 1, "camera_init CTRL_TRIGGER_WB_MODE1");
    usleep(1000);
    set_control_nolock(fd_l, CTRL_TRIGGER_WB_MODE, 2, "camera_init CTRL_TRIGGER_WB_MODE2");
    usleep(1000);

// 這邊有一個很詭異的issue，這邊的200原本是寫5000，但剛好現在設定的exposure_tun_val=5000，導致開機時，這個值不會被設定，然後FPS就一直15，顯然就是 shutter 怪怪的。所以我故意設一個很低的 200,
    set_control_nolock(fd_l, CTRL_TRIGGER_SHUTTER, 200, "camera_init CTRL_TRIGGER_SHUTTER1");
    usleep(1000);
    set_control_nolock(fd_l, CTRL_TRIGGER_SHUTTER, k180::brighttuner::exposure_tun_val, "camera_init CTRL_TRIGGER_SHUTTER2");
    usleep(1000);

    set_control_nolock(fd_l, CTRL_TRIGGER_GAIN, 10000, "camera_init CTRL_TRIGGER_GAIN1");
    usleep(1000);
    set_control_nolock(fd_l, CTRL_TRIGGER_GAIN, k180::brighttuner::gain_tun_val, "camera_init CTRL_TRIGGER_GAIN2");
    usleep(1000);

    set_control_nolock(fd_l, CTRL_WDR_MODE, 1, "camera_init CTRL_WDR_MODE1");
    usleep(1000);
    set_control_nolock(fd_l, CTRL_WDR_MODE, 0, "camera_init CTRL_WDR_MODE2");

    log_info_fmt("[CAM%d] single_mipi_camera_init done (fd=%d)", cam_id, fd_l);
    return true;
}

void init_blender(){
	auto& v = k180::runtime::rt().sdv;
	
	{
		std::lock_guard<std::mutex> lock(bd_pool_mtx);

		for (int i = 0; i < BLENDER_POOL_SIZE; ++i) {
			BlenderPtr blender = makePtr<NoBlenderGPU>();
#if 0
			auto& s = k180::runtime::rt().sdp;
			Size dst_sz = resultRoi(v.corners_sd, v.sizes_sd).size();
			float blend_width = sqrt(static_cast<float>(dst_sz.area())) * cfggg.blend_strength / 100.f;
		
			BlenderPtr blender = Blender::createDefault(blend_type, s.try_cuda);

			if (blend_width < 1.f) {
				blender = Blender::createDefault(Blender::NO, s.try_cuda);
				std::cout << "[init #" << i << "] No blending, blend_width=" << blend_width << "\n";
			} else if (blend_type == Blender::MULTI_BAND) {
				blender = makePtr<MultiBandBlender>(false, static_cast<int>(ceil(log(blend_width) / log(2.)) - 1.));
				std::cout << "[init #" << i << "] Multi-band blender, bands=" 
						  << dynamic_cast<MultiBandBlender *>(blender.get())->numBands()
						  << ", blend_width=" << blend_width << "\n";
			} else if (blend_type == Blender::FEATHER) {
				blender = makePtr<FeatherBlender>();
				dynamic_cast<FeatherBlender *>(blender.get())->setSharpness(1.f / blend_width);
				std::cout << "[init #" << i << "] Feather blender, sharpness=" 
						  << dynamic_cast<FeatherBlender *>(blender.get())->sharpness()
						  << ", blend_width=" << blend_width << "\n";
			} else {
				blender = Blender::createDefault(blend_type, s.try_cuda);
				std::cout << "[init #" << i << "] Default blender\n";
			}
#endif
			blender->prepare(v.corners_sd, v.sizes_sd);
			blender_ptr_pool.push(blender);
		}
	}
}

static inline void apply_pending(int cam_idx, int fd)
{
    int v = 0;

    if (k180::brighttuner::consume_exposure(cam_idx, v)) {
        set_control_nolock(fd, CTRL_TRIGGER_SHUTTER, v, "expo");
    }
    if (k180::brighttuner::consume_gain(cam_idx, v)) {
        set_control_nolock(fd, CTRL_TRIGGER_GAIN, v, "gain");
    }
}

bool cam_do_trigger(int cam_idx, CamCtx& cam)
{
    int fd  = cam.fd;
    if (fd < 0) return false;

    bool ok = set_control_nolock(fd, CTRL_SW_TRIGGER, 1, "trigger");

    // trigger 後再 apply：降低 trigger 當下延遲
    apply_pending(cam_idx, fd);
    return ok;
}

static bool build_cam_pipeline(const std::string& dev,
                               int in_w, int in_h,
                               int cam_id,
                               CamCtx& cam)
{
    cam.dev = dev;
    cam.in_w = in_w;
    cam.in_h = in_h;

    // 0) open fd (CamCtx owns it). 但 v4l2 init 會在 PLAYING 後做。
    cam.fd = ::open(dev.c_str(), O_RDWR);
    if (cam.fd < 0) {
        std::string errmsg = "[CAM" + std::to_string(cam_id) + "] open failed: " + dev;
        log_error_errno_fmt(errmsg.c_str());
        return false;
    }

    // 1) build gstreamer pipeline
    char pipe[1024];
    std::snprintf(pipe, sizeof(pipe),
        "nvv4l2camerasrc device=%s cap-buffers=16 ! "	// 12 -> 16, fps會降速 懷疑是 gst_app_sink_try_pull_sample
        "video/x-raw(memory:NVMM),width=%d,height=%d,format=UYVY ! "
        "nvvidconv output-buffers=32 ! video/x-raw(memory:NVMM),format=RGBA ! "	// 8 -> 32
        "appsink name=sink_%d sync=false max-buffers=2 drop=true",
        dev.c_str(), in_w, in_h, cam_id);

    GSTD("[CAM%d] launch: %s\n", cam_id, pipe);

    GError* err = nullptr;
    cam.pipeline = gst_parse_launch(pipe, &err);
    if (!cam.pipeline) {
        GSTD("[CAM%d] gst_parse_launch failed: %s\n", cam_id, err ? err->message : "(null)");
        if (err) g_error_free(err);
        ::close(cam.fd); cam.fd = -1;
        return false;
    }
    if (err) {
        GSTD("[CAM%d] gst_parse_launch warning: %s\n", cam_id, err->message);
        g_error_free(err);
        err = nullptr;
    }

    char sink_name[32];
    std::snprintf(sink_name, sizeof(sink_name), "sink_%d", cam_id);
    cam.sink_elem = gst_bin_get_by_name(GST_BIN(cam.pipeline), sink_name);
    if (!cam.sink_elem) {
        GSTD("[CAM%d] cannot find appsink: %s\n", cam_id, sink_name);
        gst_object_unref(cam.pipeline); cam.pipeline = nullptr;
        ::close(cam.fd); cam.fd = -1;
        return false;
    }

    cam.sink = GST_APP_SINK(cam.sink_elem);
    gst_app_sink_set_emit_signals(cam.sink, FALSE);
    gst_app_sink_set_drop(cam.sink, TRUE);
    gst_app_sink_set_max_buffers(cam.sink, 2);	// 不能設 4，會慢 2張,  如果 exposure_tun_val(trigger_shutter) 是 16666, 這邊就必須設為 2，不然 gst_app_sink_try_pull_sample 會卡。如果 exposure_tun_val(trigger_shutter) 是 10000, 則這邊可以設為 1, 上面 pipe 裡也要一起改
	
    // 2) set PLAYING first (your camera requires v4l2 init AFTER this)
    GstStateChangeReturn r = gst_element_set_state(cam.pipeline, GST_STATE_PLAYING);
    GSTD("[CAM%d] set_state(PLAYING) ret=%d\n", cam_id, (int)r);
	(void)r;
    // 3) wait a bit for pipeline to reach PLAYING or at least PAUSED
    GstState cur = GST_STATE_NULL;
    GstState pending = GST_STATE_NULL;
    gst_element_get_state(cam.pipeline, &cur, &pending, 2 * GST_SECOND);
    GSTD("[CAM%d] get_state cur=%s pending=%s\n",
         cam_id,
         gst_element_state_get_name(cur),
         gst_element_state_get_name(pending));
	
    // 4) queues
	// Jetson 的 nvv4l2camerasrc / nvvidconv / appsink 這條，底下通常有固定數量的 NVMM buffers, f 如果被持有太久，gst_app_sink_try_pull_sample 會卡住 
    cam.cc2bright = std::make_unique<StageQueue<FramePtr>>(1);	// 只能設 1，如果Q太大，f 會被保留太久，
    cam.q_blend   = std::make_unique<StageQueue<FramePtr>>(2);
    cam.q_seam    = std::make_unique<StageQueue<FramePtr>>(1);

    // cam.v4l2_inited = false;
    return true;
}

int cc_entry_point_all(CC_entry_Ctx& cec,
                       int in_w, int in_h,
                       const std::vector<std::string>& devs,
                       k180::HubManager& mgr)
{
    constexpr int CAM_N = 4;
    if ((int)devs.size() < CAM_N) return 10;

    // auto& s = k180::runtime::rt().sdp;
    // auto& v = k180::runtime::rt().sdv;

cec.compensator = ExposureCompensator::createDefault(ExposureCompensator::GAIN_BLOCKS);
if (auto* bg = dynamic_cast<cv::detail::BlocksGainCompensator*>(cec.compensator.get())) {
    bg->setBlockSize(cec.compensator_block, cec.compensator_block);
}
// cec.gain_shared.block_size = cec.compensator_block;
    // cec.compensator = ExposureCompensator::createDefault(expos_comp_type);
    if (dynamic_cast<GainCompensator *>(cec.compensator.get()))
    {
        GainCompensator *gcompensator = dynamic_cast<GainCompensator *>(cec.compensator.get());
        gcompensator->setNrFeeds(1);
    }
    if (dynamic_cast<ChannelsCompensator *>(cec.compensator.get()))
    {
        ChannelsCompensator *ccompensator = dynamic_cast<ChannelsCompensator *>(cec.compensator.get());
        ccompensator->setNrFeeds(1);
    }
    if (dynamic_cast<BlocksCompensator *>(cec.compensator.get()))
    {
        BlocksCompensator *bcompensator = dynamic_cast<BlocksCompensator *>(cec.compensator.get());
        bcompensator->setNrFeeds(1);
        bcompensator->setNrGainsFilteringIterations(2);
        bcompensator->setBlockSize(cec.compensator_block, cec.compensator_block);
    }

cec.seam_finder = create_seam_finder( k180::runtime::rt().sdp );


    cec.running.store(true, std::memory_order_relaxed);

    // 1) build camera pipelines (PLAYING first)
    for (int i = 0; i < CAM_N; ++i) {
        if (!build_cam_pipeline(devs[i], in_w, in_h, i, cec.cams[i])) {
            cec.running.store(false, std::memory_order_relaxed);
            return 1;
        }
    }

    // 2) NOW do v4l2 init (your requirement: must be after PLAYING)
    for (int i = 0; i < CAM_N; ++i) {
        auto& cam = cec.cams[i];
        if (!single_mipi_camera_init(i, cam.fd)) {
            log_error_errno_fmt("[CAM%d] v4l2 init after PLAYING failed", i);
            cec.running.store(false, std::memory_order_relaxed);
            return 11;
        }
        // cam.v4l2_inited = true;
    }
    log_info_fmt("camera v4l2 init (post-PLAYING) done");

    // 3) stitch assets
    if (!init_stitch_assets_all(cec)) {
        cec.running.store(false, std::memory_order_relaxed);
        return 2;
    }

    // 4) blender pool (CC-owned; needs init_sd_para done already)
    init_blender();

    // 5) capture threads (start after v4l2 init is done)
    for (int i = 0; i < CAM_N; ++i) {
        auto& cam = cec.cams[i];
        cam.t_cap = std::thread(
            capture_thread,
            i,
            std::ref(*cam.cc2bright),
            std::ref(*cam.q_blend),
            std::ref(*cam.q_seam),
            std::ref(cec.running),
            std::ref(cam.seq),
            std::ref(mgr),
            std::ref(cam),
			std::ref(cec)
        );
    }

    // 6) seam-prep threads
    for (int i = 0; i < CAM_N; ++i) {
        cec.t_seam_prep[i] = std::thread(
            thread_cc_prepare_seam_input,
            i,
			cec.cams[i].q_seam.get(),
            std::ref(cec.seam_shared),
            std::ref(cec.running)
        );
    }

    // 7) seam-find thread
	cec.t_seam = std::thread(
		thread_seam_find,
		std::ref(cec.seam_shared),
		std::ref(cec.seam_finder),
		std::ref(cec.compensator),
		std::ref(cec.gain_shared),
		std::ref(cec.gain_front),
		std::ref(cec.gain_epoch),
		cec.compensator_block,
		std::ref(cec.running),
		CAM_N
	);

	int tracker = 2	;	//-1;
	string iface_in = "eth0", iface_out = "eth1";
	string engine = "/home/fourd/projects/rtsp_server/yolov8n_fp16.engine";	// ok
	// string engine = "/home/fourd/projects/rtsp_server/yolov8s_fp16.engine";	// ok
	// string engine = "/home/fourd/projects/rtsp_server/yolov8s_fp32.engine";	// 怪怪
	// string engine = "/home/fourd/projects/rtsp_server/yolov8.engine";	// ok ctrl+c 時，停不掉
	// string engine = "/home/fourd/projects/rtsp_server/yolov8s.engine";	// 怪怪
	Mode mode = NO_SAVE;


	// cec.srv = new ImgUDPGateway(iface_in, 5555, mode, iface_out, engine, tracker);
	cec.srv = std::make_unique<ImgUDPGateway>( iface_in, 5555, mode, iface_out, engine, tracker);
    if (!init_infer_pool(cec)) {	// also create cec.infer_q, size = 2
        cec.running.store(false, std::memory_order_relaxed);
        return 12;
    }

    cec._t_inf = std::thread(
        thread_infer_worker,
        std::ref(cec),
        std::ref(cec.running)
    );
	
    // 8) blender threads
    for (int i = 0; i < THREAD_PREP_COUNT; ++i) {
        cec.prepare_threads.emplace_back(
            thread_blender_prepare,
            i,
            std::ref(cec.running)
        );
    }

	for (int i = 0; i < THREAD_APPLY_COUNT; ++i) {
		cec.apply_threads.emplace_back(
			thread_blender_apply,
			i,
			cec.cams[0].q_blend.get(),
			cec.cams[1].q_blend.get(),
			cec.cams[2].q_blend.get(),
			cec.cams[3].q_blend.get(),
			std::ref(cec.gain_shared),
			std::ref(cec.gain_front),
			std::ref(cec.gain_epoch),
			std::ref(mgr),
			std::ref(cec),
			std::ref(cec.running)
		);
	}

	cec._t_bri_adj = std::thread(
								k180::brighttuner::bright_adjust,
								1,
								cec.cams[1].cc2bright.get(),
								std::ref(cec.running));
// #endif
    return 0;
}

struct SignalCtx {
    GMainLoop* loop = nullptr;
    CC_entry_Ctx* cec = nullptr;
};

static std::atomic<bool> g_shutdown_requested{false};

static void request_stop_and_wakeup_all(CC_entry_Ctx& cec)
{
    bool expected = false;
    if (!g_shutdown_requested.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel)) {
        // already requested
    }

    pipeline_sync_stop.store(true, std::memory_order_relaxed);
    keep_running.store(false, std::memory_order_relaxed);
    cec.running.store(false, std::memory_order_relaxed);

    camera_trig.notify_all();
    blender_apply_sync.notify_all();
    seam_find_sync.notify_all();
    blend_prep.notify_all();

    auto& v = k180::runtime::rt().sdv;
    v.find_seam_ready = true;
}

static void stop_pipeline_safely(GstElement* pipeline, const char* tag)
{
    if (!pipeline) return;

    auto logp = [&](const char* s){
        fprintf(stderr, "[STOP][%s] %s\n", tag ? tag : "pipe", s);
    };

    logp("set_state(PAUSED)");
    gst_element_set_state(pipeline, GST_STATE_PAUSED);
    gst_element_get_state(pipeline, nullptr, nullptr, 500 * GST_MSECOND);

    logp("send flush-start/flush-stop");
    gst_element_send_event(pipeline, gst_event_new_flush_start());
    gst_element_send_event(pipeline, gst_event_new_flush_stop(FALSE));

    logp("set_state(NULL)");
    gst_element_set_state(pipeline, GST_STATE_NULL);

    GstState cur = GST_STATE_VOID_PENDING, pending = GST_STATE_VOID_PENDING;
    GstStateChangeReturn r =
        gst_element_get_state(pipeline, &cur, &pending, 2 * GST_SECOND);

    if (r == GST_STATE_CHANGE_FAILURE || r == GST_STATE_CHANGE_ASYNC) {
        logp("set_state(NULL) timeout/fail");
    } else {
        logp("set_state(NULL) done");
    }
}

static void shutdown_all(CC_entry_Ctx& cec,
                         GstRTSPServer* server,
                         GstRTSPSessionPool* session,
                         GMainLoop* loop, 
						 guint rtsp_attach_id)
{
    request_stop_and_wakeup_all(cec);
if (rtsp_attach_id != 0) {
    g_source_remove(rtsp_attach_id);
}
    // 1) 先停 queue，喚醒 consumer
    for (auto& cam : cec.cams) {
        if (cam.cc2bright) cam.cc2bright->close();
        if (cam.q_blend)   cam.q_blend->close();
        if (cam.q_seam)    cam.q_seam->close();
    }

    // 2) 先停 hub / recorder，避免 worker 還在 push
    mgr.stop_all_rec(1500);
    mgr.stop_all_h264();

    if (cec.infer_q) {
        cec.infer_q->close();
    }
	
    // 3) trigger thread 收掉
    if (trig_thread.joinable()) trig_thread.join();

    // 4) inference / bright / seam / blender worker join
    if (cec._t_bri_adj.joinable()) cec._t_bri_adj.join();

    if (cec._t_inf.joinable()) cec._t_inf.join();
    cec.srv.reset();

    for (auto& s : cec.infer_pool) {
        if (s.copy_done) {
            cudaEventDestroy(s.copy_done);
            s.copy_done = nullptr;
        }
        s.busy.store(false, std::memory_order_relaxed);
        s.pano_rgba.release();
    }
	
    if (cec.t_seam.joinable()) cec.t_seam.join();

    for (int i = 0; i < 4; ++i) {
        if (cec.t_seam_prep[i].joinable()) cec.t_seam_prep[i].join();
    }

    for (auto& t : cec.apply_threads) {
        if (t.joinable()) t.join();
    }

    blend_prep.notify_all();
    for (auto& t : cec.prepare_threads) {
        if (t.joinable()) t.join();
    }

    // 5) capture threads 最後 join
    //    appsink try_pull_sample 有 timeout，running=false 後應可自然退出
    for (auto& cam : cec.cams) {
        if (cam.t_cap.joinable()) cam.t_cap.join();
    }


    // 7) 先把各 camera pipeline 切到 NULL，再 unref
    // for (auto& cam : cec.cams) {
        // if (cam.pipeline) {
            // stop_pipeline_safely(cam.pipeline, cam.dev.c_str());
        // }
    // }

    for (auto& cam : cec.cams) {
        if (cam.sink_elem) {
            gst_object_unref(cam.sink_elem);
            cam.sink_elem = nullptr;
            cam.sink = nullptr;
        }
        if (cam.pipeline) {
            gst_object_unref(cam.pipeline);
            cam.pipeline = nullptr;
        }
    }

    // 8) close camera fd last
    for (int i = 0; i < (int)cec.cams.size(); ++i) {
        int fd = cec.cams[i].fd;
        if (fd >= 0) {
            set_control_nolock(fd, CTRL_TRIGGER_MODE, 0, "TRIGGER_MODE clean");
            ::close(fd);
            cec.cams[i].fd = -1;
            log_info_fmt("[CC_STOP] close cam%d fd=%d OK", i, fd);
        }
    }

    // 9) 清 input cache
    clear_input_cures_cache();

    // 10) RTSP server / session cleanup
    if (server) {
        g_object_unref(server);
    }
    // if (session) {
        // g_object_unref(session);
    // }

    if (loop) {
        g_main_loop_unref(loop);
    }

    fflush(stderr);
    std::cerr << "\n[INFO] shutdown_all done\n";
}

static int renew_fw_info( ){
    // 讀取 JSON 檔案
	int fd_l = open(FW_INFO_FILE, O_RDWR);
	if (fd_l < 0) {
		return 1;
	}
	
	if (flock(fd_l, LOCK_EX) < 0) {
		close(fd_l);
		return 1;
	}
	FILE *fp = fdopen(fd_l, "r+");
    if (!fp) {
		log_error_errno_fmt("fdopen"); 
		flock(fd_l, LOCK_UN);
        close(fd_l);
        return 1;
    }
	fseek(fp, 0, SEEK_END);
	size_t size = ftell(fp);
	rewind(fp);
	char *buffer = (char*)malloc(size + 1);
	size_t read_bytes = fread(buffer, 1, size, fp);
	if (read_bytes != size) {
		log_error_errno_fmt("Warning: only read %zu of %zu bytes from file\n", read_bytes, size); 
		flock(fd_l, LOCK_UN);
		fclose(fp);
		free(buffer);
		return 1;
	}
	buffer[size] = '\0';
	
	struct json_object *root = json_tokener_parse(buffer);
	free(buffer);
    if (!root) {
		log_error_errno_fmt("Failed to open or parse JSON file: %s\n", FW_INFO_FILE);
		flock(fd_l, LOCK_UN);
		fclose(fp); 
        return 1;
    }

    // 取得 sysinfo 物件
    struct json_object *sysinfo = NULL;
    if (!json_object_object_get_ex(root, "sysinfo", &sysinfo)) {
		log_error_errno_fmt("Missing 'sysinfo' object in JSON\n");
        json_object_put(root);
		flock(fd_l, LOCK_UN);
		fclose(fp);
        return 1;
    }

    // 建立新的 fwver 值（使用字串）
    struct json_object *new_ver = json_object_new_string(FW_VER);
    json_object_object_add(sysinfo, "fwver", new_ver);
    const char *new_json = json_object_to_json_string_ext(root, JSON_C_TO_STRING_PRETTY);
    rewind(fp);
	if (ftruncate(fd_l, 0) < 0) {
		log_error_errno_fmt("ftruncate failed"); 
		json_object_put(root);
		flock(fd_l, LOCK_UN);
		close(fd_l);
		return 1;
	}
    fwrite(new_json, 1, strlen(new_json), fp);
    fflush(fp);

	json_object_put(root);  // 釋放記憶體
	flock(fd_l, LOCK_UN);
	fclose(fp); // 自動 unlock + close
    return 0;
}

void init_sd_para(){
	auto& s = k180::runtime::rt().sdp;
	auto& v = k180::runtime::rt().sdv;
	int num_images = 4;
	char path[80];
	const char *meta_path;
	vector<CameraParams> cameras;
    vector<Point> corners_load(num_images);;
    vector<Size> sizes_load(num_images);
	
    Ptr<WarperCreator> warper_creator100 = makePtr<cv::SphericalWarperGpu>();
	
	// if( !ROTATE180 )
	if( !cfggg.rotate180 )
		meta_path = META_PATH_0;
	else
		meta_path = META_PATH_180;

	loadCameraParams(meta_path, cameras, num_images);
	
	snprintf(path, sizeof(path), "%s/scale.yml", meta_path);
	loadWarpedImageScale(path, v.warped_image_scale);

	snprintf(path, sizeof(path), "%s/corners.yml", meta_path);
	loadCorners(path, corners_load);

	snprintf(path, sizeof(path), "%s/sizes.yml", meta_path);
	loadSizes(path, sizes_load);

    for (int i = 0; i < num_images; ++i) {
        v.corners_resize[i].x = corners_load[i].x * s.seam_scale;
        v.corners_resize[i].y = corners_load[i].y * s.seam_scale;
    }

	v.corners_sd = corners_load;
	v.sizes_sd = sizes_load;
	v.dst_roi = cv::detail::resultRoi(v.corners_sd, v.sizes_sd);
	
	v.cameras_sd = cameras;
	v.warped_image_scale_sd = v.warped_image_scale;
// printf("===================  %f ======= %f\n", v.warped_image_scale_sd , v.warped_image_scale);
	v.warper_init_cam = std::shared_ptr<RotationWarper>(warper_creator100->create(v.warped_image_scale_sd));

}


void trigger_thread(CC_entry_Ctx& cec)
{
    const auto interval = std::chrono::milliseconds(shutter_interval);
    using clock = std::chrono::steady_clock;
    auto next = clock::now() + interval;

    while (keep_running.load(std::memory_order_relaxed)) {

        std::this_thread::sleep_until(next);

        // ioctl trigger	// 下面兩個工作需要 2ms
        for (int i = 0; i < tmmm_nu; ++i) {
            cam_do_trigger(i, cec.cams[i]);
        }

        camera_trig.signal_all();

static thread_local k180::dbgtime::FpsMon s_fps("trigger_thread");
s_fps.tick();

        next += interval;

        auto now = clock::now();
        if (now > next) {
            auto lag = now - next;
            auto skip = (lag / interval) + 1;
            next += skip * interval;
        }
    }

    log_info_fmt("trigger_thread exit");
}

#if 0
static gboolean on_unix_signal(gpointer user_data)
{
    try {
        auto* ctx = static_cast<SignalCtx*>(user_data);
        const char* m = "[SIG] on_unix_signal fired\n";
        ssize_t a_ = write(STDERR_FILENO, m, strlen(m));
        (void)a_;

        if (ctx && ctx->cec) {
            request_stop_and_wakeup_all(*ctx->cec);
        }

        if (ctx && ctx->loop) {
            g_main_loop_quit(ctx->loop);
        }

        return G_SOURCE_REMOVE;
    } catch (...) {
        const char* m = "[SIG] exception in on_unix_signal\n";
        ssize_t a_ = write(STDERR_FILENO, m, strlen(m));
        (void)a_;
        return G_SOURCE_REMOVE;
    }
}
#endif
static gboolean on_unix_signal(gpointer user_data)
{
    const char* m = "[SIG] on_unix_signal fired\n";
    ssize_t a_ = write(STDERR_FILENO, m, strlen(m));
    (void)a_;

    auto* ctx = static_cast<SignalCtx*>(user_data);
    if (!ctx) return G_SOURCE_REMOVE;

    if (ctx->cec) {
        request_stop_and_wakeup_all(*ctx->cec);
    }

    if (ctx->loop) {
        g_main_loop_quit(ctx->loop);
    }

    return G_SOURCE_REMOVE;
}
#if 0
static gboolean on_unix_signal(gpointer user_data) {
    try {
        auto* loop = static_cast<GMainLoop*>(user_data);
        const char* m = "[SIG] on_unix_signal fired\n";
        ssize_t a_ = write(STDERR_FILENO, m, strlen(m));
		(void)a_;
        keep_running.store(false, std::memory_order_relaxed);
request_stop_and_wakeup_all();
        if (loop) g_main_loop_quit(loop);
        return G_SOURCE_REMOVE;
    } catch (...) {
        const char* m = "[SIG] exception in on_unix_signal\n";
        ssize_t a_ = write(STDERR_FILENO, m, strlen(m));
		(void)a_;
        return G_SOURCE_REMOVE;
    }
}

static void do_graceful_shutdown()
{
    request_stop_and_wakeup_all();

    mgr.stop_all_rec(1500);
    mgr.stop_all_h264();

    if (trig_thread.joinable()) trig_thread.join();

    fflush(stderr);
    std::cerr << "\n[INFO] do_graceful_shutdown done\n";
    // close_logging();
}
#endif

int main(int argc, char *argv[])
{
    GMainLoop *loop;

    GstRTSPServer *server;
    GstRTSPMountPoints *mounts;
    GstRTSPSessionPool *session;

    init_logging("grand_yeah");
    log_info_fmt("Service started version %s", FW_VER);
    renew_fw_info();

    std::string err;
    auto st = user_cfg_load_from_file(cfggg, RF_REG_FILE, &err);
    if (st != CfgStatus::OK) {
        std::cerr << "config_load_from_file failed: " << cfg_status_str(st) << "\n";
        if (!err.empty()) std::cerr << "reason: " << err << "\n";
        return 1;
    }
    user_cfg_dump(cfggg, std::cout, true);
k180::ai::init_ai_runtime_from_cfg(cfggg.objectdet);
    // ✅ global one-time init
    gst_init(&argc, &argv);
// #if 0
	k180::runtime::init_cuda_primary_ctx_once();

    // ✅ load stitch params etc.
    init_sd_para();

	std::vector<std::string> devs(4);
	for (int i = 0; i < 4; ++i) {
		devs[i] = "/dev/video" + std::to_string(cfggg.cam_num[i]);
	}

    CC_entry_Ctx cec;

    int ret = cc_entry_point_all(cec, 1920, 1080, devs, mgr);
    if (ret != 0) {
        std::cerr << "cc_entry_point_all failed: " << ret << "\n";
        return ret;
    }
// #endif
    // ✅ trigger thread now needs cec
    trig_thread = std::thread(trigger_thread, std::ref(cec));



    loop = g_main_loop_new(NULL, FALSE);
	guint rtsp_attach_id = 0;

SignalCtx sigctx;
sigctx.loop = loop;
sigctx.cec  = &cec;
    // g_unix_signal_add(SIGINT,  on_unix_signal, loop);
    // g_unix_signal_add(SIGTERM, on_unix_signal, loop);
    g_unix_signal_add(SIGINT,  on_unix_signal, &sigctx);
    g_unix_signal_add(SIGTERM, on_unix_signal, &sigctx);
	// SIGHUP 需要就加，其他 SIGSEGV/SIGABRT 不要攔
    build_and_start_all_hubs_from_cfg(mgr, cfggg, &cec.osd_shared);

    session = gst_rtsp_session_pool_new();
    // gst_rtsp_session_pool_set_max_sessions(session, 10);

    server = gst_rtsp_server_new();
    g_object_set(server, "service", "8554", NULL);
    mounts = gst_rtsp_server_get_mount_points(server);
    attach_factories(mounts, mgr);
    g_object_unref(mounts);

    gst_rtsp_server_set_session_pool(server, session);
    g_object_unref(session);

	rtsp_attach_id = gst_rtsp_server_attach(server, NULL);

    log_info_fmt("stream ready");
    g_main_loop_run(loop);

    // do_graceful_shutdown();
    // cc_stop_all(cec);

    // g_main_loop_unref(loop);
	shutdown_all(cec, server, session, loop, rtsp_attach_id);
    return 0;
}
