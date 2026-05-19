// user_cfg_validate.cpp
#include "user_def_json.h"
#include <sstream>
#include <cmath>
#include <set>
#include <arpa/inet.h>

static bool is_one_of_int(int v, const int* arr, int n) {
    for (int i = 0; i < n; ++i) if (arr[i] == v) return true;
    return false;
}

static bool is_one_of_double(double v, const double* arr, int n, double eps = 1e-9) {
    for (int i = 0; i < n; ++i) if (std::fabs(arr[i] - v) < eps) return true;
    return false;
}

static bool is_valid_ipv4(const std::string& ip) {
    struct sockaddr_in sa;
    return inet_pton(AF_INET, ip.c_str(), &(sa.sin_addr)) == 1;
}

const char* cfg_status_str(CfgStatus s) {
    switch (s) {
        case CfgStatus::OK: return "OK";
        case CfgStatus::FILE_OPEN_FAIL: return "FILE_OPEN_FAIL";
        case CfgStatus::FILE_READ_FAIL: return "FILE_READ_FAIL";
        case CfgStatus::JSON_PARSE_FAIL: return "JSON_PARSE_FAIL";
        case CfgStatus::JSON_SCHEMA_FAIL: return "JSON_SCHEMA_FAIL";
        case CfgStatus::VALIDATION_FAIL: return "VALIDATION_FAIL";
        default: return "UNKNOWN";
    }
}

void user_cfg_set_defaults(UserConfig& cfg) {
    cfg = UserConfig{};
}

static CfgStatus validate_stream(const char* name, const StreamConfig& s, std::string* err) {
    static const int allowed_res[] = {720, 1080};
    static const int allowed_fps[] = {1, 5, 10, 15, 20, 30, 60};
    static const double allowed_datarate_mbps[] = {1, 1.6, 2, 3, 4, 6, 8, 10, 12, 16};

    if (!is_one_of_int(s.resolution, allowed_res, (int)(sizeof(allowed_res)/sizeof(allowed_res[0])))) {
        if (err) {
            std::ostringstream oss;
            oss << name << ".resolution invalid: " << s.resolution << " (allowed: 720,1080)";
            *err = oss.str();
        }
        return CfgStatus::VALIDATION_FAIL;
    }

    // width/height must match canonical resolution
    if (s.resolution == 1080 && (s.width != 1920 || s.height != 1080)) {
        if (err) {
            std::ostringstream oss;
            oss << name << ".width/height mismatch for 1080p: got "
                << s.width << "x" << s.height << " expected 1920x1080";
            *err = oss.str();
        }
        return CfgStatus::VALIDATION_FAIL;
    }
    if (s.resolution == 720 && (s.width != 1280 || s.height != 720)) {
        if (err) {
            std::ostringstream oss;
            oss << name << ".width/height mismatch for 720p: got "
                << s.width << "x" << s.height << " expected 1280x720";
            *err = oss.str();
        }
        return CfgStatus::VALIDATION_FAIL;
    }

    if (!is_one_of_int(s.fps, allowed_fps, (int)(sizeof(allowed_fps)/sizeof(allowed_fps[0])))) {
        if (err) {
            std::ostringstream oss;
            oss << name << ".fps invalid: " << s.fps << " (allowed: 5,10,15,20,25,30)";
            *err = oss.str();
        }
        return CfgStatus::VALIDATION_FAIL;
    }

    if (s.datarate_bps <= 0) {
        if (err) {
            std::ostringstream oss;
            oss << name << ".datarate_bps must be > 0, got " << s.datarate_bps;
            *err = oss.str();
        }
        return CfgStatus::VALIDATION_FAIL;
    }

    double mbps = (double)s.datarate_bps / 1e6;
    if (!is_one_of_double(mbps, allowed_datarate_mbps,
                          (int)(sizeof(allowed_datarate_mbps)/sizeof(allowed_datarate_mbps[0])),
                          1e-6)) {
        if (err) {
            std::ostringstream oss;
            oss << name << ".datarate invalid: " << mbps
                << " Mbps (allowed: 0.2,0.5,1,2,4,6,8)";
            *err = oss.str();
        }
        return CfgStatus::VALIDATION_FAIL;
    }

    return CfgStatus::OK;
}

static CfgStatus validate_cam_map(const char* name, const int a[4], std::string* err) {
    std::set<int> s;
    for (int i = 0; i < 4; ++i) s.insert(a[i]);

    // must be 4 unique values
    if ((int)s.size() != 4) {
        if (err) {
            std::ostringstream oss;
            oss << name << " must have 4 unique values, got {"
                << a[0] << "," << a[1] << "," << a[2] << "," << a[3] << "}";
            *err = oss.str();
        }
        return CfgStatus::VALIDATION_FAIL;
    }

    // must be exactly {0,1,2,3}
    if (!(s.count(0) && s.count(1) && s.count(2) && s.count(3))) {
        if (err) {
            std::ostringstream oss;
            oss << name << " must be a permutation of {0,1,2,3}, got {"
                << a[0] << "," << a[1] << "," << a[2] << "," << a[3] << "}";
            *err = oss.str();
        }
        return CfgStatus::VALIDATION_FAIL;
    }

    return CfgStatus::OK;
}

CfgStatus user_cfg_validate(const UserConfig& cfg, std::string* err) {
    // rotate180 {0,1}
    if (!(cfg.rotate180 == 0 || cfg.rotate180 == 1)) {
        if (err) *err = "system_cfg.rotate180 must be 0 or 1";
        return CfgStatus::VALIDATION_FAIL;
    }

    // netmask [0..32]
    if (cfg.netmask < 0 || cfg.netmask > 32) {
        if (err) *err = "system_cfg.netmask must be in [0..32]";
        return CfgStatus::VALIDATION_FAIL;
    }

    // recorded {0,1,2}
    {
        static const int allowed[] = {0, 1, 2};
        if (!is_one_of_int(cfg.recorded, allowed, 3)) {
            if (err) *err = "system_cfg.recorded must be one of {0,1,2}";
            return CfgStatus::VALIDATION_FAIL;
        }
    }

    // objectdet {0,1}
    {
        static const int allowed[] = {0, 1, 2};
        if (!is_one_of_int(cfg.objectdet, allowed, 3)) {
            if (err) *err = "system_cfg.objectdet must be one of {0,1}";
            return CfgStatus::VALIDATION_FAIL;
        }
    }

    // rec_dick_size > 0
    if (cfg.rec_dick_size <= 0) {
        if (err) *err = "system_cfg.rec_dick_size must be > 0";
        return CfgStatus::VALIDATION_FAIL;
    }

    // ipaddress must be valid IPv4
    if (!is_valid_ipv4(cfg.ipaddress)) {
        if (err) *err = "system_cfg.ipaddress is not a valid IPv4 address";
        return CfgStatus::VALIDATION_FAIL;
    }

    // gateway also validate (practical)
    if (!is_valid_ipv4(cfg.gateway)) {
        if (err) *err = "system_cfg.gateway is not a valid IPv4 address";
        return CfgStatus::VALIDATION_FAIL;
    }

    // cam maps
    {
        std::string e;
        auto st = validate_cam_map("system_cfg.cam_num_*_r0", cfg.cam_num_r0, &e);
        if (st != CfgStatus::OK) { if (err) *err = e; return st; }
    }
    {
        std::string e;
        auto st = validate_cam_map("system_cfg.cam_num_*_r180", cfg.cam_num_r180, &e);
        if (st != CfgStatus::OK) { if (err) *err = e; return st; }
    }

    // streams
    {
        std::string e;
        auto st = validate_stream("stream_1", cfg.s1, &e);
        if (st != CfgStatus::OK) { if (err) *err = e; return st; }
    }
    {
        std::string e;
        auto st = validate_stream("stream_2", cfg.s2, &e);
        if (st != CfgStatus::OK) { if (err) *err = e; return st; }
    }

	// bright_tuner validation
	if (cfg.bright_tuner.hysteresis_d_threshold < 0.0f || cfg.bright_tuner.hysteresis_d_threshold > 20.0f) {
		if (err) *err = "bright_tuner.hysteresis_d_threshold out of range";
		return CfgStatus::VALIDATION_FAIL;
	}

	if (cfg.bright_tuner.adjust_cooldown_default < 0 || cfg.bright_tuner.adjust_cooldown_default > 20) {
		if (err) *err = "bright_tuner.adjust_cooldown_default out of range";
		return CfgStatus::VALIDATION_FAIL;
	}

	if (cfg.bright_tuner.max_gain_step <= 0 || cfg.bright_tuner.max_gain_step > 200000) {
		if (err) *err = "bright_tuner.max_gain_step out of range";
		return CfgStatus::VALIDATION_FAIL;
	}

	if (cfg.bright_tuner.max_expo_step <= 0 || cfg.bright_tuner.max_expo_step > 5000) {
		if (err) *err = "bright_tuner.max_expo_step out of range";
		return CfgStatus::VALIDATION_FAIL;
	}

	if (cfg.bright_tuner.stable_ok_required <= 0 || cfg.bright_tuner.stable_ok_required > 20) {
		if (err) *err = "bright_tuner.stable_ok_required out of range";
		return CfgStatus::VALIDATION_FAIL;
	}

	if (cfg.bright_tuner.error_level_div <= 0.0f || cfg.bright_tuner.error_level_div > 20.0f) {
		if (err) *err = "bright_tuner.error_level_div must be > 0";
		return CfgStatus::VALIDATION_FAIL;
	}

	if (cfg.bright_tuner.k_hi <= 0.0f || cfg.bright_tuner.k_hi > 2.0f) {
		if (err) *err = "bright_tuner.k_hi must be 0 ~ 2";
		return CfgStatus::VALIDATION_FAIL;
	}

	if (cfg.bright_tuner.k_lo <= 0.0f || cfg.bright_tuner.k_lo > 2.0f) {
		if (err) *err = "bright_tuner.k_lo must be 0 ~ 2";
		return CfgStatus::VALIDATION_FAIL;
	}
	
	if (cfg.bright_tuner.sat_ratio_hi <= 0.1f || cfg.bright_tuner.sat_ratio_hi > 0.8f) {
		if (err) *err = "bright_tuner.sat_ratio_hi must be 0.1 ~ 0.8";
		return CfgStatus::VALIDATION_FAIL;
	}

	if (cfg.bright_tuner.sat_ratio_lo <= 0.1f || cfg.bright_tuner.sat_ratio_lo > 0.8f) {
		if (err) *err = "bright_tuner.sat_ratio_lo must be 0.1 ~ 0.8";
		return CfgStatus::VALIDATION_FAIL;
	}

	if (cfg.bright_tuner.mid_val <= 1.0f || cfg.bright_tuner.mid_val > 250.0f) {
		if (err) *err = "bright_tuner.mid_val must be 1 ~ 250";
		return CfgStatus::VALIDATION_FAIL;
	}
	
    return CfgStatus::OK;
}

