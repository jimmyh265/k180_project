#pragma once

#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include <gst/rtsp-server/rtsp-server.h>

#include <atomic>
#include <cstdint>
#include <string>

#include "k180_gst_ringbuf.h"
#include "k180_stream_key.h"
#include "k180_gst_dbg.h"
#include "k180_h264_hub.h"
#include "k180_osd_shared.h"

// =========================
// H265 Hub
// =========================
namespace k180 {
	
struct RecNameCtx {
    std::string dir;     // 資料夾，例如 "/data"
    std::string prefix;  // 例如 "s1_1234_"
    std::string ext;     // 例如 "mp4"
};

struct H265HubCreateArgs {
    int in_w = 1920, in_h = 1080;
    int out_w = 1920, out_h = 1080;
    int fps = 30;
    int bitrate_bps = 4'000'000;

    int udp_port = 5600;

    // recording
    bool enable_record = false;
    bool record_mp4 = true;
    std::string record_path;
    std::string record_prefix;

    // ring
    int ring_slots = 8;
	
	k180::osd::OsdShared* osd_shared = nullptr;
};

struct H265Hub {
    GstElement* pipeline = nullptr;
    GstElement* rawsrc   = nullptr;
    GstBus*     bus      = nullptr;
	std::atomic<uint64_t> t0_ns_{0};
	std::atomic<uint64_t> last_pts_{0};
    GstFrameRing ring_;
	std::atomic<uint64_t> frame_idx_{0};

    H265HubCreateArgs args_;
    std::atomic<bool> started{false};
	
    std::atomic<int>      throttle_last_fps_{0};
    std::atomic<uint64_t> throttle_next_ns_{0}; // next allowed push time (monotonic ns)
    std::atomic<uint64_t> pts_base_ns_{0};      // base time for PTS (monotonic ns)
	
    bool create(const H265HubCreateArgs& a);
    bool start();
    void stop();
	void stop_rec(int wait_ms);
	void cleanup_();

    // memcpy + ring buffer
    bool push_rgba_frame(const uint8_t* data, int stride_bytes, uint64_t pts_ns = 0);
    bool push_bgrx_frame(const uint8_t* data, int stride_bytes, uint64_t pts_ns = 0);
    bool push_nvmm_rgba_buffer(GstBuffer* buf, uint64_t pts_ns = 0);
	
#if defined(GST_DBG_MSG) && GST_DBG_MSG
    k180::gstdbg::RateMon mon_raw_;
    k180::gstdbg::RateMon mon_enc_;
    k180::gstdbg::RateMon mon_udp_;
#endif

	std::atomic<int> drop_acc_{0};     // fps限流 累加器（0..29）
	RecNameCtx rec_ctx;
	
	k180::osd::OsdShared* osd_shared_ = nullptr;
};

inline constexpr int kH264Views = 4;                 // V1..V4
inline constexpr int kH264Streams = kGroups * kH264Views; // 8

inline constexpr int h264_index(StreamGroup g, StreamView v) {
    // v must be V1..V4
    return static_cast<int>(g) * kH264Views + (static_cast<int>(v) - 1);
}

// =========================
// Hub Manager (10 hubs)
// =========================
struct HubManager {
    // ---- NEW: H264 hubs for V1..V4 only ----
    H264Hub hubs_h264[kH264Streams];
    int udp_ports_h264[kH264Streams] = {0};
	std::atomic<int> viewers_h264[kH264Streams];
	
    H265Hub hubs[kStreams];
	int udp_ports[kStreams] = {0};
    std::atomic<int> viewers[kStreams];       // viewer count per stream
    std::atomic<bool> record_enabled[kStreams];

    HubManager();

    // viewer counter API
    void viewer_inc(StreamKey k);
    void viewer_dec(StreamKey k);
    void viewer_inc_h264(StreamKey k);
    void viewer_dec_h264(StreamKey k);
	
    bool want_push(StreamKey k) const;
	bool want_push_h264(StreamKey k) const;
    // helper to set record_enabled based on cfggg.recorded
    // recorded_mode: 0=off, 1=record s1_1234, 2=record s1_1..4
    // void set_record_mode(int recorded_mode);
	void stop_all();
	void stop_all_rec(int wait_ms);
	void stop_all_h264();
	void request_idr_h264(StreamKey k);
};

}