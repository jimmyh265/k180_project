#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <shared_mutex>
#include <unistd.h>     // usleep(), sleep()
#include <pthread.h>    // pthread_exit()
#include <opencv2/opencv.hpp>
#include <opencv2/core/cuda.hpp> 
#include <opencv2/cudawarping.hpp>
#include <opencv2/cudaimgproc.hpp>
#include "k180_runtime.h"
#include "k180_bright_tuner.h"


// 你貼的程式內有用到 cvtColor / COLOR_BGRA2GRAY（未加 cv:: 前綴），這裡加 using
using namespace std;
using namespace cv;
using namespace k180::constants;
using namespace k180::brighttuner;
using namespace k180::runtime;

extern std::shared_mutex mat_mutex[7];

extern bool rec_img_ready;
extern std::atomic<bool> keep_running;
// extern bool keep_running;
// extern std::vector<bool> SD;
// extern std::vector<cv::Mat> cam_BGRA_cMat;

namespace k180::brighttuner {

struct BrightObs {
    int   cam_id = -1;
    int   error_level = 0;
    float da = 0.f;
    float D  = 0.f;
    float M  = 0.f;
    float K  = 0.f;
    float sat_hi = 0.f; // 修法B: 亮飽和比例（區間或單點都行）
    float sat_lo = 0.f; // 修法B: 暗飽和比例
};

static PendingCtrl g_pending[k180::constants::CAM_NUMBER];

static inline bool valid_idx(int cam_idx) {
    return cam_idx >= 0 && cam_idx < k180::constants::CAM_NUMBER;
}

// -------- Producer: request_* --------
void request_exposure(int cam_idx, int val) {
    if (!valid_idx(cam_idx)) return;
    g_pending[cam_idx].expo_val.store(val, std::memory_order_relaxed);
    g_pending[cam_idx].expo_dirty.store(true, std::memory_order_release);
}

void request_gain(int cam_idx, int val) {
    if (!valid_idx(cam_idx)) return;
    g_pending[cam_idx].gain_val.store(val, std::memory_order_relaxed);
    g_pending[cam_idx].gain_dirty.store(true, std::memory_order_release);
}

// -------- Consumer: consume_* (test-and-clear) --------
bool consume_exposure(int cam_idx, int& out_val) {
    if (!valid_idx(cam_idx)) return false;
    if (!g_pending[cam_idx].expo_dirty.exchange(false, std::memory_order_acq_rel))
        return false;
    out_val = g_pending[cam_idx].expo_val.load(std::memory_order_relaxed);
    return true;
}

bool consume_gain(int cam_idx, int& out_val) {
    if (!valid_idx(cam_idx)) return false;
    if (!g_pending[cam_idx].gain_dirty.exchange(false, std::memory_order_acq_rel))
        return false;
    out_val = g_pending[cam_idx].gain_val.load(std::memory_order_relaxed);
    return true;
}

inline int clamp_step(int v, int max_v) {
    if (v < MIN_STEP) return MIN_STEP;
    if (v > max_v) return max_v;
    return v;
}

// 根據 base_step 與 error_level 回傳較平滑的 gain step
inline int get_gain_step_from_base(int base_step, int error_level)
{
    // 若誤差等級為 0，直接回最小步階（或 0）
    if (error_level <= 0) return 0;

    // multipliers：誤差越大倍數越大（但使用溫和曲線）
    float mult;
    if (error_level >= 8) mult = 3.5f;
    else if (error_level >= 6) mult = 2.5f;
    else if (error_level >= 4) mult = 1.8f;
    else if (error_level >= 2) mult = 1.2f;
    else mult = 1.0f;

    int step = int(base_step * mult);

    // 若 base_step 很小 (例如 1000) 但我們希望小誤差時更小，
    // 可以對小等級做額外縮放（避免一次跳太大）
    if (error_level < 3) {
        // 針對小誤差減少 step（更保守）
        step = step / 2;
    }

    return clamp_step(step, cfggg.bright_tuner.max_gain_step);
}

// 根據 base_step 與 error_level 回傳較平滑的 expo step
inline int get_expo_step_from_base(int base_step, int error_level)
{
    if (error_level <= 0) return 0;

    float mult;
    if (error_level >= 8) mult = 3.0f;
    else if (error_level >= 6) mult = 2.0f;
    else if (error_level >= 4) mult = 1.6f;
    else if (error_level >= 2) mult = 1.1f;
    else mult = 1.0f;

    int step = int(base_step * mult);

    if (error_level < 3) step = step / 2;

    return clamp_step(step, cfggg.bright_tuner.max_expo_step);
}

void bright_tunning(BrightState bs, const BrightObs& o)
{
    // --- 小工具：狀態切換必印一次 ---
    auto log_state = [&](const char* tag) {
        log_info_fmt("[BT][STATE] %s st=%d expo=%d gain=%d cam=%d",
                     tag, ts_state, exposure_tun_val, gain_tun_val, o.cam_id);
        last_logged_state = ts_state;
    };

    // --- 小工具：expo/gain 改變才印 ---
    auto log_expo = [&](const char* dir, int oldv, int newv, int step, int base_step) {
        log_info_fmt("[BT][EXPO] %s %d->%d step=%d base=%d err=%d st=%d cam=%d da=%.1f D=%.1f K=%.2f satH=%.2f satL=%.2f",
                     dir, oldv, newv, step, base_step, o.error_level, ts_state, o.cam_id,
                     o.da, o.D, o.K, o.sat_hi, o.sat_lo);
    };
    auto log_gain = [&](const char* dir, int oldv, int newv, int step, int base_step) {
        log_info_fmt("[BT][GAIN] %s %d->%d step=%d base=%d err=%d st=%d cam=%d da=%.1f D=%.1f K=%.2f satH=%.2f satL=%.2f",
                     dir, oldv, newv, step, base_step, o.error_level, ts_state, o.cam_id,
                     o.da, o.D, o.K, o.sat_hi, o.sat_lo);
    };

	// ===== guard：到極限就不再調（避免邊界振盪/連打控制）=====
	{
		// 你可以視需求調整 margin（避免在極限附近反覆抖動）
		constexpr int EXPO_MARGIN = 0;  // 例如 0 或 50 或 100
		constexpr int GAIN_MARGIN = 0;  // 例如 0 或 5000

		const bool expo_at_min = (exposure_tun_val <= CAM_EXPO_MIN + EXPO_MARGIN);
		const bool expo_at_max = (exposure_tun_val >= CAM_EXPO_MAX - EXPO_MARGIN);
		const bool gain_at_min = (gain_tun_val     <= CAM_GAIN_MIN + GAIN_MARGIN);
		const bool gain_at_max = (gain_tun_val     >= CAM_GAIN_MAX - GAIN_MARGIN);

		// 太亮但已無可再降（expo&gain 都到下限附近）
		if (bs == BS_TOO_B && expo_at_min && gain_at_min) {
			bs = BS_OK;
		}

		// 太暗但已無可再升（expo&gain 都到上限附近）
		if (bs == BS_TOO_D && expo_at_max && gain_at_max) {
			bs = BS_OK;
		}
	}

    // --- BS_OK：進 stable（保留你原本行為，但 log 統一格式） ---
    if (bs == BS_OK) {
        ok_streak++;
        if (ok_streak >= cfggg.bright_tuner.stable_ok_required) {
            ts_state = ST_INIT;
            if (last_logged_state != ts_state) {
                log_info_fmt("[BT][STABLE] st=%d expo=%d gain=%d cam=%d",
                             ts_state, exposure_tun_val, gain_tun_val, o.cam_id);
                last_logged_state = ts_state;
            }
            ok_streak = 0;
        }
        return;
    }
    ok_streak = 0;

    // --- cooldown：只在「第一次被擋住」時印一次（避免洗屏） ---
    if (adjust_cooldown_frames.load() > 0) {
        static int last_cd = 0;
        int cd = adjust_cooldown_frames.load();
        if (cd != last_cd) {
            // 只記錄「剛進入 cooldown / 或 cd 值變化」一次即可；你不想印也可整段刪掉
            log_info_fmt("[BT][COOLDOWN] cd=%d st=%d expo=%d gain=%d cam=%d",
                         cd, ts_state, exposure_tun_val, gain_tun_val, o.cam_id);
            last_cd = cd;
        }
        return;
    }

    switch (ts_state) {
    case ST_INIT: {
        int base_level = std::min(9, std::max(0, int(log2(std::max(1, gain_tun_val / CAM_GAIN_MIN)))));
        int base_step  = CAM_EXPO_TUNE_STEP[base_level];
        int step       = get_expo_step_from_base(base_step, o.error_level);

        // 狀態切換：expo 到 MID 附近就改調 gain
        if ((bs == BS_TOO_B && exposure_tun_val <= CAM_EXPO_MID) ||
            (bs == BS_TOO_D && exposure_tun_val >= CAM_EXPO_MID)) {
            ts_state = ST_1;
            log_state("GOTO_ST_1");
            break;
        }

        int old_expo = exposure_tun_val;

		if (exposure_tun_val <= CAM_EXPO_MIN + 500) {
			step = std::min(step, 50);
		}
		else if (exposure_tun_val >= CAM_EXPO_MAX - 500) {
			step = std::min(step, 50);
		}

        if (bs == BS_TOO_B && exposure_tun_val > CAM_EXPO_MID) {
            exposure_tun_val -= step;
            if (exposure_tun_val < CAM_EXPO_MID) exposure_tun_val = CAM_EXPO_MID;
        } else if (bs == BS_TOO_D && exposure_tun_val < CAM_EXPO_MID) {
            exposure_tun_val += step;
            if (exposure_tun_val > CAM_EXPO_MID) exposure_tun_val = CAM_EXPO_MID;
        }

        // 只有真的變更才印
        if (exposure_tun_val != old_expo) {
            log_expo((bs == BS_TOO_B) ? "-" : "+", old_expo, exposure_tun_val, step, base_step);
        }

        // 下指令（沿用你原邏輯）
        int req_expo = exposure_tun_val;
        if (req_expo > (CAM_EXPO_MID - 2000) && req_expo < (CAM_EXPO_MID + 2000)) req_expo = CAM_EXPO_MID;
        for (int i = 0; i < CAM_NUMBER; i++) request_exposure(i, req_expo);

        adjust_cooldown_frames.store(cfggg.bright_tuner.adjust_cooldown_default);
        break;
    }

    case ST_1: {
        int base_level = std::min(9, std::max(0, int(log2(std::max(1, gain_tun_val / CAM_GAIN_MIN)))));
        int base_step  = CAM_GAIN_TUNE_STEP[base_level];
        int step       = get_gain_step_from_base(base_step, o.error_level);

        if ((bs == BS_TOO_B && gain_tun_val <= CAM_GAIN_MIN) ||
            (bs == BS_TOO_D && gain_tun_val >= CAM_GAIN_MAX)) {
            ts_state = ST_2;
            log_state("GOTO_ST_2");
            break;
        }

        int old_gain = gain_tun_val;

        if (bs == BS_TOO_B && gain_tun_val > CAM_GAIN_MIN) {
            gain_tun_val -= step;
            if (gain_tun_val < CAM_GAIN_MIN) gain_tun_val = CAM_GAIN_MIN;
        } else if (bs == BS_TOO_D && gain_tun_val < CAM_GAIN_MAX) {
            gain_tun_val += step;
            if (gain_tun_val > CAM_GAIN_MAX) gain_tun_val = CAM_GAIN_MAX;
        }

        if (gain_tun_val != old_gain) {
            log_gain((bs == BS_TOO_B) ? "-" : "+", old_gain, gain_tun_val, step, base_step);
        }

        for (int i = 0; i < CAM_NUMBER; i++) request_gain(i, gain_tun_val);

        adjust_cooldown_frames.store(cfggg.bright_tuner.adjust_cooldown_default);
        break;
    }

    case ST_2: {
        int base_level = std::min(9, std::max(0, int(log2(std::max(1, gain_tun_val / CAM_GAIN_MIN)))));
        int base_step  = CAM_EXPO_TUNE_STEP[base_level];

        int step = get_expo_step_from_base(base_step, o.error_level);
        step = clamp_step(step, cfggg.bright_tuner.max_expo_step);

		if (exposure_tun_val <= CAM_EXPO_MIN + 500) {
			step = std::min(step, 50);
		}
		else if (exposure_tun_val >= CAM_EXPO_MAX - 500) {
			step = std::min(step, 50);
		}

		if (bs == BS_TOO_B) {	// 解決：暗→亮：gain 很高 → ST_2 降 shutter 到 MID → 回 ST_1 降 gain
			if (gain_tun_val > CAM_GAIN_MIN + CAM_GAIN_MID) {
				if (exposure_tun_val <= CAM_EXPO_MID || (exposure_tun_val - step) <= CAM_EXPO_MID) {
					ts_state = ST_1;
					log_state("ST2->ST1 (reach MID, TOO_B, gain_high)");
					break;
				}
			}
		} 
		// 不需要 亮到暗，因為：亮到 gain 已 MIN：gain 不高（接近 MIN）→ 不回 ST_1，ST_2 直接繼續把 shutter 往下壓到需要的程度

        if ((bs == BS_TOO_B && exposure_tun_val <= CAM_EXPO_MIN) ||
            (bs == BS_TOO_D && exposure_tun_val >= CAM_EXPO_MAX)) {
            ts_state = ST_INIT;
            log_state("GOTO_ST_INIT");
            break;
        }

        int old_expo = exposure_tun_val;

        if (bs == BS_TOO_B && exposure_tun_val > CAM_EXPO_MIN) {
            exposure_tun_val -= step;
            if (exposure_tun_val < CAM_EXPO_MIN) exposure_tun_val = CAM_EXPO_MIN;
        } else if (bs == BS_TOO_D && exposure_tun_val < CAM_EXPO_MAX) {
            exposure_tun_val += step;
            if (exposure_tun_val > CAM_EXPO_MAX) exposure_tun_val = CAM_EXPO_MAX;
        }

        if (exposure_tun_val != old_expo) {
            log_expo((bs == BS_TOO_B) ? "-" : "+", old_expo, exposure_tun_val, step, base_step);
        }

        int req_expo = exposure_tun_val;
        if (req_expo > (CAM_EXPO_MID - 2000) && req_expo < (CAM_EXPO_MID + 2000)) req_expo = CAM_EXPO_MID;
        for (int i = 0; i < CAM_NUMBER; i++) request_exposure(i, req_expo);

        adjust_cooldown_frames.store(cfggg.bright_tuner.adjust_cooldown_default);
        break;
    }

    default:
        break;
    }
}

void bright_adjust(int cam_id,
                   StageQueue<FramePtr>* q,
                   std::atomic<bool>& running)
{
    k180::runtime::cuda_set_current_for_thread("bright_adjust");

    cv::Mat grayImage;
    cv::cuda::Stream stream;
    cv::cuda::GpuMat g_small_rgba, g_small_gray;

    FramePtr tmp;
    int height, width;
    BrightState probe;
    float da;
    int MID_VAL = cfggg.bright_tuner.mid_val;

    // [新增] 用來偵測 probe 方向翻轉（只在翻轉時印一次）
    static thread_local int last_probe = -999;
	static thread_local bool k_active = false; // false=目前視為 OK；true=目前視為需要調整

    while (running.load(std::memory_order_relaxed)) {
        int sleep_time_us = 100000; // 預設 0.1 秒
        if (exposure_tun_val >= CAM_EXPO_MAX && gain_tun_val >= CAM_GAIN_MAX) {
            sleep_time_us = 1000000; // 拉長到 1 秒
        }
        usleep(sleep_time_us);

        if (!running.load(std::memory_order_relaxed)) break;

        // cooldown > 0 就遞減
        int cd = adjust_cooldown_frames.load(std::memory_order_relaxed);
        if (cd > 0) {
            adjust_cooldown_frames.fetch_sub(1, std::memory_order_relaxed);
        }

        if (!q->pop_latest(tmp)) break;

        cv::cuda::resize(tmp->fr.rgba, g_small_rgba, cv::Size(320, 180), 0, 0, cv::INTER_LINEAR, stream);
        cv::cuda::cvtColor(g_small_rgba, g_small_gray, cv::COLOR_RGBA2GRAY, 0, stream);
        g_small_gray.download(grayImage, stream);
        stream.waitForCompletion();

        height = grayImage.rows;
        width  = grayImage.cols;

        float a = 0.f;
        int Hist[256] = {0};

        for (int i = 0; i < height; i++) {
            const uchar* row = grayImage.ptr<uchar>(i);
            for (int j = 0; j < width; j++) {
                a += float(row[j] - MID_VAL);
                Hist[row[j]]++;
            }
        }

        const int N = height * width;

        da = a / float(N);
        float D = std::fabs(da);
        int error_level = std::min(9, int(D / cfggg.bright_tuner.error_level_div));

        float Ma = 0.f;
        for (int i = 0; i < 256; i++) {
            Ma += (std::fabs(i - MID_VAL - da) * Hist[i]);
        }
        Ma /= float(N);

        float M = std::fabs(Ma);
        float K = (M > 1e-6f) ? (D / M) : 0.0f;

        // ===== 修法B：區間飽和比例 =====
        static constexpr int SAT_BIN_HI0 = 250; // 高飽和區間 [250..255]
        static constexpr int SAT_BIN_LO1 = 5;   // 低飽和區間 [0..5]

        int hi_cnt = 0;
        for (int i = SAT_BIN_HI0; i <= 255; ++i) hi_cnt += Hist[i];

        int lo_cnt = 0;
        for (int i = 0; i <= SAT_BIN_LO1; ++i) lo_cnt += Hist[i];

        float sat_hi = float(hi_cnt) / float(N);
        float sat_lo = float(lo_cnt) / float(N);

        // ===== probe 判斷（移除每圈 bright/dark log）=====
        if (sat_hi > cfggg.bright_tuner.sat_ratio_hi) {
            probe = BS_TOO_B;
        }
        else if (sat_lo > cfggg.bright_tuner.sat_ratio_lo) {
            probe = BS_TOO_D;
        }
        else {
			// 走 D + K(hysteresis)
			if (D < cfggg.bright_tuner.hysteresis_d_threshold) {
				// D 很小 => 一律 OK，並把 hysteresis 狀態復位（避免卡在 active）
				k_active = false;
				probe = BS_OK;
			} else {
				const float k_hi = cfggg.bright_tuner.k_hi; // 進入調整門檻 (ex:1.10)
				const float k_lo = cfggg.bright_tuner.k_lo; // 回到 OK 門檻 (ex:0.90)

				if (!k_active) {
					// 目前 OK：要跨過 k_hi 才進入調整
					if (K > k_hi) k_active = true;
				} else {
					// 目前在調整：要跌破 k_lo 才回 OK
					if (K < k_lo) k_active = false;
				}

				if (!k_active) {
					probe = BS_OK;
				} else {
					probe = (da > 0) ? BS_TOO_B : BS_TOO_D;
				}
			}
        }

        // [新增] 只在 probe 方向改變時印一次（方便抓振盪/翻轉）
        if ((int)probe != last_probe) {
            log_info_fmt("[BT][PROBE] %d->%d cam=%d da=%.1f D=%.1f M=%.1f K=%.2f err=%d satH=%.2f satL=%.2f cd=%d expo=%d gain=%d",
                         last_probe, (int)probe, cam_id,
                         da, D, M, K, error_level,
                         sat_hi, sat_lo,
                         adjust_cooldown_frames.load(), exposure_tun_val, gain_tun_val);
            last_probe = (int)probe;
        }

        // [新增] 組 obs 傳給新版 bright_tunning
        BrightObs o;
        o.cam_id      = cam_id;
        o.error_level = error_level;
        o.da          = da;
        o.D           = D;
        o.M           = M;
        o.K           = K;
        o.sat_hi      = sat_hi;
        o.sat_lo      = sat_lo;

        bright_tunning(probe, o);
    }

    log_info_fmt("bright_adjust exit");
}

}