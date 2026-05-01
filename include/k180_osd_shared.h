// k180_osd_shared.h
#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <vector>

#include <cuda_runtime.h>
#include "types.h"   // Detection

namespace k180::osd {

enum class SlotState : uint8_t {
    FREE = 0,
    INFER_PENDING
};

struct OsdDetSlot {
    std::uint64_t frame_seq = 0;

    // device-side result
    Detection* det_device = nullptr;
    int* det_count_device = nullptr;

    // host-side pinned result
    Detection* det_host = nullptr;
    int* det_count_host = nullptr;

    // event recorded after all async work for this slot is queued
    cudaEvent_t ready_event = nullptr;

    std::atomic<SlotState> state{SlotState::FREE};
};

struct OsdShared {
    mutable std::shared_mutex mtx;

    // detect result slots
    std::vector<std::unique_ptr<OsdDetSlot>> slots;

    // track result slots
    std::vector<std::unique_ptr<OsdDetSlot>> track_slots;

    // latest completed detect result for fallback
    mutable std::mutex latest_mtx;
    std::vector<Detection> latest_det_host;
    int latest_det_count = 0;
    std::uint64_t latest_frame_seq = 0;
    bool latest_valid = false;

    // latest completed track result for fallback
    mutable std::mutex latest_track_mtx;
    std::vector<Detection> latest_track_det_host;
    int latest_track_det_count = 0;
    std::uint64_t latest_track_frame_seq = 0;
    bool latest_track_valid = false;

    // detect empty-result policy
    int latest_empty_streak = 0;
    int latest_max_fallback_frames = 2;
    int latest_clear_after_empty = 2;

    // track empty-result policy
    int latest_track_empty_streak = 0;
    int latest_track_max_fallback_frames = 2;
    int latest_track_clear_after_empty = 2;

    // detect counters
    std::atomic<std::uint64_t> infer_drop_no_slot{0};
    std::atomic<std::uint64_t> probe_miss_not_ready{0};
    std::atomic<std::uint64_t> probe_miss_not_found{0};
    std::atomic<std::uint64_t> probe_attach_ok{0};

    std::atomic<std::uint64_t> probe_exact_ready_ok{0};
    std::atomic<std::uint64_t> probe_fallback_used{0};
    std::atomic<std::uint64_t> probe_fallback_miss{0};

    // track counters
    std::atomic<std::uint64_t> track_drop_no_slot{0};
    std::atomic<std::uint64_t> track_probe_miss_not_ready{0};
    std::atomic<std::uint64_t> track_probe_miss_not_found{0};
    std::atomic<std::uint64_t> track_probe_attach_ok{0};

    std::atomic<std::uint64_t> track_probe_exact_ready_ok{0};
    std::atomic<std::uint64_t> track_probe_fallback_used{0};
    std::atomic<std::uint64_t> track_probe_fallback_miss{0};
};

} // namespace k180::osd