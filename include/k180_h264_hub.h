#pragma once

#include <gst/gst.h>
#include <gst/app/gstappsrc.h>

#include <atomic>
#include <cstdint>
#include <string>

#include "k180_stream_key.h"
#include "k180_gst_dbg.h"
#include "k180_osd_shared.h"

namespace k180 {

struct H264HubCreateArgs {
    int in_w = 1920, in_h = 1080;
    int out_w = 1920, out_h = 1080;
    int fps = 30;
    int bitrate_bps = 2'000'000;
    int udp_port = 0;
};

struct H264Hub {
    GstElement* pipeline = nullptr;
    GstElement* rawsrc   = nullptr;
    GstElement* enc   = nullptr;
    GstBus*     bus      = nullptr;
	std::atomic<uint64_t> t0_ns_{0};
	std::atomic<uint64_t> last_pts_{0};
	
    std::atomic<bool> started{false};

    H264HubCreateArgs args_;

    bool create(const H264HubCreateArgs& a);
    bool start();
    void stop();
    void cleanup_();

	bool push_nvmm_rgba_buffer(GstBuffer* buf, uint64_t pts_ns = 0);
	
	bool request_idr();
};

} // namespace k180
