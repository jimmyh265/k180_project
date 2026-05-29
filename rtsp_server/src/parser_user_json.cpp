// parser_user_json.cpp
#include "user_def_json.h"
#include <json-c/json.h>

#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

#include <string>
#include <sstream>
#include <cmath>

static bool read_file_locked_ex(const char* path, std::string& out, std::string* err) {
    out.clear();

    int fd = ::open(path, O_RDONLY);
    if (fd < 0) {
        if (err) *err = std::string("open() failed: ") + path;
        return false;
    }

    // match your original behavior: exclusive lock
    if (::flock(fd, LOCK_SH) != 0) {
        if (err) *err = "flock(LOCK_SH) failed";
        ::close(fd);
        return false;
    }

    std::string buf;
    buf.resize(64 * 1024);

    std::string content;
    for (;;) {
        ssize_t n = ::read(fd, buf.data(), buf.size());
        if (n < 0) {
            if (err) *err = "read() failed";
            ::flock(fd, LOCK_UN);
            ::close(fd);
            return false;
        }
        if (n == 0) break;
        content.append(buf.data(), (size_t)n);
    }

    ::flock(fd, LOCK_UN);
    ::close(fd);

    out.swap(content);
    return true;
}

static json_object* obj_get(json_object* parent, const char* key) {
    if (!parent || !json_object_is_type(parent, json_type_object)) return nullptr;
    json_object* v = nullptr;
    if (!json_object_object_get_ex(parent, key, &v)) return nullptr;
    return v;
}

// ---------- strict require helpers ----------
static bool require_object(json_object* parent, const char* key, json_object** out, std::string* err) {
    *out = obj_get(parent, key);
    if (!*out || !json_object_is_type(*out, json_type_object)) {
        if (err) {
            std::ostringstream oss;
            oss << "missing or invalid object: " << key;
            *err = oss.str();
        }
        return false;
    }
    return true;
}

static bool require_string(json_object* parent, const char* key, std::string& out, std::string* err) {
    json_object* v = obj_get(parent, key);
    if (!v || !json_object_is_type(v, json_type_string)) {
        if (err) {
            std::ostringstream oss;
            oss << "missing or invalid string: " << key;
            *err = oss.str();
        }
        return false;
    }
    out = json_object_get_string(v);
    return true;
}

static bool require_int(json_object* parent, const char* key, int& out, std::string* err) {
    json_object* v = obj_get(parent, key);
    if (!v) {
        if (err) *err = std::string("missing int: ") + key;
        return false;
    }
    if (json_object_is_type(v, json_type_int)) {
        out = json_object_get_int(v);
        return true;
    }
    // strict：不接受 double 代替 int（你要更硬就這樣）
    if (err) {
        std::ostringstream oss;
        oss << "invalid type (expect int): " << key;
        *err = oss.str();
    }
    return false;
}

static bool require_int64(json_object* parent, const char* key, std::int64_t& out, std::string* err) {
    json_object* v = obj_get(parent, key);
    if (!v || !json_object_is_type(v, json_type_int)) {
        if (err) {
            std::ostringstream oss;
            oss << "missing or invalid int64(int): " << key;
            *err = oss.str();
        }
        return false;
    }
    out = (std::int64_t)json_object_get_int64(v);
    return true;
}

static bool require_float(json_object* parent, const char* key, float& out, std::string* err) {
    json_object* v = obj_get(parent, key);
    if (!v) {
        if (err) *err = std::string("missing number: ") + key;
        return false;
    }
    if (json_object_is_type(v, json_type_double) || json_object_is_type(v, json_type_int)) {
        out = (float)json_object_get_double(v);
        return true;
    }
    if (err) {
        std::ostringstream oss;
        oss << "invalid type (expect number): " << key;
        *err = oss.str();
    }
    return false;
}

// JSON "datarate" is Mbps and may be int or double (e.g., 4 or 0.2)
static bool require_datarate_bps_from_mbps(json_object* parent, const char* key, int& out_bps, std::string* err) {
    json_object* v = obj_get(parent, key);
    if (!v) {
        if (err) *err = std::string("missing datarate: ") + key;
        return false;
    }

    if (!(json_object_is_type(v, json_type_int) || json_object_is_type(v, json_type_double))) {
        if (err) {
            std::ostringstream oss;
            oss << "invalid type (expect number): " << key;
            *err = oss.str();
        }
        return false;
    }

    double mbps = json_object_get_double(v);
    if (mbps <= 0.0) {
        if (err) {
            std::ostringstream oss;
            oss << "invalid datarate (must be >0): " << key;
            *err = oss.str();
        }
        return false;
    }

    out_bps = (int)std::llround(mbps * 1e6);
    return true;
}

static void canonicalize_stream(StreamConfig& s) {
    if (s.resolution == 1080) { s.width = 1920; s.height = 1080; }
    if (s.resolution == 720)  { s.width = 1280; s.height = 720;  }
}

static bool parse_stream_strict(json_object* jstream, StreamConfig& s, const char* stream_name, std::string* err) {
    if (!jstream || !json_object_is_type(jstream, json_type_object)) {
        if (err) *err = std::string("missing object: ") + stream_name;
        return false;
    }

    if (!require_int(jstream, "resolution", s.resolution, err)) return false;
    if (!require_int(jstream, "fps", s.fps, err)) return false;
    if (!require_datarate_bps_from_mbps(jstream, "datarate", s.datarate_bps, err)) return false;

    canonicalize_stream(s);
    return true;
}

CfgStatus user_cfg_load_from_file(UserConfig& cfg, const char* json_path, std::string* err) {
    user_cfg_set_defaults(cfg);

    std::string content;
    if (!read_file_locked_ex(json_path, content, err)) {
        return CfgStatus::FILE_READ_FAIL;
    }

    json_object* root = json_tokener_parse(content.c_str());
    if (!root) {
        if (err) *err = "json_tokener_parse() failed";
        return CfgStatus::JSON_PARSE_FAIL;
    }

    std::string e;
    json_object* jsys = nullptr;
    json_object* js1  = nullptr;
    json_object* js2  = nullptr;
	json_object* jbt  = nullptr;

    if (!require_object(root, "system_cfg", &jsys, &e)) {
        json_object_put(root);
        if (err) *err = e;
        return CfgStatus::JSON_SCHEMA_FAIL;
    }
    if (!require_object(root, "stream_1", &js1, &e)) {
        json_object_put(root);
        if (err) *err = e;
        return CfgStatus::JSON_SCHEMA_FAIL;
    }
    if (!require_object(root, "stream_2", &js2, &e)) {
        json_object_put(root);
        if (err) *err = e;
        return CfgStatus::JSON_SCHEMA_FAIL;
    }
	if (!require_object(root, "bright_tuner", &jbt, &e)) {
		json_object_put(root);
		if (err) *err = e;
		return CfgStatus::JSON_SCHEMA_FAIL;
	}
    // ---- system_cfg: STRICT require EVERY field you listed ----
    if (!require_string(jsys, "ipaddress", cfg.ipaddress, &e)) goto schema_fail;
    if (!require_int(jsys, "netmask", cfg.netmask, &e)) goto schema_fail;
    if (!require_string(jsys, "gateway", cfg.gateway, &e)) goto schema_fail;

    if (!require_int(jsys, "cam_num_1_r0",   cfg.cam_num_r0[0], &e)) goto schema_fail;
    if (!require_int(jsys, "cam_num_2_r0",   cfg.cam_num_r0[1], &e)) goto schema_fail;
    if (!require_int(jsys, "cam_num_3_r0",   cfg.cam_num_r0[2], &e)) goto schema_fail;
    if (!require_int(jsys, "cam_num_4_r0",   cfg.cam_num_r0[3], &e)) goto schema_fail;

    if (!require_int(jsys, "cam_num_1_r180", cfg.cam_num_r180[0], &e)) goto schema_fail;
    if (!require_int(jsys, "cam_num_2_r180", cfg.cam_num_r180[1], &e)) goto schema_fail;
    if (!require_int(jsys, "cam_num_3_r180", cfg.cam_num_r180[2], &e)) goto schema_fail;
    if (!require_int(jsys, "cam_num_4_r180", cfg.cam_num_r180[3], &e)) goto schema_fail;

    if (!require_int(jsys, "recorded", cfg.recorded, &e)) goto schema_fail;
    if (!require_int(jsys, "objectdet", cfg.objectdet, &e)) goto schema_fail;
    if (!require_int(jsys, "draw_det", cfg.draw_det, &e)) goto schema_fail;
    if (!require_int(jsys, "draw_trk", cfg.draw_trk, &e)) goto schema_fail;
    if (!require_int(jsys, "track_fps", cfg.track_fps, &e)) goto schema_fail;
    if (!require_int(jsys, "track_frame_rate", cfg.track_frame_rate, &e)) goto schema_fail;
    if (!require_int(jsys, "rotate180", cfg.rotate180, &e)) goto schema_fail;
    if (!require_float(jsys, "blend_strength", cfg.blend_strength, &e)) goto schema_fail;
    if (!require_int(jsys, "seam_mse_thresh", cfg.seam_mse_thresh, &e)) goto schema_fail;
    if (!require_int(jsys, "mask_cut", cfg.mask_cut, &e)) goto schema_fail;
    if (!require_int(jsys, "rec_fps_adj", cfg.rec_fps_adj, &e)) goto schema_fail;
    if (!require_int(jsys, "seam_rate", cfg.seam_rate, &e)) goto schema_fail;
    if (!require_int64(jsys, "rec_dick_size", cfg.rec_dick_size, &e)) goto schema_fail;

    if (!require_int(jsys, "pic_w_0_1234", cfg.pic_w_0_1234, &e)) goto schema_fail;
    if (!require_int(jsys, "pic_h_0_1234", cfg.pic_h_0_1234, &e)) goto schema_fail;
    if (!require_int(jsys, "pic_h_0_cut",  cfg.pic_h_0_cut,  &e)) goto schema_fail;

    if (!require_int(jsys, "pic_w_180_1234", cfg.pic_w_180_1234, &e)) goto schema_fail;
    if (!require_int(jsys, "pic_h_180_1234", cfg.pic_h_180_1234, &e)) goto schema_fail;
    if (!require_int(jsys, "pic_h_180_cut",  cfg.pic_h_180_cut,  &e)) goto schema_fail;

    if (!require_string(jsys, "dev_name", cfg.dev_name, &e)) goto schema_fail;

    // ---- streams strict ----
    if (!parse_stream_strict(js1, cfg.s1, "stream_1", &e)) goto schema_fail;
    if (!parse_stream_strict(js2, cfg.s2, "stream_2", &e)) goto schema_fail;

	// ---- bright_tuner: STRICT require EVERY field ----
	if (!require_float(jbt, "hysteresis_d_threshold", cfg.bright_tuner.hysteresis_d_threshold, &e)) goto schema_fail;
	if (!require_int  (jbt, "adjust_cooldown_default", cfg.bright_tuner.adjust_cooldown_default,  &e)) goto schema_fail;
	if (!require_int  (jbt, "max_gain_step",           cfg.bright_tuner.max_gain_step,            &e)) goto schema_fail;
	if (!require_int  (jbt, "max_expo_step",           cfg.bright_tuner.max_expo_step,            &e)) goto schema_fail;
	if (!require_int  (jbt, "stable_ok_required",      cfg.bright_tuner.stable_ok_required,       &e)) goto schema_fail;
	if (!require_float(jbt, "error_level_div",         cfg.bright_tuner.error_level_div,          &e)) goto schema_fail;
	if (!require_float(jbt, "k_hi",                    cfg.bright_tuner.k_hi,                     &e)) goto schema_fail;
	if (!require_float(jbt, "k_lo",                    cfg.bright_tuner.k_lo,                     &e)) goto schema_fail;
	if (!require_float(jbt, "sat_ratio_hi",            cfg.bright_tuner.sat_ratio_hi,             &e)) goto schema_fail;
	if (!require_float(jbt, "sat_ratio_lo",            cfg.bright_tuner.sat_ratio_lo,             &e)) goto schema_fail;
	if (!require_float(jbt, "mid_val",                 cfg.bright_tuner.mid_val,                  &e)) goto schema_fail;

    json_object_put(root);

    // validate
    {
        std::string ve;
        auto st = user_cfg_validate(cfg, &ve);
        if (st != CfgStatus::OK) {
            if (err) *err = ve;
            return st;
        }
    }

    // postprocess (normalize cam map + apply rotate180)
    {
        std::string pe;
        auto st = user_cfg_postprocess(cfg, &pe);
        if (st != CfgStatus::OK) {
            if (err) *err = pe;
            return st;
        }
    }

    return CfgStatus::OK;

schema_fail:
    json_object_put(root);
    if (err) *err = e;
    return CfgStatus::JSON_SCHEMA_FAIL;
}

