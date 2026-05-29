#pragma once

#include <gst/gst.h>
#include <opencv2/core/cuda.hpp>
#include <cuda.h>

#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

// ============================================================
// GpuMatPool: fixed-size pool of preallocated cv::cuda::GpuMat
// ============================================================
struct GpuMatPool {
    std::mutex m;
    std::vector<cv::cuda::GpuMat> mats;
    std::vector<int> free_idx;

    void init(int n, int h, int w, int type) {
        std::lock_guard<std::mutex> lk(m);

        mats.resize(n);
        free_idx.clear();
        free_idx.reserve(n);

        for (int i = 0; i < n; ++i) {
            mats[i].create(h, w, type);   // allocate ONCE
            free_idx.push_back(i);
        }
    }

    int try_acquire() {
        std::lock_guard<std::mutex> lk(m);
        if (free_idx.empty()) return -1;
        int idx = free_idx.back();
        free_idx.pop_back();
        return idx;
    }

    void release(int idx) {
        std::lock_guard<std::mutex> lk(m);
        free_idx.push_back(idx);
    }

    cv::cuda::GpuMat& mat(int idx) { return mats[idx]; }
    const cv::cuda::GpuMat& mat(int idx) const { return mats[idx]; }
};

// ============================================================
// 修法B專用：FrameGpuRGBA / FrameItem
// - 不管理 EGL map/unmap
// - 不管理 cuGraphicsEGLRegisterImage/unregister
// - cache cleanup 由 thread_local cache 的 clear_input_cures_cache() 負責
// ============================================================
struct FrameGpuRGBA {
    cv::cuda::GpuMat rgba;          // shallow wrap to NVMM device pointer
    CUgraphicsResource cudaRes{};   // optional: owned by cache, here for debug/trace only
};

static inline void release_frame(FrameGpuRGBA& f) {
    // 修法B：不做任何 Unmap/Unregister（都由 cache 統一管理）
    f.rgba.release();
    f.cudaRes = nullptr;
}

struct FrameItem {
    GstSample* sample = nullptr;    // owns 1 ref
    FrameGpuRGBA fr;                // NVMM->CUDA wrap (修法B)
    std::uint64_t seq = 0;

    // ---- warped output (borrow from pool) ----
    cv::cuda::GpuMat warp_rgba;     // shallow handle to pool slot
    GpuMatPool* warp_pool = nullptr;
    int warp_pool_idx = -1;

    cv::cuda::Event warp_done;      // remap 完成事件
    bool warp_done_valid = false;

    ~FrameItem() {
        // 1) 釋放 input wrap（不碰 cache 內部資源）
        release_frame(fr);

        // 2) 歸還 warp slot（最後一個 shared_ptr 消失才會進來）
        if (warp_pool && warp_pool_idx >= 0) {
            warp_pool->release(warp_pool_idx);
        }
        warp_pool = nullptr;
        warp_pool_idx = -1;
        warp_rgba.release();

        // 3) unref GstSample
        if (sample) {
            gst_sample_unref(sample);
            sample = nullptr;
        }
    }

    FrameItem() = default;
    FrameItem(const FrameItem&) = delete;
    FrameItem& operator=(const FrameItem&) = delete;
};

using FramePtr = std::shared_ptr<FrameItem>;

#if 0
struct FrameGpuRGBA {
    cv::cuda::GpuMat rgba;
    NvBufSurface* surface = nullptr;
    int surf_idx = 0;

    CUgraphicsResource cudaRes = nullptr;
    bool cudaRes_from_cache = false;
    bool eglMapped = false;

    GstBuffer* mapped_buf = nullptr;
    GstMapInfo mapped_info = GST_MAP_INFO_INIT;
    bool gst_mapped = false;
};

static inline void release_frame(FrameGpuRGBA& f) {
    if (f.cudaRes && !f.cudaRes_from_cache) {
        cuGraphicsUnregisterResource(f.cudaRes);
    }
    f.cudaRes = nullptr;
    f.cudaRes_from_cache = false;

    if (f.eglMapped && f.surface) {
        NvBufSurfaceUnMapEglImage(f.surface, f.surf_idx);
    }
    f.eglMapped = false;

    if (f.gst_mapped && f.mapped_buf) {
        gst_buffer_unmap(f.mapped_buf, &f.mapped_info);
        gst_buffer_unref(f.mapped_buf);
    }
    f.mapped_buf = nullptr;
    f.mapped_info = GST_MAP_INFO_INIT;
    f.gst_mapped = false;

    f.rgba.release();
    f.surface = nullptr;
    f.surf_idx = 0;
}

struct FrameItem {
    GstSample* sample = nullptr;   // owns 1 ref
    FrameGpuRGBA fr;               // NVMM->EGL->CUDA wrap
    uint64_t seq = 0;

    // ---- warped output (borrow from pool) ----
    cv::cuda::GpuMat warp_rgba;    // shallow handle to pool slot
    GpuMatPool* warp_pool = nullptr;
    int warp_pool_idx = -1;
	cv::cuda::Event warp_done;   // NEW: remap 完成事件
	bool warp_done_valid = false;

    ~FrameItem() {
        // 先釋放 NVMM mapping
        release_frame(fr);

        // 再把 warp slot 還回 pool（確保最後一個 shared_ptr 消失才做）
        if (warp_pool && warp_pool_idx >= 0) {
            warp_pool->release(warp_pool_idx);
        }
        warp_pool = nullptr;
        warp_pool_idx = -1;
        warp_rgba.release();

        if (sample) {
            gst_sample_unref(sample);
            sample = nullptr;
        }
    }

    FrameItem() = default;
    FrameItem(const FrameItem&) = delete;
    FrameItem& operator=(const FrameItem&) = delete;
};

using FramePtr = std::shared_ptr<FrameItem>;
#endif