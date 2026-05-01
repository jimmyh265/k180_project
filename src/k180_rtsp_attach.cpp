// k180_rtsp_attach.cpp

#include "k180_rtsp_attach.h"      // 你自己的宣告（attach_factories 等）
#include "k180_h265_hub.h"         // 會用到 HubManager / StreamKey / viewer_inc/dec（或你自己的介面）
#include "k180_gst_dbg.h"
#include "k180_stream_key.h"

#include <gst/gst.h>
#include <gst/rtsp-server/rtsp-server.h>   // GstRTSPServer / GstRTSPMountPoints / GstRTSPMediaFactory / GstRTSPMedia
#include <gst/app/gstappsrc.h>


#include <glib.h>                  // gpointer / g_free / g_strdup / g_print / g_printerr（若你有用）
#include <mutex>                   // 若你在 callback 裡用 std::mutex / lock_guard
#include <atomic>                  // viewer 計數用 atomic
#include <string>                  // std::string（若你用）
#include <unordered_map>           // 若你有 path->StreamKey mapping

namespace k180 {
// =========================
// RTSP glue (factory callbacks)
// =========================
struct FactoryCtx {
    HubManager* mgr;
    StreamKey   key;
    int         udp_port;
	bool        is_h264;
};

static void on_media_prepared(GstRTSPMedia* media, gpointer user_data)
{
#if defined(GST_DBG_MSG) && GST_DBG_MSG
    GstElement* e = gst_rtsp_media_get_element(media);
    GstState s = GST_STATE_NULL;
    // gst_element_get_state(e, &s, NULL, 0);
	gst_element_get_state(e, &s, NULL, GST_SECOND);
    GSTD("[RTSP] media prepared, element state=%s\n", gst_element_state_get_name(s));
    gst_object_unref(e);
#endif
}

static void on_media_unprepared(GstRTSPMedia* media, gpointer user_data)
{
    auto* ctx = static_cast<FactoryCtx*>(user_data);
    (void)media;
    if (!ctx || !ctx->mgr) return;

    if (ctx->is_h264) ctx->mgr->viewer_dec_h264(ctx->key);
    else              ctx->mgr->viewer_dec(ctx->key);
#if defined(GST_DBG_MSG) && GST_DBG_MSG
	GSTD("[RTSP] media unprepared (client disconnected?)\n");
#endif
}

static void media_configure_cb(GstRTSPMediaFactory* /*factory*/, GstRTSPMedia* media, gpointer user_data)
{
#if defined(GST_DBG_MSG) && GST_DBG_MSG
	GSTD("[media_configure_cb]\n");
#endif
    // This is called when media pipeline is created for a client.
    auto* ctx = static_cast<FactoryCtx*>(user_data);
    if (!ctx || !ctx->mgr) return;

    // if (ctx->is_h264) ctx->mgr->viewer_inc_h264(ctx->key);
    // else              ctx->mgr->viewer_inc(ctx->key);
	
	if (ctx->is_h264) {
		ctx->mgr->viewer_inc_h264(ctx->key);
		ctx->mgr->request_idr_h264(ctx->key);   // ★加這行
	} else {
		ctx->mgr->viewer_inc(ctx->key);
	}
    // Connect prepared/unprepared (viewer++/--)
    g_signal_connect(media, "prepared",   G_CALLBACK(on_media_prepared),   ctx);
    g_signal_connect(media, "unprepared", G_CALLBACK(on_media_unprepared), ctx);
}

static void destroy_ctx(gpointer p)
{
    auto* ctx = static_cast<FactoryCtx*>(p);
    delete ctx;
}

static std::string mount_path_h264(StreamGroup g, StreamView v)
{
    // "/s1_1_h264"
    std::string s = "/";
    s += group_name(g);
    s += "_";
    s += std::to_string(view_number(v));
    s += "_h264";
    return s;
}

static std::string mount_path(StreamGroup g, StreamView v)
{
    // "/s1_1", "/s1_1234"
    std::string s = "/";
    s += group_name(g);
    s += "_";
    s += std::to_string(view_number(v));
    return s;
}

static std::string factory_launch_h264(int udp_port)
{
    std::string launch =
        "( "
        "udpsrc name=usrc port=" + std::to_string(udp_port) +
        " caps=\"video/x-h264,stream-format=byte-stream,alignment=au\" "
        "! queue name=q_udpin max-size-buffers=0 max-size-bytes=0 max-size-time=0 "
        "! h264parse config-interval=1 "
        "! rtph264pay name=pay0 pt=96 mtu=1470 "
        ")";
#if 0
    std::string launch =
        "( "
        "udpsrc name=usrc port=" + std::to_string(udp_port) +
        " caps=\"application/x-rtp,media=video,encoding-name=H264,payload=96\" "
        "! rtpjitterbuffer latency=0 "
        "! rtph264depay "
        "! h264parse "
        "! rtph264pay name=pay0 pt=96 mtu=1470 config-interval=1 "
        ")";
#endif
    return launch;
}

static std::string factory_launch(int udp_port)
{
    // RTSP per-client pipeline:
    // udpsrc -> queue -> h265parse -> rtph265pay pay0
    // NOTE: name=usrc and name=pay0 are kept for debugging / pad-probes if you want later.

    std::string launch =
        "( "
        "udpsrc name=usrc port=" + std::to_string(udp_port) +
        " caps=\"video/x-h265,stream-format=byte-stream,alignment=au\" "
        "! queue name=q_udpin max-size-buffers=0 max-size-bytes=0 max-size-time=0 "
        "! h265parse config-interval=1 "
        "! rtph265pay name=pay0 pt=96 mtu=1470 "
        ")";

#if 0

    std::string launch =
        "( "
        "udpsrc name=usrc port=" + std::to_string(udp_port) +
        " caps=\"application/x-rtp,media=video,encoding-name=H265,payload=96\" "
        "! rtpjitterbuffer latency=0 "
        "! rtph265depay "
        "! h265parse "
        "! rtph265pay name=pay0 pt=96 mtu=1470 config-interval=1 "
        ")";
#endif
    return launch;
}

void attach_factories(GstRTSPMountPoints* mounts, HubManager& mgr)
{
    for (int gi = 0; gi < kGroups; ++gi) {
        for (int vi = 0; vi < kViews; ++vi) {
            StreamKey k{ (StreamGroup)gi, (StreamView)vi };
            int idx = stream_index(k);
            int port = mgr.udp_ports[idx];

            std::string path = mount_path(k.g, k.v);
            std::string launch = factory_launch(port);

            auto* f = gst_rtsp_media_factory_new();
            gst_rtsp_media_factory_set_launch(f, launch.c_str());
            gst_rtsp_media_factory_set_shared(f, TRUE);

			auto* ctx = new FactoryCtx{ &mgr, k, port };
            g_signal_connect_data(f, "media-configure",
                                  G_CALLBACK(media_configure_cb),
                                  ctx,
                                  (GClosureNotify)destroy_ctx,
                                  (GConnectFlags)0);

            gst_rtsp_mount_points_add_factory(mounts, path.c_str(), f);

            GSTD("[RTSP] mount %s -> udp_port=%d\n", path.c_str(), port);
        }
    }

    // ---- H264: 新增 8 路（s1/s2 各 V1..V4）----
    for (int gi = 0; gi < kGroups; ++gi) {
        for (int vi = (int)StreamView::V1; vi <= (int)StreamView::V4; ++vi) {
            StreamKey k{ (StreamGroup)gi, (StreamView)vi };
            int idx = h264_index(k.g, k.v);
            int port = mgr.udp_ports_h264[idx];

            std::string path = mount_path_h264(k.g, k.v);
            std::string launch = factory_launch_h264(port);

            auto* f = gst_rtsp_media_factory_new();
            gst_rtsp_media_factory_set_launch(f, launch.c_str());
            gst_rtsp_media_factory_set_shared(f, TRUE);

            auto* ctx = new FactoryCtx{ &mgr, k, port, true };
            g_signal_connect_data(f, "media-configure",
                                  G_CALLBACK(media_configure_cb),
                                  ctx,
                                  (GClosureNotify)destroy_ctx,
                                  (GConnectFlags)0);

            gst_rtsp_mount_points_add_factory(mounts, path.c_str(), f);
            GSTD("[RTSP] mount %s -> udp_port=%d\n", path.c_str(), port);
        }
    }
	
}

}