// user_cfg_postprocess.cpp
#include "user_def_json.h"

CfgStatus user_cfg_postprocess(UserConfig& cfg, std::string* err) {
    (void)err; // 目前 postprocess 不會失敗；若你未來要加衍生值檢查可用

    // Apply rotate180 for active mapping & picture layout
    if (cfg.rotate180) {
        for (int i = 0; i < 4; ++i) cfg.cam_num[i] = cfg.cam_num_r180[i];
        cfg.pic_w_1234 = cfg.pic_w_180_1234;
        cfg.pic_h_1234 = cfg.pic_h_180_1234;
        cfg.pic_h_cut  = cfg.pic_h_180_cut;
    } else {
        for (int i = 0; i < 4; ++i) cfg.cam_num[i] = cfg.cam_num_r0[i];
        cfg.pic_w_1234 = cfg.pic_w_0_1234;
        cfg.pic_h_1234 = cfg.pic_h_0_1234;
        cfg.pic_h_cut  = cfg.pic_h_0_cut;
    }

    return CfgStatus::OK;
}

