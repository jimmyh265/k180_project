// k180_nvtracker.h
#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>

#include "types.h"   // Detection

namespace k180::nvtracker {

enum class State : int {
    IDLE = 0,
    START_PENDING = 1,
    TRACKING = 2,
};

struct StartTrackRequest {
    Detection det{};
    std::uint64_t frame_seq = 0;
    std::uint64_t request_id = 0;
};

struct ActiveTrack {
    std::uint64_t track_id = 0;
    Detection last_det{};
    std::uint64_t last_frame_seq = 0;
    bool valid = false;
};

class NvTrackerService {
public:
    NvTrackerService() = default;
    ~NvTrackerService() = default;

    NvTrackerService(const NvTrackerService&) = delete;
    NvTrackerService& operator=(const NvTrackerService&) = delete;

    bool init();
    void shutdown();

    // user click 命中某個 detection 後，發出「開始追蹤」請求
    // 注意：這不代表 tracker 已經真的開始輸出結果
    bool request_start_from_detection(const Detection& det, std::uint64_t frame_seq);

    // 由真正接 nvtracker / DeepStream 的那層取走 pending request
    bool consume_pending_start(StartTrackRequest& out_req);

    // 當外部 bridge 確認 tracker 已正式啟動後呼叫
    // request_id 用來避免舊 request / 舊 frame 的錯誤回填
    bool set_tracking_started(std::uint64_t request_id,
                              std::uint64_t track_id,
                              const Detection& det,
                              std::uint64_t frame_seq);

    // 每次 tracker 有新的單目標結果時回填
    // 只有 track_id 符合目前 active track 時才會更新成功
    bool on_tracker_output(std::uint64_t track_id,
                           const Detection& det,
                           std::uint64_t frame_seq);

    // 手動停止 tracking
    void stop_tracking();

    // tracker 判定 lost 時呼叫
    void notify_tracking_lost();

    bool is_tracking() const;
    bool has_pending_start() const;
    State state() const;

    bool get_active_track(ActiveTrack& out_track) const;

    // 判斷是否應該視為 lost
    bool should_mark_lost(std::uint64_t current_frame_seq,
                          std::uint64_t max_missing_frames) const;

private:
    void clear_all_locked();

private:
    mutable std::mutex mtx_;
    std::atomic<State> state_{State::IDLE};

    bool pending_start_ = false;
    StartTrackRequest pending_req_{};

    bool active_track_valid_ = false;
    ActiveTrack active_track_{};

    std::uint64_t next_request_id_ = 1;
};

NvTrackerService& service();

} // namespace k180::nvtracker