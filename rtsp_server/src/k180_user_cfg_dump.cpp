
#include "user_def_json.h"
#include <iostream>
#include <iomanip>

static void dump_cam_map(const char* name, const int a[4], std::ostream& os) {
    os << name << "=[" << a[0] << "," << a[1] << "," << a[2] << "," << a[3] << "]";
}

static void dump_stream(const char* name, const StreamConfig& s, std::ostream& os) {
    os << name << " { "
       << "resolution=" << s.resolution
       << " size=" << s.width << "x" << s.height
       << " fps=" << s.fps
       << " datarate=" << std::fixed << std::setprecision(3)
       << (double)s.datarate_bps / 1e6 << " Mbps"
       << " (" << s.datarate_bps << " bps)"
       << " }";
}

void user_cfg_dump(const UserConfig& c, std::ostream& os, bool pretty) {
    if (!pretty) {
        os << "dev_name=" << c.dev_name
           << " ipaddress=" << c.ipaddress << "/" << c.netmask
           << " gateway=" << c.gateway
           << " rotate180=" << c.rotate180
           << " recorded=" << c.recorded
           << " objectdet=" << c.objectdet
           << " draw_det=" << c.draw_det
           << " draw_trk=" << c.draw_trk
           << " track_fps=" << c.track_fps
           << " track_frame_rate=" << c.track_frame_rate
           << " blend_strength=" << c.blend_strength
           << " seam_mse_thresh=" << c.seam_mse_thresh
           << " mask_cut=" << c.mask_cut
           << " rec_fps_adj=" << c.rec_fps_adj
           << " seam_rate=" << c.seam_rate
           << " rec_dick_size=" << c.rec_dick_size
           << " ";
        dump_cam_map("cam_num_r0", c.cam_num_r0, os); os << " ";
        dump_cam_map("cam_num_r180", c.cam_num_r180, os); os << " ";
        dump_cam_map("cam_num_active", c.cam_num, os); os << " ";
        os << "pic_active={w=" << c.pic_w_1234 << ",h=" << c.pic_h_1234 << ",cut=" << c.pic_h_cut << "} ";
        dump_stream("stream_1", c.s1, os); os << " ";
        dump_stream("stream_2", c.s2, os);
        os << "\n";
        return;
    }

    os << "================ UserConfig ================\n";
    os << "[system_cfg]\n";
    os << "  dev_name          : " << c.dev_name << "\n";
    os << "  ipaddress/netmask : " << c.ipaddress << "/" << c.netmask << "\n";
    os << "  gateway           : " << c.gateway << "\n";
    os << "  rotate180         : " << c.rotate180 << "\n";
    os << "  recorded          : " << c.recorded << "\n";
    os << "  objectdet         : " << c.objectdet << "\n";
    os << "  draw_det          : " << c.draw_det << "\n";
    os << "  draw_trk          : " << c.draw_trk << "\n";
    os << "  track_fps         : " << c.track_fps << "\n";
    os << "  track_frame_rate  : " << c.track_frame_rate << "\n";
    os << "  blend_strength    : " << std::fixed << std::setprecision(4) << c.blend_strength << "\n";
    os << "  seam_mse_thresh   : " << c.seam_mse_thresh << "\n";
    os << "  mask_cut          : " << c.mask_cut << "\n";
    os << "  rec_fps_adj       : " << c.rec_fps_adj << "\n";
    os << "  seam_rate         : " << c.seam_rate << "\n";
    os << "  rec_dick_size     : " << c.rec_dick_size << "\n";

    os << "\n  camera map\n";
    os << "    "; dump_cam_map("cam_num_r0     ", c.cam_num_r0, os); os << "\n";
    os << "    "; dump_cam_map("cam_num_r180   ", c.cam_num_r180, os); os << "\n";
    os << "    "; dump_cam_map("cam_num_active ", c.cam_num, os); os << "\n";

    os << "\n  picture layout\n";
    os << "    pic_0   : w=" << c.pic_w_0_1234   << " h=" << c.pic_h_0_1234   << " cut=" << c.pic_h_0_cut   << "\n";
    os << "    pic_180 : w=" << c.pic_w_180_1234 << " h=" << c.pic_h_180_1234 << " cut=" << c.pic_h_180_cut << "\n";
    os << "    active  : w=" << c.pic_w_1234     << " h=" << c.pic_h_1234     << " cut=" << c.pic_h_cut     << "\n";

    os << "\n[streams]\n  ";
    dump_stream("stream_1", c.s1, os); os << "\n  ";
    dump_stream("stream_2", c.s2, os); os << "\n";

	os << "\n[bright_tuner]\n";
	os << "  hysteresis_d_threshold : " << c.bright_tuner.hysteresis_d_threshold << "\n";
	os << "  adjust_cooldown_default: " << c.bright_tuner.adjust_cooldown_default << "\n";
	os << "  max_gain_step          : " << c.bright_tuner.max_gain_step << "\n";
	os << "  max_expo_step          : " << c.bright_tuner.max_expo_step << "\n";
	os << "  stable_ok_required     : " << c.bright_tuner.stable_ok_required << "\n";
	os << "  error_level_div        : " << c.bright_tuner.error_level_div << "\n";
	os << "  k_hi                   : " << c.bright_tuner.k_hi << "\n";
	os << "  k_lo                   : " << c.bright_tuner.k_lo << "\n";
	os << "  sat_ratio_hi           : " << c.bright_tuner.sat_ratio_hi << "\n";
	os << "  sat_ratio_lo           : " << c.bright_tuner.sat_ratio_lo << "\n";
	os << "  mid_val                : " << c.bright_tuner.mid_val << "\n";

    os << "===========================================\n";
}

