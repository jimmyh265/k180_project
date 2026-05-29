// k180_nvtracker_bridge.h
#pragma once

#include <cstdint>
#include <mutex>

#include "types.h"              // Detection
#include "k180_nvtracker.h"

namespace k180::nvtracker {

// 這層是「控制橋接層」：
// 1. 從 service() 取 pending start request
// 2. 等待第一個符合條件的 tracker output，建立 active track
// 3. 後續把同一 track_id 的結果回填給 service()
// 4. 可做 lost 檢查
//
// 注意：
// - 這裡先不直接依賴 GstBuffer / NvDsBatchMeta
// - 真正的 probe / metadata code 可在外部先把 tracker output
//   整理成 (track_id, Detection, frame_seq) 再丟進來

class NvTrackerBridge {
public:
    NvTrackerBridge() = default;
    ~NvTrackerBridge() = default;

    NvTrackerBridge(const NvTrackerBridge&) = delete;
    NvTrackerBridge& operator=(const NvTrackerBridge&) = delete;

    bool init();
    void shutdown();

    // 每輪/每幀先呼叫一次，看看是否有新的 start request
    // 若有，就進入「等待第一個 tracker output 來綁定 track_id」狀態
    void poll_start_request();

    // 餵入某一個 tracker output。
    //
    // 回傳 true:
    //   代表這筆 output 已被本 bridge 接收/使用
    //
    // 回傳 false:
    //   代表這筆 output 與目前 bridge 狀態無關
    bool ingest_tracker_output(std::uint64_t track_id,
                               const Detection& det,
                               std::uint64_t frame_seq);

    // 檢查目前是否該 lost；若是，會直接通知 service().notify_tracking_lost()
    bool check_and_handle_lost(std::uint64_t current_frame_seq,
                               std::uint64_t max_missing_frames);

    // 手動重置 bridge 內部狀態
    void reset();

private:
    bool detection_matches_start_request(const Detection& det) const;

private:
    mutable std::mutex mtx_;

    bool waiting_start_bind_ = false;
    StartTrackRequest pending_start_req_{};

    bool active_track_bound_ = false;
    std::uint64_t active_track_id_ = 0;
};

NvTrackerBridge& bridge();

} // namespace k180::nvtracker
