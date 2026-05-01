#include "k180_h265_hub.h"
#include "k180_gst_dbg.h"
#include "k180_osd_meta.h"

#include <cstring>
#include <mutex>
#include <chrono>

#include <gst/app/gstappsrc.h>
#include <gst/video/video.h>

#include <time.h>     // clock_gettime
#include <stdint.h>
#include <stdio.h>

#include "gstnvdsmeta.h"
#include "nvdsmeta.h"
#include "k180_osd_slots.h"
#include "k180_osd_meta.h"
#include "k180_dbg_timing.h"

using k180::gstdbg::RateMon;
using k180::gstdbg::padprobe_rate;
using namespace k180::dbgtime;


namespace k180 {

// =========================
// local helpers
// =========================
static void on_bus_msg(GstBus* /*bus*/, GstMessage* msg, gpointer /*user_data*/) {
#if defined(GST_DBG_MSG) && GST_DBG_MSG
    switch (GST_MESSAGE_TYPE(msg)) {
    case GST_MESSAGE_ERROR: {
        GError* err = nullptr; gchar* dbg = nullptr;
        gst_message_parse_error(msg, &err, &dbg);
        g_printerr("GST ERROR: %s\n", err ? err->message : "unknown");
        if (dbg) g_printerr("GST DEBUG: %s\n", dbg);
        if (err) g_error_free(err);
        if (dbg) g_free(dbg);
        break;
    }
    case GST_MESSAGE_WARNING: {
        GError* err = nullptr; gchar* dbg = nullptr;
        gst_message_parse_warning(msg, &err, &dbg);
        g_printerr("GST WARN: %s\n", err ? err->message : "unknown");
        if (dbg) g_printerr("GST DEBUG: %s\n", dbg);
        if (err) g_error_free(err);
        if (dbg) g_free(dbg);
        break;
    }
    default: break;
    }
#else
    (void)msg;
#endif
}

static std::string now_yyyymmdd_hhmm() {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_r(&t, &tm);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d%02d%02d_%02d%02d",
                  tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                  tm.tm_hour, tm.tm_min);
    return buf;
}

static gchar* on_format_location_full(GstElement*, guint, GstSample*, gpointer ud)
{
    auto* ctx = static_cast<RecNameCtx*>(ud);
    std::string ts = now_yyyymmdd_hhmm();   // 只到分鐘
    std::string path = ctx->dir + "/" + ctx->prefix + ts + ".mp4";
    return g_strdup(path.c_str());
}

// =========================
// H265Hub
// =========================
bool H265Hub::create(const H265HubCreateArgs& a)
{
    // 如果允許重複 create：先把舊的收乾淨
    if (pipeline) {
        stop();
    }

    args_ = a;
	osd_shared_ = a.osd_shared;

    // ---- record branch ----
    std::string rec_branch;
    if (a.enable_record) {
        if (a.record_prefix.empty()) {
            g_printerr("[HUB] ERROR: enable_record=1 but record_path is empty\n");
            return false;
        }

        if (a.record_mp4) {		
            rec_branch =
				// "t. ! queue "
				"t. ! queue name=q_rec leaky=downstream max-size-buffers=1 max-size-bytes=0 max-size-time=0 "
				"! h265parse config-interval=1 "
				"! splitmuxsink name=smux muxer=qtmux async-finalize=true "
				"max-size-time=180000000000";
        } else {
            rec_branch =
                // "t. ! queue "
                "t. ! queue name=q_rec leaky=downstream max-size-buffers=1 max-size-bytes=0 max-size-time=0 "
                "! h265parse config-interval=1 "
                "! mpegtsmux "
                "! filesink location=\"" + a.record_path + "\"";
        }
    } else {
        rec_branch = ""; // ✅ 沒錄影就真的空
    }

    // ---- udp branch to feed RTSP ----
    if (a.udp_port <= 0) {
        g_printerr("[HUB] ERROR: invalid udp_port=%d\n", a.udp_port);
        return false;
    }

    std::string udp_branch =
        // "t. ! queue "
        "t. ! queue leaky=downstream max-size-buffers=1 max-size-bytes=0 max-size-time=0 "
        "! udpsink name=rtsp_udp host=127.0.0.1 port=" + std::to_string(a.udp_port) +
        " sync=false async=false ";
        // " sync=true async=false "

#if 0
	std::string udp_branch =
		"t. ! queue "
		"! rtph265pay config-interval=1 pt=96 mtu=1470 "
		"! udpsink name=rtsp_udp host=127.0.0.1 port=" + std::to_string(a.udp_port) +
		" sync=false async=false ";
#endif
    // ---- main pipeline ----
std::string pipe_desc =
    "nvstreammux name=mux batch-size=1 width=" + std::to_string(a.in_w) +
    " height=" + std::to_string(a.in_h) + " live-source=1 "
	"! nvdsosd name=OSD process-mode=1 display-text=1 "
    "! nvvidconv "
    "! video/x-raw(memory:NVMM),format=NV12,width=" + std::to_string(a.out_w) +
    ",height=" + std::to_string(a.out_h) + " "
    "! nvv4l2h265enc name=enc insert-sps-pps=1 vbv-size=160000 iframeinterval=30 bitrate=" +
    std::to_string(a.bitrate_bps) + " maxperf-enable=1 "
    "! h265parse config-interval=1 "
    "! video/x-h265,stream-format=byte-stream,alignment=au "
    "! tee name=t "
    "appsrc name=rawsrc is-live=true format=time do-timestamp=false block=false "
    "! video/x-raw(memory:NVMM),format=RGBA,width=" + std::to_string(a.in_w) +
    ",height=" + std::to_string(a.in_h) +
    ",framerate=" + std::to_string(a.fps) + "/1 "
    "! queue leaky=downstream max-size-buffers=1 max-size-bytes=0 max-size-time=0 "
    "! mux.sink_0 "
    + udp_branch +
    (rec_branch.empty() ? "" : (" " + rec_branch));

		// nvv4l2h265enc 我之前有多加一個，vbv-size=32000，這邊還沒加上去, insert-sps-pps=1 , 640000 堪用，但高飽和的顏色區塊還是會閃, 768000/896000 就開始會全畫面LAG

    GError* err = nullptr;
    pipeline = gst_parse_launch(pipe_desc.c_str(), &err);
    if (!pipeline) {
        g_printerr("[HUB] gst_parse_launch failed: %s\n", err ? err->message : "unknown");
        if (err) g_error_free(err);
        return false;
    }

if (osd_shared_) {
    // if (!k180::osd::osd_init_slots(*osd_shared_, 4, kMaxNumOutputBbox)) {
    if (!k180::osd::osd_init_slots(*osd_shared_, 4, 50)) {
        fprintf(stderr, "[OSD] osd_init_slots failed\n");
    }

    if (!k180::osd::attach_osd_probe_to_mux(pipeline, osd_shared_)) {
        fprintf(stderr, "[OSD] attach_osd_probe_to_mux failed\n");
    }
}

	if (a.enable_record && a.record_mp4) {
		// 注意：rec_ctx 必須是 H265Hub 的 member，不能是區域變數
		rec_ctx.dir    = a.record_path;                        // 或用 a.record_path 當資料夾
		rec_ctx.prefix = a.record_prefix;
		rec_ctx.ext    = "mp4";

		GstElement* smux = gst_bin_get_by_name(GST_BIN(pipeline), "smux");
		if (!smux) {
			g_printerr("[HUB] failed to get smux from pipeline\n");
			gst_object_unref(pipeline);
			pipeline = nullptr;
			return false;
		}

		// 連接 signal（你用 full）
		g_signal_connect(smux, "format-location-full",
						 G_CALLBACK(on_format_location_full), &rec_ctx);
		gst_object_unref(smux);
	}

    // ---- get rawsrc ----
    rawsrc = gst_bin_get_by_name(GST_BIN(pipeline), "rawsrc");
    if (!rawsrc) {
        g_printerr("[HUB] failed to get rawsrc from pipeline\n");
        gst_object_unref(pipeline);
        pipeline = nullptr;
        return false;
    }

    // ---- bus watch ----
    bus = gst_element_get_bus(pipeline);
    gst_bus_add_signal_watch(bus);
    g_signal_connect(bus, "message", G_CALLBACK(on_bus_msg), nullptr);

#if defined(GST_DBG_MSG) && GST_DBG_MSG
    // probes (optional)
    mon_raw_.tag = "sid? rawsrc src=" + std::to_string(a.udp_port);
    mon_enc_.tag = "sid? enc    src=" + std::to_string(a.udp_port);
    mon_udp_.tag = "sid? udpsink sink=" + std::to_string(a.udp_port);
    // rawsrc src pad
    {
        GstPad* p = gst_element_get_static_pad(rawsrc, "src");
        if (p) {
            gst_pad_add_probe(p, GST_PAD_PROBE_TYPE_BUFFER,
                              padprobe_rate, &mon_raw_, NULL);
            gst_object_unref(p);
        }
    }

    // enc src pad
    {
        GstElement* enc = gst_bin_get_by_name(GST_BIN(pipeline), "enc");
        if (enc) {
            GstPad* p = gst_element_get_static_pad(enc, "src");
            if (p) {
                gst_pad_add_probe(p, GST_PAD_PROBE_TYPE_BUFFER,
                                  padprobe_rate, &mon_enc_, NULL);
                gst_object_unref(p);
            }
            gst_object_unref(enc);
        }
    }

    // udpsink sink pad
    {
        GstElement* us = gst_bin_get_by_name(GST_BIN(pipeline), "rtsp_udp");
        if (us) {
            GstPad* p = gst_element_get_static_pad(us, "sink");
            if (p) {
                gst_pad_add_probe(p, GST_PAD_PROBE_TYPE_BUFFER,
                                  padprobe_rate, &mon_udp_, NULL);
                gst_object_unref(p);
            }
            gst_object_unref(us);
        }
    }
#endif
    // ---- reset runtime state (✅你加回來的做法 OK) ----
    // ring_ = GstFrameRing{};
	// ring_.reset();

	drop_acc_.store(0, std::memory_order_relaxed);
	frame_idx_.store(0, std::memory_order_relaxed);
t0_ns_ = 0;
#if defined(GST_DBG_MSG) && GST_DBG_MSG
    g_printerr("[HUB] created: udp_port=%d record=%d ring_slots=%d\n",
               a.udp_port, (int)a.enable_record, a.ring_slots);
#endif

    return true;
}


bool H265Hub::start()
{
    if (!pipeline) return false;
    auto r = gst_element_set_state(pipeline, GST_STATE_PLAYING);
    if (r == GST_STATE_CHANGE_FAILURE) return false;
    started.store(true, std::memory_order_release);
    return true;
}

void H265Hub::cleanup_()
{
    if (!pipeline) return;

    gst_element_set_state(pipeline, GST_STATE_NULL);

    if (bus) {
        gst_bus_remove_signal_watch(bus);
        gst_object_unref(bus);
        bus = nullptr;
    }
    if (rawsrc) {
        gst_object_unref(rawsrc);
        rawsrc = nullptr;
    }
    if (osd_shared_) {
        k180::osd::osd_reset_all_results(*osd_shared_);
        k180::osd::osd_destroy_slots(*osd_shared_);
    }
    gst_object_unref(pipeline);
    pipeline = nullptr;
}

void H265Hub::stop()
{
    started.store(false, std::memory_order_release);
    if (!pipeline) return;

    cleanup_();
}

void HubManager::stop_all()
{
    for (int i = 0; i < kStreams; ++i) {
        hubs[i].stop();
    }
}

void H265Hub::stop_rec(int wait_ms)
{
    started.store(false, std::memory_order_release);
    if (!pipeline) return;

    // 只有錄影（且用 splitmuxsink/mp4）才值得等 finalize
    const bool need_finalize = args_.enable_record ;
    if (need_finalize && rawsrc) {
        // 等同於讓 appsrc 發 EOS
        gst_app_src_end_of_stream(GST_APP_SRC(rawsrc));
    }

    if (need_finalize) {
        // 等 bus 收到 EOS 或 ERROR，給 muxer/splitmuxsink finalize 的時間
        GstBus* b = bus ? bus : gst_element_get_bus(pipeline);
        if (b) {
            GstClockTime timeout = (wait_ms <= 0) ? GST_CLOCK_TIME_NONE
                                                  : (GstClockTime)wait_ms * GST_MSECOND;

            GstMessage* msg = gst_bus_timed_pop_filtered(
                b, timeout,
                (GstMessageType)(GST_MESSAGE_EOS | GST_MESSAGE_ERROR)
            );
            if (msg) gst_message_unref(msg);
            if (!bus) gst_object_unref(b);
        }
    }
	cleanup_();
}

void HubManager::stop_all_rec(int wait_ms)
{
    for (int i = 0; i < kStreams; ++i) {
        hubs[i].stop_rec(wait_ms);
    }
}

#if 0
//無 fps限流
bool H265Hub::push_bgrx_frame(const uint8_t* data, int stride_bytes, uint64_t /*pts_ns*/)
{
    // basic checks
    if (!started.load(std::memory_order_acquire)) return false;
    if (!pipeline || !rawsrc || !data) return false;
    if (args_.in_w <= 0 || args_.in_h <= 0 || args_.fps <= 0) return false;
    if (stride_bytes <= 0) return false;

    const size_t frame_size = (size_t)stride_bytes * (size_t)args_.in_h;
    if (frame_size == 0) return false;

    // (可選) pipeline state guard
    {
        GstState st = GST_STATE_NULL, pending = GST_STATE_NULL;
        gst_element_get_state(pipeline, &st, &pending, 1 * GST_MSECOND);
        if (st < GST_STATE_PAUSED) return false;
    }

    // init ring on first frame
    if (ring_.bytes_per_frame() == 0) {
        const int slots = (args_.ring_slots > 0) ? args_.ring_slots : 8;
        if (!ring_.init((size_t)slots, frame_size)) {
            g_printerr("[HUB] ring init FAILED slots=%d frame_size=%zu\n", slots, frame_size);
            return false;
        }
        ring_.reset_stats();
        GSTD("[HUB] ring init OK slots=%d frame_size=%zu (stride=%d h=%d)\n",
             slots, frame_size, stride_bytes, args_.in_h);
    } else {
        if (ring_.bytes_per_frame() != frame_size) {
            g_printerr("[HUB] frame_size changed (%zu -> %zu). Need stop/recreate hub.\n",
                       ring_.bytes_per_frame(), frame_size);
            return false;
        }
    }

    // acquire slot
    size_t idx = 0;
    uint8_t* dst = ring_.try_acquire_slot(&idx);
    if (!dst) return false;

    // memcpy
    std::memcpy(dst, data, frame_size);

    // wrap buffer
    GstBuffer* buf = ring_.wrap_slot_as_buffer(idx, frame_size);
    if (!buf) {
        // 注意：wrap_slot_as_buffer 內部目前只會 inuse_[idx]=false
        // 如果你要統計準確，建議也在 ring 裡補 inuse_now_--
        return false;
    }

    // VideoMeta (stride)
    {
        gsize offsets[GST_VIDEO_MAX_PLANES] = {0,0,0,0};
        gint  strides[GST_VIDEO_MAX_PLANES] = {stride_bytes,0,0,0};
        gst_buffer_add_video_meta_full(
            buf,
            (GstVideoFrameFlags)0,
            GST_VIDEO_FORMAT_BGRx,
            (guint)args_.in_w,
            (guint)args_.in_h,
            1,
            offsets,
            strides
        );
    }

    // timestamps
    const GstClockTime dur = gst_util_uint64_scale_int(1, GST_SECOND, args_.fps);
    const uint64_t fi = frame_idx_.fetch_add(1, std::memory_order_relaxed);
    const GstClockTime pts = fi * dur;

    GST_BUFFER_PTS(buf)      = pts;
    GST_BUFFER_DTS(buf)      = pts;
    GST_BUFFER_DURATION(buf) = dur;

    // push
    GstFlowReturn ret = gst_app_src_push_buffer(GST_APP_SRC(rawsrc), buf);
    if (ret != GST_FLOW_OK) {
        gst_buffer_unref(buf);
        return false;
    }
    return true;
}
#endif

#if 0
bool H265Hub::push_rgba_frame(const uint8_t* data, int stride_bytes, uint64_t /*pts_ns*/)
{
    // basic checks
    if (!started.load(std::memory_order_acquire)) return false;
    if (!pipeline || !rawsrc || !data) return false;
    if (args_.in_w <= 0 || args_.in_h <= 0) return false;
    if (stride_bytes <= 0) return false;

    // ------------------------------
    // FPS thinning (assume producer ~30fps)
    // target fps: 20/15/10/5/1
    // ------------------------------

    constexpr int IN_FPS = 30;
    int out_fps = args_.fps;
#if 0
    // accumulator decision
    // allow 30: always keep
    if (out_fps < IN_FPS) {
        int acc = drop_acc_.load(std::memory_order_relaxed);
        acc += out_fps;

        if (acc < IN_FPS) {
            // drop this frame (store back and exit BEFORE memcpy)
            drop_acc_.store(acc, std::memory_order_relaxed);
            return false;
        }

        acc -= IN_FPS;
        drop_acc_.store(acc, std::memory_order_relaxed);
    }
    // out_fps == 30 -> keep all
#endif
    // ------------------------------
    // existing logic (unchanged)
    // ------------------------------
    const size_t frame_size = (size_t)stride_bytes * (size_t)args_.in_h;
    if (frame_size == 0) return false;

    // (optional) pipeline state guard
    {
        GstState st = GST_STATE_NULL, pending = GST_STATE_NULL;
        gst_element_get_state(pipeline, &st, &pending, 1 * GST_MSECOND);
        if (st < GST_STATE_PAUSED) return false;
    }

    // init ring on first frame
    if (ring_.bytes_per_frame() == 0) {
        const int slots = (args_.ring_slots > 0) ? args_.ring_slots : 128;
        if (!ring_.init((size_t)slots, frame_size)) {
            g_printerr("[HUB] ring init FAILED slots=%d frame_size=%zu\n", slots, frame_size);
            return false;
        }
        ring_.reset_stats();
        GSTD("[HUB] ring init OK slots=%d frame_size=%zu (stride=%d h=%d)\n",
             slots, frame_size, stride_bytes, args_.in_h);
    } else {
        if (ring_.bytes_per_frame() != frame_size) {
            g_printerr("[HUB] frame_size changed (%zu -> %zu). Need stop/recreate hub.\n",
                       ring_.bytes_per_frame(), frame_size);
            return false;
        }
    }

    // acquire slot
    size_t idx = 0;
    uint8_t* dst = ring_.try_acquire_slot(&idx);
    if (!dst) return false;

    // memcpy
    std::memcpy(dst, data, frame_size);

    // wrap buffer
    GstBuffer* buf = ring_.wrap_slot_as_buffer(idx, frame_size);
    if (!buf) return false;

    // VideoMeta (stride)
    {
        gsize offsets[GST_VIDEO_MAX_PLANES] = {0,0,0,0};
        gint  strides[GST_VIDEO_MAX_PLANES] = {stride_bytes,0,0,0};
        gst_buffer_add_video_meta_full(
            buf,
            (GstVideoFrameFlags)0,
            GST_VIDEO_FORMAT_RGBA,
            (guint)args_.in_w,
            (guint)args_.in_h,
            1,
            offsets,
            strides
        );
    }
#if 0
    // timestamps (based on OUTPUT fps, not input)
    const GstClockTime dur = gst_util_uint64_scale_int(1, GST_SECOND, out_fps);
    const uint64_t fi = frame_idx_.fetch_add(1, std::memory_order_relaxed);
    const GstClockTime pts = fi * dur;

    GST_BUFFER_PTS(buf)      = pts;
    GST_BUFFER_DTS(buf)      = pts;
    GST_BUFFER_DURATION(buf) = dur;
#endif
// #if 0
	const GstClockTime dur = gst_util_uint64_scale_int(1, GST_SECOND, out_fps);

	// base on first pushed frame
	if (t0_ns_ == 0) {
		t0_ns_ = now_ns_mono();
		frame_idx_.store(0, std::memory_order_relaxed); // 可留可不留；留著方便你 debug
	}

	// PTS from monotonic clock (ns), GstClockTime is also ns
	const GstClockTime pts = (GstClockTime)(now_ns_mono() - t0_ns_);

	GST_BUFFER_PTS(buf)      = pts;
	GST_BUFFER_DTS(buf)      = pts;
	GST_BUFFER_DURATION(buf) = dur;
// #endif
    GstFlowReturn ret = gst_app_src_push_buffer(GST_APP_SRC(rawsrc), buf);
    if (ret != GST_FLOW_OK) {
        gst_buffer_unref(buf);
        return false;
    }
    return true;
}
#endif

bool H265Hub::push_rgba_frame(const uint8_t* data, int stride_bytes, uint64_t)
{
    if (!started.load(std::memory_order_acquire)) return false;
    if (!pipeline || !rawsrc || !data) return false;

    const int out_fps = args_.fps;
    const GstClockTime dur = gst_util_uint64_scale_int(1, GST_SECOND, out_fps);

    // --- allocate a fresh GstBuffer every frame (TEST) ---
    const size_t frame_size = (size_t)stride_bytes * (size_t)args_.in_h;
    GstBuffer* buf = gst_buffer_new_allocate(nullptr, frame_size, nullptr);
    if (!buf) return false;

    GstMapInfo mi{};
    if (!gst_buffer_map(buf, &mi, GST_MAP_WRITE)) {
        gst_buffer_unref(buf);
        return false;
    }
    std::memcpy(mi.data, data, frame_size);
    gst_buffer_unmap(buf, &mi);

    // VideoMeta (stride)
    {
        gsize offsets[GST_VIDEO_MAX_PLANES] = {0,0,0,0};
        gint  strides[GST_VIDEO_MAX_PLANES] = {stride_bytes,0,0,0};
        gst_buffer_add_video_meta_full(
            buf, (GstVideoFrameFlags)0,
            GST_VIDEO_FORMAT_RGBA,
            (guint)args_.in_w, (guint)args_.in_h,
            1, offsets, strides
        );
    }

    // PTS: monotonic based
    if (t0_ns_ == 0) t0_ns_ = now_ns_mono();
    const GstClockTime pts = (GstClockTime)(now_ns_mono() - t0_ns_);

    GST_BUFFER_PTS(buf)      = pts;
    GST_BUFFER_DTS(buf)      = pts;
    GST_BUFFER_DURATION(buf) = dur;

    GstFlowReturn ret = gst_app_src_push_buffer(GST_APP_SRC(rawsrc), buf);
    if (ret != GST_FLOW_OK) {
        gst_buffer_unref(buf);
        return false;
    }
    return true;
}

bool H265Hub::push_bgrx_frame(const uint8_t* data, int stride_bytes, uint64_t /*pts_ns*/)
{
    // basic checks
    if (!started.load(std::memory_order_acquire)) return false;
    if (!pipeline || !rawsrc || !data) return false;
    if (args_.in_w <= 0 || args_.in_h <= 0) return false;
    if (stride_bytes <= 0) return false;

    // ------------------------------
    // FPS thinning (assume producer ~30fps)
    // target fps: 20/15/10/5/1
    // ------------------------------
    constexpr int IN_FPS = 30;
    int out_fps = args_.fps;

    // accumulator decision
    // allow 30: always keep
    if (out_fps < IN_FPS) {
        int acc = drop_acc_.load(std::memory_order_relaxed);
        acc += out_fps;

        if (acc < IN_FPS) {
            // drop this frame (store back and exit BEFORE memcpy)
            drop_acc_.store(acc, std::memory_order_relaxed);
            return false;
        }

        acc -= IN_FPS;
        drop_acc_.store(acc, std::memory_order_relaxed);
    }
    // out_fps == 30 -> keep all

    // ------------------------------
    // existing logic (unchanged)
    // ------------------------------
    const size_t frame_size = (size_t)stride_bytes * (size_t)args_.in_h;
    if (frame_size == 0) return false;

    // (optional) pipeline state guard
    {
        GstState st = GST_STATE_NULL, pending = GST_STATE_NULL;
        gst_element_get_state(pipeline, &st, &pending, 1 * GST_MSECOND);
        if (st < GST_STATE_PAUSED) return false;
    }

    // init ring on first frame
    if (ring_.bytes_per_frame() == 0) {
        const int slots = (args_.ring_slots > 0) ? args_.ring_slots : 8;
        if (!ring_.init((size_t)slots, frame_size)) {
            g_printerr("[HUB] ring init FAILED slots=%d frame_size=%zu\n", slots, frame_size);
            return false;
        }
        ring_.reset_stats();
        GSTD("[HUB] ring init OK slots=%d frame_size=%zu (stride=%d h=%d)\n",
             slots, frame_size, stride_bytes, args_.in_h);
    } else {
        if (ring_.bytes_per_frame() != frame_size) {
            g_printerr("[HUB] frame_size changed (%zu -> %zu). Need stop/recreate hub.\n",
                       ring_.bytes_per_frame(), frame_size);
            return false;
        }
    }

    // acquire slot
    size_t idx = 0;
    uint8_t* dst = ring_.try_acquire_slot(&idx);
    if (!dst) return false;

    // memcpy
    std::memcpy(dst, data, frame_size);

    // wrap buffer
    GstBuffer* buf = ring_.wrap_slot_as_buffer(idx, frame_size);
    if (!buf) return false;

    // VideoMeta (stride)
    {
        gsize offsets[GST_VIDEO_MAX_PLANES] = {0,0,0,0};
        gint  strides[GST_VIDEO_MAX_PLANES] = {stride_bytes,0,0,0};
        gst_buffer_add_video_meta_full(
            buf,
            (GstVideoFrameFlags)0,
            GST_VIDEO_FORMAT_BGRx,
            (guint)args_.in_w,
            (guint)args_.in_h,
            1,
            offsets,
            strides
        );
    }

    // timestamps (based on OUTPUT fps, not input)
    const GstClockTime dur = gst_util_uint64_scale_int(1, GST_SECOND, out_fps);
    const uint64_t fi = frame_idx_.fetch_add(1, std::memory_order_relaxed);
    const GstClockTime pts = fi * dur;

    GST_BUFFER_PTS(buf)      = pts;
    GST_BUFFER_DTS(buf)      = pts;
    GST_BUFFER_DURATION(buf) = dur;

    // push
    GstFlowReturn ret = gst_app_src_push_buffer(GST_APP_SRC(rawsrc), buf);
    if (ret != GST_FLOW_OK) {
        gst_buffer_unref(buf);
        return false;
    }
    return true;
}

bool H265Hub::push_nvmm_rgba_buffer(GstBuffer* buf, uint64_t /*pts_ns*/)
{
    if (!started.load(std::memory_order_acquire)) return false;
    if (!pipeline || !rawsrc || !buf) return false;
    if (args_.in_w <= 0 || args_.in_h <= 0) return false;

    // ------------------------------
    // FPS thinning (assume producer ~30fps)
    // target fps: 20/15/10/5/1
    // ------------------------------
    constexpr int IN_FPS = 30;
    const int out_fps = args_.fps;
	// printf("========== %d ========\n", out_fps);

    if (out_fps < IN_FPS) {
        int acc = drop_acc_.load(std::memory_order_relaxed);
        acc += out_fps;

        if (acc < IN_FPS) {
            drop_acc_.store(acc, std::memory_order_relaxed);
            gst_buffer_unref(buf);           // IMPORTANT: avoid leak on drop
            return false;
        }

        acc -= IN_FPS;
        drop_acc_.store(acc, std::memory_order_relaxed);
    }

    // (optional) pipeline state guard (same idea as your old code)
    {
        GstState st = GST_STATE_NULL, pending = GST_STATE_NULL;
        gst_element_get_state(pipeline, &st, &pending, 1 * GST_MSECOND);
        if (st < GST_STATE_PAUSED) {
            gst_buffer_unref(buf);
            return false;
        }
    }

    // ------------------------------
    // timestamps (based on OUTPUT fps)
    // - appsrc is format=time + do-timestamp=false, so we must set them.
    // - use a stable timeline: pts = frame_idx * duration
    // ------------------------------
#if 0 // 原本的方法
    const GstClockTime dur = gst_util_uint64_scale_int(1, GST_SECOND, out_fps);
    const uint64_t fi = frame_idx_.fetch_add(1, std::memory_order_relaxed);
    const GstClockTime pts = (GstClockTime)(fi * dur);
	GstBuffer* wbuf = gst_buffer_make_writable(buf);
	buf = wbuf;
    // make writable before modifying meta/timestamps
    // buf = gst_buffer_make_writable(buf);

    GST_BUFFER_PTS(buf)      = pts;
    GST_BUFFER_DTS(buf)      = pts;
    GST_BUFFER_DURATION(buf) = dur;

	// 第二種方法
	const GstClockTime dur = gst_util_uint64_scale_int(1, GST_SECOND, out_fps);

	// base on first pushed frame
	if (t0_ns_ == 0) {
		t0_ns_ = now_ns_mono();
		frame_idx_.store(0, std::memory_order_relaxed); // 可留可不留；留著方便你 debug
	}

	// PTS from monotonic clock (ns), GstClockTime is also ns
	const GstClockTime pts = (GstClockTime)(now_ns_mono() - t0_ns_);

	GST_BUFFER_PTS(buf)      = pts;
	GST_BUFFER_DTS(buf)      = pts;
	GST_BUFFER_DURATION(buf) = dur;

	// 第三種方法：把 pts 對齊到 dur 的格點：PTS 只會以 dur 為單位跳，不會每次因抖動出現奇怪的間距
	GstBuffer* wbuf = gst_buffer_make_writable(buf);
	if (!wbuf) return false;   // 保守檢查
	buf = wbuf;

	const GstClockTime dur = gst_util_uint64_scale_int(1, GST_SECOND, out_fps);

	uint64_t now = now_ns_mono();
	if (t0_ns_ == 0) {
		t0_ns_ = now;
		last_pts_ = 0;
	}

	GstClockTime raw = (GstClockTime)(now - t0_ns_);

	// 量化到 dur 格點，減少 jitter（四捨五入或直接 floor 都可）
	GstClockTime pts = (raw / dur) * dur;

	// 保證單調遞增（至少 +dur），避免偶發倒退或重複
	if (pts <= last_pts_) pts = last_pts_ + dur;
	last_pts_ = pts;

	GST_BUFFER_PTS(buf)      = pts;
	GST_BUFFER_DTS(buf)      = pts;
	GST_BUFFER_DURATION(buf) = dur;
#endif
	
	// 第四種方法：在第三種方法上，增加 atomic 操作, 理論應該不需要 atomic，因為每個 stream 都是獨立的 hub, 但我腦累，懶得想了
	GstClockTime dur = gst_util_uint64_scale_int(1, GST_SECOND, out_fps);

	uint64_t now = now_ns_mono();

	// init t0 once
	uint64_t t0 = t0_ns_.load(std::memory_order_relaxed);
	if (t0 == 0) {
		uint64_t expected = 0;
		if (t0_ns_.compare_exchange_strong(expected, now, std::memory_order_relaxed)) {
			last_pts_.store(0, std::memory_order_relaxed);
			t0 = now;
		} else {
			t0 = t0_ns_.load(std::memory_order_relaxed);
		}
	}

	GstClockTime raw = (GstClockTime)(now - t0);

	// quantize
	GstClockTime q = (raw / dur) * dur;

	// enforce monotonic (+dur)
	uint64_t prev = last_pts_.load(std::memory_order_relaxed);
	GstClockTime pts = (q <= prev) ? (prev + dur) : q;
	last_pts_.store((uint64_t)pts, std::memory_order_relaxed);

	GST_BUFFER_PTS(buf)      = pts;
	GST_BUFFER_DTS(buf)      = pts;
	GST_BUFFER_DURATION(buf) = dur;
    // ------------------------------
    // push (appsrc takes ownership of buf on success)
    // ------------------------------
    GstFlowReturn ret = gst_app_src_push_buffer(GST_APP_SRC(rawsrc), buf);
    if (ret != GST_FLOW_OK) {
        gst_buffer_unref(buf);   // appsrc didn't take it
        return false;
    }
// static thread_local k180::dbgtime::FpsMon s_fps("hub_push_nvmm_rgba");
// s_fps.tick();
    return true;
}

// =========================
// HubManager
// =========================
HubManager::HubManager()
{
    for (int i = 0; i < kStreams; ++i) {
        viewers[i].store(0, std::memory_order_relaxed);
        record_enabled[i].store(false, std::memory_order_relaxed);
    }
    for (int i = 0; i < kH264Streams; ++i) {
        viewers_h264[i].store(0, std::memory_order_relaxed);
    }
}

void HubManager::viewer_inc(StreamKey k)
{
    int idx = stream_index(k);
    int v = viewers[idx].fetch_add(1, std::memory_order_relaxed) + 1;
#if defined(GST_DBG_MSG) && GST_DBG_MSG		
	GSTD("[RTSP] viewers[%s_%d]++ => %d\n",
         group_name(k.g), view_number(k.v), v);
#else
	(void)v;
#endif
}

void HubManager::viewer_dec(StreamKey k)
{
    // never go below 0 (CAS loop)
    int idx = stream_index(k);
    auto& a = viewers[idx];
    int cur = a.load(std::memory_order_relaxed);
    while (cur > 0) {
        if (a.compare_exchange_weak(cur, cur - 1, std::memory_order_relaxed)) break;
    }
    GSTD("[RTSP] viewers[%s_%d]-- => %d\n",
         group_name(k.g), view_number(k.v), a.load(std::memory_order_relaxed));
}

bool HubManager::want_push(StreamKey k) const
{
    int idx = stream_index(k);
    return viewers[idx].load(std::memory_order_relaxed) > 0
        || record_enabled[idx].load(std::memory_order_relaxed);
}
#if 0
void HubManager::set_record_mode(int recorded_mode)
{
    // recorded_mode:
    // 0: none
    // 1: record s1_1234
    // 2: record s1_1..4
    for (int i = 0; i < kStreams; ++i) record_enabled[i].store(false, std::memory_order_relaxed);

    auto set_on = [&](StreamGroup g, StreamView v) {
        record_enabled[ stream_index({g,v}) ].store(true, std::memory_order_relaxed);
    };

    if (recorded_mode == 1) {
        set_on(StreamGroup::S1, StreamView::V1234);
    } else if (recorded_mode == 2) {
        set_on(StreamGroup::S1, StreamView::V1);
        set_on(StreamGroup::S1, StreamView::V2);
        set_on(StreamGroup::S1, StreamView::V3);
        set_on(StreamGroup::S1, StreamView::V4);
    }
}
#endif
void HubManager::viewer_inc_h264(StreamKey k)
{
    int idx = h264_index(k.g, k.v);
    int v = viewers_h264[idx].fetch_add(1, std::memory_order_relaxed) + 1;
#if defined(GST_DBG_MSG) && GST_DBG_MSG
    GSTD("[RTSP] viewers_h264[%s_%d]++ => %d\n", group_name(k.g), view_number(k.v), v);
#else
    (void)v;
#endif
}

void HubManager::viewer_dec_h264(StreamKey k)
{
    int idx = h264_index(k.g, k.v);
    auto& a = viewers_h264[idx];
    int cur = a.load(std::memory_order_relaxed);
    while (cur > 0) {
        if (a.compare_exchange_weak(cur, cur - 1, std::memory_order_relaxed)) break;
    }
    GSTD("[RTSP] viewers_h264[%s_%d]-- => %d\n",
         group_name(k.g), view_number(k.v), a.load(std::memory_order_relaxed));
}

bool HubManager::want_push_h264(StreamKey k) const
{
    int idx = h264_index(k.g, k.v);
    return viewers_h264[idx].load(std::memory_order_relaxed) > 0;
}

void HubManager::stop_all_h264()
{
    for (int i = 0; i < kH264Streams; ++i) {
        hubs_h264[i].stop();
    }
}

void HubManager::request_idr_h264(StreamKey k)
{
    // 只允許 V1..V4
    if ((int)k.v < (int)StreamView::V1 || (int)k.v > (int)StreamView::V4) return;

    const int idx = h264_index(k.g, k.v);
    if (idx < 0 || idx >= kH264Streams) return;

    hubs_h264[idx].request_idr();
}

}
