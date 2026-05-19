// k180_rtsp_attach.cpp

#include "k180_rtsp_attach.h"      // 你自己的宣告（attach_factories 等）
#include "k180_h265_hub.h"         // 會用到 HubManager / StreamKey / viewer_inc/dec（或你自己的介面）
#include "k180_gst_dbg.h"
#include "k180_stream_key.h"
#include "k180_stream_builder.h"

#include <gst/gst.h>
#include <gst/rtsp-server/rtsp-server.h>   // GstRTSPServer / GstRTSPMountPoints / GstRTSPMediaFactory / GstRTSPMedia
#include <gst/app/gstappsrc.h>


#include <glib.h>                  // gpointer / g_free / g_strdup / g_print / g_printerr（若你有用）
#include <algorithm>
#include <cstring>
#include <mutex>                   // 若你在 callback 裡用 std::mutex / lock_guard
#include <atomic>                  // viewer 計數用 atomic
#include <string>                  // std::string（若你用）
#include <unordered_map>           // 若你有 path->StreamKey mapping
#include <vector>

namespace k180 {
// =========================
// RTSP glue (factory callbacks)
// =========================
struct FactoryCtx {
    HubManager* mgr;
    StreamKey   key;
    int         udp_port;
	bool        is_h264;
    std::atomic<bool> bootstrap_active{false};
};

struct ActiveRtspStream {
    StreamKey key;
    bool is_h264 = false;
    int id = -1;
};

struct ClientTrackState {
    HubManager* mgr = nullptr;
    std::mutex mu;
    std::vector<ActiveRtspStream> active;
};

static constexpr const char* kClientTrackStateKey = "k180-client-track-state";
static std::mutex g_factory_ctx_mu;
static std::unordered_map<int, FactoryCtx*> g_factory_ctx_by_id;

static int active_stream_id(StreamKey key, bool is_h264)
{
    return is_h264 ? (1000 + h264_index(key.g, key.v)) : stream_index(key);
}

static bool ends_with(const std::string& s, const char* suffix)
{
    const size_t n = std::strlen(suffix);
    return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

static bool parse_active_stream_path(const char* raw_path, ActiveRtspStream* out)
{
    if (!raw_path || !out) return false;

    std::string path(raw_path);
    if (path.empty()) return false;

    // Some RTSP requests can carry a control sub-path. Keep only the mount.
    const size_t second_slash = path.find('/', 1);
    if (second_slash != std::string::npos) {
        path.resize(second_slash);
    }

    bool is_h264 = false;
    if (ends_with(path, "_h264")) {
        is_h264 = true;
        path.resize(path.size() - std::strlen("_h264"));
    }

    StreamKey key{};
    if (!k180::streambuilder::parse_stream_path(path.c_str(), &key)) {
        return false;
    }

    if (is_h264 &&
        (static_cast<int>(key.v) < static_cast<int>(StreamView::V1) ||
         static_cast<int>(key.v) > static_cast<int>(StreamView::V4))) {
        return false;
    }

    out->key = key;
    out->is_h264 = is_h264;
    out->id = active_stream_id(key, is_h264);
    return true;
}

static void viewer_inc_for_stream(HubManager* mgr, const ActiveRtspStream& s)
{
    if (!mgr) return;
    if (s.is_h264) {
        mgr->viewer_inc_h264(s.key);
        mgr->request_idr_h264(s.key);
    } else {
        mgr->viewer_inc(s.key);
        mgr->request_idr_h265(s.key);
    }
}

static void viewer_dec_for_stream(HubManager* mgr, const ActiveRtspStream& s)
{
    if (!mgr) return;
    if (s.is_h264) mgr->viewer_dec_h264(s.key);
    else           mgr->viewer_dec(s.key);
}

static ActiveRtspStream active_stream_from_factory(const FactoryCtx& ctx)
{
    ActiveRtspStream stream{};
    stream.key = ctx.key;
    stream.is_h264 = ctx.is_h264;
    stream.id = active_stream_id(stream.key, stream.is_h264);
    return stream;
}

static void register_factory_ctx(FactoryCtx* ctx)
{
    if (!ctx) return;

    const ActiveRtspStream stream = active_stream_from_factory(*ctx);
    std::lock_guard<std::mutex> lock(g_factory_ctx_mu);
    g_factory_ctx_by_id[stream.id] = ctx;
}

static void release_factory_bootstrap(FactoryCtx* ctx)
{
    if (!ctx || !ctx->mgr) return;
    if (!ctx->bootstrap_active.exchange(false, std::memory_order_acq_rel)) {
        return;
    }

    const ActiveRtspStream stream = active_stream_from_factory(*ctx);
    viewer_dec_for_stream(ctx->mgr, stream);
}

static void consume_bootstrap_for_stream(HubManager* mgr, const ActiveRtspStream& stream)
{
    (void)mgr;

    FactoryCtx* ctx = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_factory_ctx_mu);
        const auto it = g_factory_ctx_by_id.find(stream.id);
        if (it != g_factory_ctx_by_id.end()) {
            ctx = it->second;
        }
    }

    release_factory_bootstrap(ctx);
}

static void release_all_streams_locked(ClientTrackState* state)
{
    if (!state) return;

    for (const auto& s : state->active) {
        viewer_dec_for_stream(state->mgr, s);
    }
    state->active.clear();
}

static void destroy_client_track_state(gpointer data)
{
    auto* state = static_cast<ClientTrackState*>(data);
    if (!state) return;

    {
        std::lock_guard<std::mutex> lock(state->mu);
        release_all_streams_locked(state);
    }
    delete state;
}

static ClientTrackState* get_client_track_state(GstRTSPClient* client, HubManager* mgr)
{
    if (!client) return nullptr;

    auto* state = static_cast<ClientTrackState*>(
        g_object_get_data(G_OBJECT(client), kClientTrackStateKey));
    if (!state && mgr) {
        state = new ClientTrackState;
        state->mgr = mgr;
        g_object_set_data_full(G_OBJECT(client), kClientTrackStateKey,
                               state, destroy_client_track_state);
    }
    return state;
}

static void start_client_stream(GstRTSPClient* client, GstRTSPContext* rtsp_ctx, HubManager* mgr)
{
    if (!rtsp_ctx || !rtsp_ctx->uri) return;

    ActiveRtspStream stream{};
    if (!parse_active_stream_path(rtsp_ctx->uri->abspath, &stream)) {
        return;
    }

    ClientTrackState* state = get_client_track_state(client, mgr);
    if (!state) return;

    std::lock_guard<std::mutex> lock(state->mu);
    const auto it = std::find_if(
        state->active.begin(), state->active.end(),
        [&](const ActiveRtspStream& s) { return s.id == stream.id; });
    if (it != state->active.end()) {
        consume_bootstrap_for_stream(state->mgr, stream);
        return;
    }

    state->active.push_back(stream);
    viewer_inc_for_stream(state->mgr, stream);
    consume_bootstrap_for_stream(state->mgr, stream);
}

static void stop_client_stream(GstRTSPClient* client, GstRTSPContext* rtsp_ctx)
{
    ClientTrackState* state = get_client_track_state(client, nullptr);
    if (!state) return;

    ActiveRtspStream stream{};
    const bool has_path =
        rtsp_ctx && rtsp_ctx->uri &&
        parse_active_stream_path(rtsp_ctx->uri->abspath, &stream);

    std::lock_guard<std::mutex> lock(state->mu);
    if (!has_path) {
        release_all_streams_locked(state);
        return;
    }

    const auto it = std::find_if(
        state->active.begin(), state->active.end(),
        [&](const ActiveRtspStream& s) { return s.id == stream.id; });
    if (it == state->active.end()) {
        return;
    }

    const ActiveRtspStream removed = *it;
    state->active.erase(it);
    viewer_dec_for_stream(state->mgr, removed);
}

static void on_client_play_request(GstRTSPClient* client, GstRTSPContext* ctx, gpointer user_data)
{
    start_client_stream(client, ctx, static_cast<HubManager*>(user_data));
}

static void on_client_describe_request(GstRTSPClient* client, GstRTSPContext* ctx, gpointer user_data)
{
    start_client_stream(client, ctx, static_cast<HubManager*>(user_data));
}

static void on_client_setup_request(GstRTSPClient* client, GstRTSPContext* ctx, gpointer user_data)
{
    start_client_stream(client, ctx, static_cast<HubManager*>(user_data));
}

static void on_client_pause_request(GstRTSPClient* client, GstRTSPContext* ctx, gpointer)
{
    stop_client_stream(client, ctx);
}

static void on_client_teardown_request(GstRTSPClient* client, GstRTSPContext* ctx, gpointer)
{
    stop_client_stream(client, ctx);
}

static void on_client_closed(GstRTSPClient* client, gpointer)
{
    ClientTrackState* state = get_client_track_state(client, nullptr);
    if (!state) return;

    std::lock_guard<std::mutex> lock(state->mu);
    release_all_streams_locked(state);
}

static void on_client_connected(GstRTSPServer*, GstRTSPClient* client, gpointer user_data)
{
    auto* mgr = static_cast<HubManager*>(user_data);
    get_client_track_state(client, mgr);

    g_signal_connect(client, "describe-request",
                     G_CALLBACK(on_client_describe_request), mgr);
    g_signal_connect(client, "setup-request",
                     G_CALLBACK(on_client_setup_request), mgr);
    g_signal_connect(client, "play-request",
                     G_CALLBACK(on_client_play_request), mgr);
    g_signal_connect(client, "pause-request",
                     G_CALLBACK(on_client_pause_request), nullptr);
    g_signal_connect(client, "teardown-request",
                     G_CALLBACK(on_client_teardown_request), nullptr);
    g_signal_connect(client, "closed",
                     G_CALLBACK(on_client_closed), nullptr);
}

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
    release_factory_bootstrap(ctx);
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

    if (!ctx->bootstrap_active.exchange(true, std::memory_order_acq_rel)) {
        const ActiveRtspStream stream = active_stream_from_factory(*ctx);
        viewer_inc_for_stream(ctx->mgr, stream);
    }

    // Shared factories can reuse one GstRTSPMedia across clients, so viewer
    // accounting is handled from client request/closed signals instead.
    g_signal_connect(media, "prepared",   G_CALLBACK(on_media_prepared),   ctx);
    g_signal_connect(media, "unprepared", G_CALLBACK(on_media_unprepared), ctx);
}

static void destroy_ctx(gpointer p)
{
    auto* ctx = static_cast<FactoryCtx*>(p);
    if (ctx) {
        const ActiveRtspStream stream = active_stream_from_factory(*ctx);
        {
            std::lock_guard<std::mutex> lock(g_factory_ctx_mu);
            const auto it = g_factory_ctx_by_id.find(stream.id);
            if (it != g_factory_ctx_by_id.end() && it->second == ctx) {
                g_factory_ctx_by_id.erase(it);
            }
        }
        release_factory_bootstrap(ctx);
    }
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
        "! rtph264pay name=pay0 pt=96 mtu=1470 config-interval=1 "
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
        "! queue name=q_udpin leaky=downstream max-size-buffers=2 max-size-bytes=0 max-size-time=0 "
        "! h265parse config-interval=1 "
        "! rtph265pay name=pay0 pt=96 mtu=1470 config-interval=1 "
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

static void configure_live_factory(GstRTSPMediaFactory* f, const std::string& launch)
{
    gst_rtsp_media_factory_set_launch(f, launch.c_str());
    gst_rtsp_media_factory_set_shared(f, TRUE);
    // Work around clients that connect and leave S1 frozen on old frames.
    // The failing runs showed GStreamer sticky-event ordering warnings on the
    // RTCP receive path, e.g. recv_rtcp_sink/rtpbin got 'segment' before 'caps'.
    gst_rtsp_media_factory_set_suspend_mode(f, GST_RTSP_SUSPEND_MODE_NONE);
    gst_rtsp_media_factory_set_eos_shutdown(f, FALSE);
#if GST_CHECK_VERSION(1,20,0)
    gst_rtsp_media_factory_set_enable_rtcp(f, FALSE);
#endif
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
            configure_live_factory(f, launch);

            auto* ctx = new FactoryCtx{ &mgr, k, port };
            register_factory_ctx(ctx);
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
            configure_live_factory(f, launch);

            auto* ctx = new FactoryCtx{ &mgr, k, port, true };
            register_factory_ctx(ctx);
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

void attach_rtsp_client_tracking(GstRTSPServer* server, HubManager& mgr)
{
    if (!server) return;
    g_signal_connect(server, "client-connected",
                     G_CALLBACK(on_client_connected), &mgr);
}

}
