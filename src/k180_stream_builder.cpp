#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <string>
#include <glib.h>

#include "k180_stream_builder.h"
#include "k180_constants.h"
#include "user_def_json.h"
#include "k180_h265_hub.h"      // H265HubCreateArgs / hub.create/start

using namespace k180;
using namespace k180::constants;

// 你原本的輸出尺寸選擇邏輯：用 resolution 決定 out_w/out_h
static inline void choose_out_size_for_1234(int resolution, int& out_w, int& out_h) {
    if (resolution == 720) {
        out_w = stream_out_w_1234_720;
        out_h = stream_out_h_1234_720;
    } else {
        out_w = stream_out_w_1234_1080;
        out_h = stream_out_h_1234_1080;
    }
}
static inline void choose_out_size_for_one(int resolution, int& out_w, int& out_h) {
    if (resolution == 720) {
        out_w = stream_out_w_one_720;
        out_h = stream_out_h_one_720;
    } else {
        out_w = stream_out_w_one_1080;
        out_h = stream_out_h_one_1080;
    }
}

// static inline std::string make_record_prefix(StreamGroup g, StreamView v) {
    // std::string p;
    // p += group_name(g);
    // p += "_";
    // p += std::to_string(view_number(v));
    // p += "_";              // 讓後面接時間
    // return p;              // e.g. "s1_1234_"
// }
static inline std::string make_record_prefix(StreamGroup g, StreamView v) {
    return std::string(group_name(g)) + "_ch" + std::to_string(view_number(v)) + "_";
}


static inline bool record_enabled_for(StreamGroup g, StreamView v, int recorded_mode) {
    if (g != StreamGroup::S1) return false;
    if (recorded_mode == 1) return (v == StreamView::V1234);
    if (recorded_mode == 2) return (v != StreamView::V1234); // s1_1..4
    return false;
}

static inline int bitrate_for(StreamView v, int datarate_bps) {
    return (v == StreamView::V1234) ? datarate_bps : (datarate_bps / 4);
}

// -------------------- UDP port probing --------------------
static bool probe_udp_port_loopback(int port)
{
    int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return false;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons((uint16_t)port);
    // probe 只測 loopback
    ::inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    bool ok = (::bind(fd, (sockaddr*)&addr, sizeof(addr)) == 0);
    ::close(fd);
    return ok;
}

static int pick_udp_port_for_sid(int base, int sid, int step, int max_tries)
{
    const int start = base + sid * step;
    for (int k = 0; k < max_tries; ++k) {
        const int p = start + k;
        if (probe_udp_port_loopback(p)) return p;
    }
    return -1;
}

static inline const char* tf(bool v) { return v ? "true" : "false"; }

static inline void dump_h264_hub_create_args(FILE* out, const H264HubCreateArgs& a,
                                             const char* prefix = "")
{
    if (!out) out = stderr;

    std::fprintf(out,
        "%sH264HubCreateArgs {\n"
        "%s  in_w=%d, in_h=%d\n"
        "%s  out_w=%d, out_h=%d\n"
        "%s  fps=%d\n"
        "%s  bitrate_bps=%d\n"
        "%s  udp_port=%d\n"
        "%s}\n",
        prefix,
        prefix, a.in_w, a.in_h,
        prefix, a.out_w, a.out_h,
        prefix, a.fps,
        prefix, a.bitrate_bps,
        prefix, a.udp_port,
        prefix
    );
}


static inline void dump_h265_hub_create_args(FILE* out, const H265HubCreateArgs& a,
                                             const char* prefix = "")
{
    if (!out) out = stderr;

    std::fprintf(out,
        "%sH265HubCreateArgs {\n"
        "%s  in_w=%d, in_h=%d\n"
        "%s  out_w=%d, out_h=%d\n"
        "%s  fps=%d\n"
        "%s  bitrate_bps=%d\n"
        "%s  udp_port=%d\n"
        "%s  enable_record=%s\n"
        "%s  record_mp4=%s\n"
        "%s  record_path='%s'\n"
        "%s}\n",
        prefix,
        prefix, a.in_w, a.in_h,
        prefix, a.out_w, a.out_h,
        prefix, a.fps,
        prefix, a.bitrate_bps,
        prefix, a.udp_port,
        prefix, tf(a.enable_record),
        prefix, tf(a.record_mp4),
        prefix, a.record_path.c_str(),
        prefix
    );
}

namespace k180::streambuilder {

bool parse_stream_path(const char* path, StreamKey* out)
{
    if (!path || !out) return false;

    // path expected like "/s1_1", "/s2_1234"
    // gst-rtsp-server gives mount path including leading '/'
    if (path[0] != '/') return false;

    // minimal parse
    // format: /s{1|2}_{1|2|3|4|1234}
    if (std::strncmp(path, "/s", 2) != 0) return false;
    char gch = path[2];
    if (path[3] != '_') return false;

    StreamGroup g;
    if (gch == '1') g = StreamGroup::S1;
    else if (gch == '2') g = StreamGroup::S2;
    else return false;

    const char* p = path + 4;
    if (*p == '\0') return false;

    // parse number
    int val = 0;
    for (const char* q = p; *q; ++q) {
        if (*q < '0' || *q > '9') return false;
        val = val * 10 + (*q - '0');
        if (val > 1234) break;
    }

    StreamView v;
    if (val == 1234) v = StreamView::V1234;
    else if (val >= 1 && val <= 4) v = (StreamView)val;
    else return false;

    out->g = g;
    out->v = v;
    return true;
}

bool build_and_start_all_hubs_from_cfg(HubManager& mgr, const UserConfig& cfggg, k180::osd::OsdShared* osd_shared)
{
    const int UDP_BASE      = 40000;
	const int UDP_BASE_H264 = 41000;
    const int UDP_STEP      = 10;
    const int UDP_MAX_TRIES = 32;

    auto get_scfg = [&](StreamGroup g) -> const auto& {
        return (g == StreamGroup::S1) ? cfggg.s1 : cfggg.s2;
    };

    bool ok_all = true;

    // 先挑 port
    for (int gi = 0; gi < kGroups; ++gi) {
        for (int vi = 0; vi < kViews; ++vi) {
            StreamKey k{ (StreamGroup)gi, (StreamView)vi };
            const int sid = stream_index(k);

            int p = pick_udp_port_for_sid(UDP_BASE, sid, UDP_STEP, UDP_MAX_TRIES);
            if (p < 0) {
                g_printerr("[HUB] ERROR: cannot find free UDP port for sid=%d (base=%d)\n", sid, UDP_BASE);
                mgr.udp_ports[sid] = 0;
                ok_all = false;
            } else {
                mgr.udp_ports[sid] = p;
            }
        }
    }

    // ---- pick ports for H264 (8 streams) ----
    for (int gi = 0; gi < kGroups; ++gi) {
        for (int vi = (int)StreamView::V1; vi <= (int)StreamView::V4; ++vi) {
            StreamKey k{ (StreamGroup)gi, (StreamView)vi };
            const int sid = h264_index(k.g, k.v);

            int p = pick_udp_port_for_sid(UDP_BASE_H264, sid, UDP_STEP, UDP_MAX_TRIES);
            if (p < 0) {
                g_printerr("[H264] ERROR: cannot find free UDP port for sid=%d (base=%d)\n", sid, UDP_BASE_H264);
                mgr.udp_ports_h264[sid] = 0;
                ok_all = false;
            } else {
                mgr.udp_ports_h264[sid] = p;
            }
        }
    }
	
	// mgr.set_record_mode(cfggg.recorded);

    // 建 hub
    for (int gi = 0; gi < kGroups; ++gi) {
        StreamGroup g = (StreamGroup)gi;
        const auto& scfg = get_scfg(g);

        for (int vi = 0; vi < kViews; ++vi) {
            StreamView v = (StreamView)vi;
            StreamKey k{ g, v };
            const int sid = stream_index(k);

            H265HubCreateArgs a;
a.osd_shared = osd_shared;
            if (v == StreamView::V1234) {
                a.in_w = 5800; a.in_h = 1000;
                choose_out_size_for_1234(scfg.resolution, a.out_w, a.out_h);
            } else {
                a.in_w = CAPTURE_IMG_WIDTH;
                a.in_h = CAPTURE_IMG_HEIGHT;
                choose_out_size_for_one(scfg.resolution, a.out_w, a.out_h);
            }

            a.fps         = scfg.fps;
            mgr.set_h265_fps(k, a.fps);
            a.bitrate_bps = bitrate_for(v, scfg.datarate_bps);

            a.udp_port = mgr.udp_ports[sid];
            if (a.udp_port == 0) {
                g_printerr("[HUB] WARN: sid=%d has no UDP port, skip\n", sid);
                ok_all = false;
                continue;
            }

            a.enable_record = record_enabled_for(g, v, cfggg.recorded);
			mgr.record_enabled[sid].store(a.enable_record, std::memory_order_relaxed);
            a.record_mp4    = true;
			a.record_path = a.enable_record ? RECORD_DIR : "";
			a.record_prefix = a.enable_record ? make_record_prefix(g, v) : "";
			dump_h265_hub_create_args(stderr, a, "  ");
            if (!mgr.hubs[sid].create(a) || !mgr.hubs[sid].start()) {
                g_printerr("[HUB] ERROR: create/start failed sid=%d\n", sid);
                ok_all = false;
                continue;
            }

            g_printerr("[HUB] started sid=%d %s_%d udp=%d record=%d\n",
                       sid, group_name(g), view_number(v), a.udp_port, (int)a.enable_record);
        }
    }

    // ---- build/start H264 hubs (s1/s2, V1..V4) ----
    for (int gi = 0; gi < kGroups; ++gi) {
        StreamGroup g = (StreamGroup)gi;
        const auto& scfg = get_scfg(g);

        for (int vi = (int)StreamView::V1; vi <= (int)StreamView::V4; ++vi) {
            StreamView v = (StreamView)vi;
            // StreamKey k{ g, v };
            const int sid = h264_index(g, v);

            k180::H264HubCreateArgs a;
            a.in_w = CAPTURE_IMG_WIDTH;
            a.in_h = CAPTURE_IMG_HEIGHT;
            choose_out_size_for_one(scfg.resolution, a.out_w, a.out_h);

            a.fps         = scfg.fps;
            mgr.set_h264_fps({g, v}, a.fps);
            a.bitrate_bps = bitrate_for(v, scfg.datarate_bps);
            a.udp_port    = mgr.udp_ports_h264[sid];

            if (a.udp_port == 0) {
                g_printerr("[H264] WARN: %s_%d has no UDP port, skip\n",
                           group_name(g), view_number(v));
                ok_all = false;
                continue;
            }
			dump_h264_hub_create_args(stderr, a, "  ");
            if (!mgr.hubs_h264[sid].create(a) || !mgr.hubs_h264[sid].start()) {
                g_printerr("[H264] ERROR: create/start failed %s_%d sid=%d\n",
                           group_name(g), view_number(v), sid);
                ok_all = false;
                continue;
            }

            g_printerr("[H264] started %s_%d udp=%d\n",
                       group_name(g), view_number(v), a.udp_port);
        }
    }


    return ok_all;
}


}
