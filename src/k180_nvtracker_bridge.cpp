// k180_nvtracker_bridge.cpp
#include "k180_nvtracker_bridge.h"

#include <cmath>

namespace k180::nvtracker {

namespace {

// 你目前 Detection 的 bbox[4] 是 center_x, center_y, w, h
inline float bbox_cx(const Detection& d) { return d.bbox[0]; }
inline float bbox_cy(const Detection& d) { return d.bbox[1]; }
inline float bbox_w (const Detection& d) { return d.bbox[2]; }
inline float bbox_h (const Detection& d) { return d.bbox[3]; }

inline float safe_abs(float v) { return (v >= 0.0f) ? v : -v; }

} // namespace

bool NvTrackerBridge::init()
{
    std::lock_guard<std::mutex> lk(mtx_);
    waiting_start_bind_ = false;
    pending_start_req_ = {};
    active_track_bound_ = false;
    active_track_id_ = 0;
    return true;
}

void NvTrackerBridge::shutdown()
{
    reset();
}

void NvTrackerBridge::poll_start_request()
{
    std::lock_guard<std::mutex> lk(mtx_);

    // 若目前 bridge 已在等待 bind，或已經有 active track，就先不再吃新的 request
    if (waiting_start_bind_ || active_track_bound_) {
        return;
    }

    StartTrackRequest req{};
    if (!service().consume_pending_start(req)) {
        return;
    }

    pending_start_req_ = req;
    waiting_start_bind_ = true;
    active_track_bound_ = false;
    active_track_id_ = 0;
}

bool NvTrackerBridge::ingest_tracker_output(std::uint64_t track_id,
                                            const Detection& det,
                                            std::uint64_t frame_seq)
{
    std::lock_guard<std::mutex> lk(mtx_);

    // case 1:
    // 尚未綁定 active track，但目前正在等第一個 tracker output 來對應 start request
    if (waiting_start_bind_) {
        if (!detection_matches_start_request(det)) {
            return false;
        }

        if (!service().set_tracking_started(pending_start_req_.request_id,
                                            track_id,
                                            det,
                                            frame_seq)) {
            return false;
        }

        active_track_id_ = track_id;
        active_track_bound_ = true;
        waiting_start_bind_ = false;
        return true;
    }

    // case 2:
    // 已綁定 active track，只接受同一個 track_id
    if (active_track_bound_) {
        if (track_id != active_track_id_) {
            return false;
        }

        return service().on_tracker_output(track_id, det, frame_seq);
    }

    return false;
}

bool NvTrackerBridge::check_and_handle_lost(std::uint64_t current_frame_seq,
                                            std::uint64_t max_missing_frames)
{
    std::lock_guard<std::mutex> lk(mtx_);

    if (!active_track_bound_) {
        return false;
    }

    if (!service().should_mark_lost(current_frame_seq, max_missing_frames)) {
        return false;
    }

    service().notify_tracking_lost();

    waiting_start_bind_ = false;
    pending_start_req_ = {};
    active_track_bound_ = false;
    active_track_id_ = 0;
    return true;
}

void NvTrackerBridge::reset()
{
    std::lock_guard<std::mutex> lk(mtx_);
    waiting_start_bind_ = false;
    pending_start_req_ = {};
    active_track_bound_ = false;
    active_track_id_ = 0;
}

bool NvTrackerBridge::detection_matches_start_request(const Detection& det) const
{
    // 第一版先用「bbox 近似」來判斷：
    // tracker 的第一筆輸出，只要和 start request 的 bbox 足夠接近，就視為同一目標。
    //
    // 這是刻意簡化版，適合單目標 click-to-track 第一版。
    // 之後若要更穩，可再改成：
    // - class_id 必須一致
    // - IoU 門檻
    // - 中心點距離 + 尺寸差雙條件
    const Detection& ref = pending_start_req_.det;

    const float cx_diff = safe_abs(bbox_cx(det) - bbox_cx(ref));
    const float cy_diff = safe_abs(bbox_cy(det) - bbox_cy(ref));
    const float w_diff  = safe_abs(bbox_w(det)  - bbox_w(ref));
    const float h_diff  = safe_abs(bbox_h(det)  - bbox_h(ref));

    const float pos_tol_x = std::max(8.0f, bbox_w(ref) * 0.25f);
    const float pos_tol_y = std::max(8.0f, bbox_h(ref) * 0.25f);
    const float size_tol_w = std::max(8.0f, bbox_w(ref) * 0.30f);
    const float size_tol_h = std::max(8.0f, bbox_h(ref) * 0.30f);

    if (cx_diff > pos_tol_x) return false;
    if (cy_diff > pos_tol_y) return false;
    if (w_diff  > size_tol_w) return false;
    if (h_diff  > size_tol_h) return false;

    return true;
}

NvTrackerBridge& bridge()
{
    static NvTrackerBridge b;
    return b;
}

} // namespace k180::nvtracker
