// user_cfg.h
#pragma once
#include <cstdint>
#include <string>
#include <iosfwd>

struct StreamConfig {
    int resolution = 1080;         // 720 or 1080
    int width      = 1920;         // derived from resolution
    int height     = 1080;         // derived from resolution
    int fps        = 15;           // 5/10/15/20/25/30
    int datarate_bps = 2'000'000;  // bps (JSON "datarate" in Mbps)
};

struct BrightTunerCfg {
    float hysteresis_d_threshold = 4.0f;		// hysteresis 閾值（以 D = |da| 為準；若 D 很小就不動）
    int adjust_cooldown_default  = 6;			// 等幾個 loop（每 loop 約 33ms）
    int max_gain_step            = 50000;		// 單次 gain 最大改變量（依 hardware 調）
    int max_expo_step            = 2000;		// 單次 exposure 最大改變量
    int stable_ok_required       = 3;			// 連續幾幀為 OK 才真正回 ST_INIT
    float error_level_div        = 5.0f;
    float k_hi                   = 1.1f;
    float k_lo                   = 0.9f;
    float sat_ratio_hi			 = 0.35f;
    float sat_ratio_lo           = 0.35f;
    float mid_val                = 128;
};

struct UserConfig {
    // -------------------------
    // system_cfg
    // -------------------------
    std::string ipaddress;   // "192.168.50.91"
    int         netmask = 24; // CIDR, 0..32
    std::string gateway;     // "192.168.50.1"
    std::string dev_name;    // "fd-k180-01"

    // camera mapping tables from JSON
    int cam_num_r0[4]   = {0, 1, 2, 3};
    int cam_num_r180[4] = {3, 2, 1, 0};

    // active cam mapping derived by rotate180
    int cam_num[4] = {0, 1, 2, 3};

    // modes / flags
    int   recorded         = 0;
    int   objectdet        = 0;
    int   draw_det         = 0;
    int   draw_trk         = 0;
    int   track_fps        = 0;
    int   track_frame_rate = 0;
    int   rotate180        = 0;     // 0/1
    float blend_strength   = 0.04f;
    int   seam_mse_thresh  = 0;
    int   mask_cut         = 0;
    int   rec_fps_adj      = 0;
    int   seam_rate        = 0;

    // disk size
    std::int64_t rec_dick_size = 50'000'000'000LL; // JSON uses rec_dick_size

    // picture layout raw values,我想不起來這幾個要怎麼用，可能是要依照正反面，來給值，但我現在通通都用 pic_w_1234 / pic_h_1234 / pic_h_cut 在運作
    int pic_w_0_1234   = 5700;
    int pic_h_0_1234   = 920;
    int pic_h_0_cut    = 60;

    int pic_w_180_1234 = 5700;
    int pic_h_180_1234 = 920;
    int pic_h_180_cut  = 60;

    // derived active layout by rotate180
    int pic_w_1234 = 5700;
    int pic_h_1234 = 920;
    int pic_h_cut  = 60;

    // streams
    StreamConfig s1;
    StreamConfig s2;
	BrightTunerCfg bright_tuner;
};

enum class CfgStatus {
    OK = 0,
    FILE_OPEN_FAIL,
    FILE_READ_FAIL,
    JSON_PARSE_FAIL,
    JSON_SCHEMA_FAIL,
    VALIDATION_FAIL,
};

const char* cfg_status_str(CfgStatus s);

// defaults
void user_cfg_set_defaults(UserConfig& cfg);

// validate (strict)
CfgStatus user_cfg_validate(const UserConfig& cfg, std::string* err = nullptr);

// postprocess (rotate180 apply + normalize cam map to 0-based + active layout)
CfgStatus user_cfg_postprocess(UserConfig& cfg, std::string* err = nullptr);

// load from JSON (strict: missing ANY field => fail)
CfgStatus user_cfg_load_from_file(UserConfig& cfg, const char* json_path, std::string* err = nullptr);

// 將 cfg 印到任意 output stream（std::cout / std::cerr / 檔案）
void user_cfg_dump(const UserConfig& cfg, std::ostream& os, bool pretty = true);