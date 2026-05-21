                                #include "k180_h264_hub.h"

#include <cstring>
#include <gst/video/video.h>
#include <glib.h>

namespace k180 {

static inline uint64_t now_ns_mono_h264()
{
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static void on_bus_msg(GstBus*, GstMessage* msg, gpointer) {
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

bool H264Hub::request_idr()
{
    if (!pipeline || !enc) return false;
    if (!started.load(std::memory_order_acquire)) return false;

    bool ok = false;

    // 1) 優先嘗試 encoder action signal
    if (g_signal_lookup("force-idr", G_OBJECT_TYPE(enc)) != 0) {
        g_signal_emit_by_name(enc, "force-idr");
        ok = true;
    } else if (g_signal_lookup("force-key-unit", G_OBJECT_TYPE(enc)) != 0) {
        g_signal_emit_by_name(enc, "force-key-unit");
        ok = true;
    }

    // 2) fallback：ForceKeyUnit event
    if (!ok) {
        GstPad* sinkpad = gst_element_get_static_pad(enc, "sink");
        if (sinkpad) {
            GstClockTime now = gst_util_get_timestamp();
            GstEvent* ev = gst_video_event_new_downstream_force_key_unit(
                GST_CLOCK_TIME_NONE,
                GST_CLOCK_TIME_NONE,
                now,
                TRUE,
                0
            );
            if (ev) {
                ok = gst_pad_send_event(sinkpad, ev);
            }
            gst_object_unref(sinkpad);
        }
    }

#if defined(GST_DBG_MSG) && GST_DBG_MSG
    GSTD("[H264] request_idr -> %s\n", ok ? "OK" : "FAIL");
#endif
    return ok;
}

bool H264Hub::create(const H264HubCreateArgs& a)
{
    if (pipeline) stop();
    args_ = a;

    if (a.udp_port <= 0) {
        g_printerr("[H264] invalid udp_port=%d\n", a.udp_port);
        return false;
    }
	// int gop_sec = 1;  // 1 秒一個 keyframe（可改 2）
	// int iframe = std::max(1, a.fps * gop_sec);
    const int iframe_interval = (a.fps > 0) ? a.fps : 1;
    // appsrc(raw(memory:NVMM) RGBA) -> nvvidconv -> NVMM NV12 -> nvv4l2h264enc -> h264parse -> udpsink(loopback)
    std::string pipe_desc =
        "appsrc name=rawsrc is-live=true format=time do-timestamp=false block=false "
        "! video/x-raw(memory:NVMM),format=RGBA,width=" + std::to_string(a.in_w) + ",height=" + std::to_string(a.in_h) +
        ",framerate=" + std::to_string(a.fps) + "/1 "
        "! queue leaky=downstream max-size-buffers=1 max-size-bytes=0 max-size-time=0 "
        "! nvvidconv "
        "! video/x-raw(memory:NVMM),format=NV12,width=" + std::to_string(a.out_w) + ",height=" + std::to_string(a.out_h) + " "
        "! nvv4l2h264enc name=enc insert-sps-pps=1 iframeinterval=" + std::to_string(iframe_interval) + " bitrate=" + std::to_string(a.bitrate_bps) + " maxperf-enable=1 "
        "! h264parse config-interval=1 "
        "! video/x-h264,stream-format=byte-stream,alignment=au "
        "! udpsink name=rtsp_udp host=127.0.0.1 port=" + std::to_string(a.udp_port) + " sync=false async=false";
#if 0
    std::string pipe_desc =
        "appsrc name=rawsrc is-live=true format=time do-timestamp=false block=false "
        "! video/x-raw(memory:NVMM),format=RGBA,width=" + std::to_string(a.in_w) + ",height=" + std::to_string(a.in_h) +
        ",framerate=" + std::to_string(a.fps) + "/1 "
        // "! queue "
        "! queue leaky=downstream max-size-buffers=1 max-size-bytes=0 max-size-time=0 "
        "! nvvidconv "
        "! video/x-raw(memory:NVMM),format=NV12,width=" + std::to_string(a.out_w) + ",height=" + std::to_string(a.out_h) + " "
        "! nvv4l2h264enc name=enc insert-sps-pps=1 iframeinterval=" + std::to_string(iframe) + " bitrate=" + std::to_string(a.bitrate_bps) + " maxperf-enable=1 "
        "! h264parse "
        "! rtph264pay pt=96 mtu=1200 config-interval=1 "
        "! udpsink name=rtsp_udp host=127.0.0.1 port=" + std::to_string(a.udp_port) + " sync=false async=false";
#endif

    GError* err = nullptr;
    pipeline = gst_parse_launch(pipe_desc.c_str(), &err);
    if (!pipeline) {
        g_printerr("[H264] gst_parse_launch failed: %s\n", err ? err->message : "unknown");
        if (err) g_error_free(err);
        return false;
    }

    rawsrc = gst_bin_get_by_name(GST_BIN(pipeline), "rawsrc");
    if (!rawsrc) {
        g_printerr("[H264] failed to get rawsrc\n");
        gst_object_unref(pipeline);
        pipeline = nullptr;
        return false;
    }

	enc = gst_bin_get_by_name(GST_BIN(pipeline), "enc");
	if (!enc) {
		g_printerr("[H264] failed to get enc\n");
		gst_object_unref(rawsrc);
		rawsrc = nullptr;
		gst_object_unref(pipeline);
		pipeline = nullptr;
		return false;
	}
#if 0
	auto* asrc = GST_APP_SRC(rawsrc);
	GstCaps* caps = gst_caps_new_simple(
		"video/x-raw",
		"format",    G_TYPE_STRING, "RGBA",
		"width",     G_TYPE_INT,    a.in_w,
		"height",    G_TYPE_INT,    a.in_h,
		"framerate", GST_TYPE_FRACTION, a.fps, 1,
		NULL
	);
	// 關鍵：加 memory:NVMM（必須用 capsfilter 的方式加 features）
	GstCapsFeatures* f = gst_caps_features_new("memory:NVMM", NULL);
	gst_caps_set_features(caps, 0, f);

	gst_app_src_set_caps(asrc, caps);
	gst_caps_unref(caps);

    g_object_set(G_OBJECT(asrc),
                 "is-live", TRUE,
                 "format", GST_FORMAT_TIME,
                 "do-timestamp", FALSE,
                 "block", FALSE,
                 NULL);
#endif
    bus = gst_element_get_bus(pipeline);
    gst_bus_add_signal_watch(bus);
    g_signal_connect(bus, "message", G_CALLBACK(on_bus_msg), nullptr);

    t0_ns_.store(0, std::memory_order_relaxed);
    last_pts_.store(0, std::memory_order_relaxed);

    return true;
}

bool H264Hub::start()
{
    if (!pipeline) return false;
    auto r = gst_element_set_state(pipeline, GST_STATE_PLAYING);
    if (r == GST_STATE_CHANGE_FAILURE) return false;
    started.store(true, std::memory_order_release);
    return true;
}

void H264Hub::cleanup_()
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

	if (enc) {
		gst_object_unref(enc);
		enc = nullptr;
	}

    gst_object_unref(pipeline);
    pipeline = nullptr;
}

void H264Hub::stop()
{
    started.store(false, std::memory_order_release);
    if (!pipeline) return;
    cleanup_();
}

bool H264Hub::push_nvmm_rgba_buffer(GstBuffer* buf, uint64_t /*pts_ns*/)
{
    if (!buf) return false;
    if (!started.load(std::memory_order_acquire) ||
        !pipeline || !rawsrc ||
        args_.in_w <= 0 || args_.in_h <= 0 || args_.fps <= 0) {
        gst_buffer_unref(buf);
        return false;
    }

    const int out_fps = args_.fps;
    const GstClockTime dur = gst_util_uint64_scale_int(1, GST_SECOND, out_fps);
    if (dur == 0) {
        gst_buffer_unref(buf);
        return false;
    }
    const uint64_t now = now_ns_mono_h264();

    // FPS gating is done before the producer calls this function.
    // Do not thin frames here, or H264 appsrc/encoder can be starved.

    // (optional) pipeline state guard
    {
        GstState st = GST_STATE_NULL, pending = GST_STATE_NULL;
        gst_element_get_state(pipeline, &st, &pending, 1 * GST_MSECOND);
        if (st < GST_STATE_PAUSED) {
            gst_buffer_unref(buf);
            return false;
        }
    }

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

    return true;
}

} // namespace k180
