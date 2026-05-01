#pragma once
#include <atomic>
#include <cstdint>
#include <vector>
#include "gy_logging.h"
#include "k180_constants.h"
#include "k180_runtime.h"
#include "k180_stage_queue.h"
#include "k180_frame_item.h" 

namespace k180::brighttuner {

enum BrightState : uint8_t {
    BS_OK,
    BS_TOO_B,
    BS_TOO_D
};

enum TunningStates : uint8_t {
    ST_INIT,
    ST_1,
    ST_2,
    ST_3
};

// 只保留「最新值」（coalesce）：producer 多次寫入，consumer 只取最後一次
struct PendingCtrl {
    std::atomic<bool> expo_dirty{false};
    std::atomic<int>  expo_val{0};

    std::atomic<bool> gain_dirty{false};
    std::atomic<int>  gain_val{0};
};

// ---- shared runtime state (inline 變數：header 可定義，整個程式只會一份) ----
inline std::atomic<int> adjust_cooldown_frames{0};

inline TunningStates ts_state = ST_INIT;

inline int exposure_tun_val = 5000;	// 如果是要追求 60fps, interval 17ms, 這邊要設為 8000比較安全, 如果是想要30fps, 設 16666 有時會小卡, 低於8000 都會有 電源燈光的閃頻問題。
inline int gain_tun_val     = 29000;

inline int ok_streak = 0;
inline int last_logged_state = -1;

// gain 以大約 6k ~ 7k 為一級慢慢遞增
inline constexpr int CAM_GAIN_TUNE_STEP[10] = {
    1000, 5000, 10000, 15000, 20000,
    25000, 30000, 35000, 40000, 45000
};

// expo 以大約 1k 為一級慢慢遞增
inline constexpr int CAM_EXPO_TUNE_STEP[10] = {
    100, 1000, 2000, 3000, 4000,
    5000, 6000, 7000, 8000, 9000
};

struct BrightJob {
    uint64_t seq = 0;
    cv::Mat  gray_small; // CV_8UC1
};

using BrightJobPtr = std::shared_ptr<BrightJob>;

// ---- params / constants ----
inline int   MIN_STEP                 = 20;

inline int CAM_EXPO_MIN           = 100;
inline int CAM_EXPO_FINE_TUN_MIN  = 1000;
inline int CAM_EXPO_MID           = 5000;		// 16666
inline int CAM_EXPO_MAX           = 100000;
inline int CAM_EXPO_FINE_TUN_MAX  = 90000;
inline int CAM_EXPO_TUNE_STEP_S   = 100;
inline int CAM_EXPO_TUNE_STEP_L   = 1000;

inline int CAM_GAIN_MIN           = 10000;
inline int CAM_GAIN_FINE_TUN_MIN  = 100000;
inline int CAM_GAIN_MID           = 29000;	//10000;
inline int CAM_GAIN_MAX           = 1280000;
inline int CAM_GAIN_FINE_TUN_MAX  = 1180000;
inline int CAM_GAIN_TUNE_STEP_S   = 1000;
inline int CAM_GAIN_TUNE_STEP_L   = 2000;

void request_exposure(int cam_idx, int val);
void request_gain(int cam_idx, int val);

bool consume_exposure(int cam_idx, int& out_val);
bool consume_gain(int cam_idx, int& out_val);

void bright_adjust(int cam_id,
                       StageQueue<FramePtr>* q,
                       std::atomic<bool>& running);

} // namespace k180::brighttuner
