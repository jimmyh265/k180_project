// k180_nvtracker.cpp
#include "k180_nvtracker.h"

namespace k180::nvtracker {

bool NvTrackerService::init()
{
    std::lock_guard<std::mutex> lk(mtx_);
    clear_all_locked();
    state_.store(State::IDLE, std::memory_order_release);
    return true;
}

void NvTrackerService::shutdown()
{
    std::lock_guard<std::mutex> lk(mtx_);
    clear_all_locked();
    state_.store(State::IDLE, std::memory_order_release);
}

bool NvTrackerService::request_start_from_detection(const Detection& det,
                                                    std::uint64_t frame_seq)
{
    std::lock_guard<std::mutex> lk(mtx_);

    // 若已經在 pending 或 tracking 中，拒絕新的 start request
    const State cur = state_.load(std::memory_order_acquire);
    if (cur != State::IDLE) {
        return false;
    }

    pending_req_.det = det;
    pending_req_.frame_seq = frame_seq;
    pending_req_.request_id = next_request_id_++;
    pending_start_ = true;

    active_track_valid_ = false;
    active_track_ = {};

    state_.store(State::START_PENDING, std::memory_order_release);
    return true;
}

bool NvTrackerService::consume_pending_start(StartTrackRequest& out_req)
{
    std::lock_guard<std::mutex> lk(mtx_);

    if (!pending_start_) {
        return false;
    }

    out_req = pending_req_;
    pending_start_ = false;

    // 注意：
    // 這裡不把 state 改回 IDLE，也不改成 TRACKING。
    // 因為目前語意是：
    // START_PENDING = 已收到 start request，等待外部 bridge 真正完成 tracker 啟動
    return true;
}

bool NvTrackerService::set_tracking_started(std::uint64_t request_id,
                                            std::uint64_t track_id,
                                            const Detection& det,
                                            std::uint64_t frame_seq)
{
    std::lock_guard<std::mutex> lk(mtx_);

    if (state_.load(std::memory_order_acquire) != State::START_PENDING) {
        return false;
    }

    // 只接受和目前 pending request 對得上的 request_id
    // 若 consume_pending_start() 已經把 pending_start_ 清掉，
    // 仍允許使用 pending_req_ 中最後一次 request_id 做確認
    if (pending_req_.request_id != request_id) {
        return false;
    }

    active_track_.track_id = track_id;
    active_track_.last_det = det;
    active_track_.last_frame_seq = frame_seq;
    active_track_.valid = true;
    active_track_valid_ = true;

    state_.store(State::TRACKING, std::memory_order_release);
    return true;
}

bool NvTrackerService::on_tracker_output(std::uint64_t track_id,
                                         const Detection& det,
                                         std::uint64_t frame_seq)
{
    std::lock_guard<std::mutex> lk(mtx_);

    if (state_.load(std::memory_order_acquire) != State::TRACKING) {
        return false;
    }

    if (!active_track_valid_ || !active_track_.valid) {
        return false;
    }

    if (active_track_.track_id != track_id) {
        return false;
    }

    // 避免舊 frame 回頭覆蓋新資料
    if (frame_seq < active_track_.last_frame_seq) {
        return false;
    }

    active_track_.last_det = det;
    active_track_.last_frame_seq = frame_seq;
    active_track_.valid = true;
    return true;
}

void NvTrackerService::stop_tracking()
{
    std::lock_guard<std::mutex> lk(mtx_);
    clear_all_locked();
    state_.store(State::IDLE, std::memory_order_release);
}

void NvTrackerService::notify_tracking_lost()
{
    stop_tracking();
}

bool NvTrackerService::is_tracking() const
{
    return state_.load(std::memory_order_acquire) == State::TRACKING;
}

bool NvTrackerService::has_pending_start() const
{
    std::lock_guard<std::mutex> lk(mtx_);
    return state_.load(std::memory_order_acquire) == State::START_PENDING;
}

State NvTrackerService::state() const
{
    return state_.load(std::memory_order_acquire);
}

bool NvTrackerService::get_active_track(ActiveTrack& out_track) const
{
    std::lock_guard<std::mutex> lk(mtx_);

    if (!active_track_valid_ || !active_track_.valid) {
        return false;
    }

    out_track = active_track_;
    return true;
}

bool NvTrackerService::should_mark_lost(std::uint64_t current_frame_seq,
                                        std::uint64_t max_missing_frames) const
{
    std::lock_guard<std::mutex> lk(mtx_);

    if (state_.load(std::memory_order_acquire) != State::TRACKING) {
        return false;
    }

    if (!active_track_valid_ || !active_track_.valid) {
        return true;
    }

    if (current_frame_seq < active_track_.last_frame_seq) {
        return false;
    }

    const std::uint64_t missing = current_frame_seq - active_track_.last_frame_seq;
    return missing > max_missing_frames;
}

void NvTrackerService::clear_all_locked()
{
    pending_start_ = false;
    pending_req_ = {};

    active_track_valid_ = false;
    active_track_ = {};
}

NvTrackerService& service()
{
    static NvTrackerService s;
    return s;
}

} // namespace k180::nvtracker