// k180_osd_slots.cpp
#include "k180_osd_slots.h"

#include <cstdio>
#include <cstring>
#include <mutex>
#include <shared_mutex>

namespace k180::osd {

namespace {

static bool init_slot_vector(std::vector<std::unique_ptr<OsdDetSlot>>& vec,
                             int slot_count,
                             int max_det)
{
    vec.clear();
    vec.reserve(static_cast<size_t>(slot_count));

    for (int i = 0; i < slot_count; ++i) {
        vec.emplace_back(std::make_unique<OsdDetSlot>());
    }

    for (auto& sp : vec) {
        auto& s = *sp;
        s.frame_seq = 0;
        s.state.store(SlotState::FREE, std::memory_order_relaxed);

        s.det_device = nullptr;
        s.det_count_device = nullptr;
        s.det_host = nullptr;
        s.det_count_host = nullptr;
        s.ready_event = nullptr;

        if (cudaMalloc((void**)&s.det_device, max_det * sizeof(Detection)) != cudaSuccess) {
            std::fprintf(stderr, "[OSD] cudaMalloc(det_device) failed\n");
            return false;
        }

        if (cudaMalloc((void**)&s.det_count_device, sizeof(int)) != cudaSuccess) {
            std::fprintf(stderr, "[OSD] cudaMalloc(det_count_device) failed\n");
            return false;
        }

        if (cudaMallocHost((void**)&s.det_host, max_det * sizeof(Detection)) != cudaSuccess) {
            std::fprintf(stderr, "[OSD] cudaMallocHost(det_host) failed\n");
            return false;
        }

        if (cudaMallocHost((void**)&s.det_count_host, sizeof(int)) != cudaSuccess) {
            std::fprintf(stderr, "[OSD] cudaMallocHost(det_count_host) failed\n");
            return false;
        }

        *s.det_count_host = 0;

        if (cudaEventCreateWithFlags(&s.ready_event, cudaEventDisableTiming) != cudaSuccess) {
            std::fprintf(stderr, "[OSD] cudaEventCreateWithFlags failed\n");
            return false;
        }
    }

    return true;
}

static void destroy_slot_vector(std::vector<std::unique_ptr<OsdDetSlot>>& vec)
{
    for (auto& sp : vec) {
        auto& s = *sp;

        if (s.ready_event) {
            cudaEventDestroy(s.ready_event);
            s.ready_event = nullptr;
        }

        if (s.det_device) {
            cudaFree(s.det_device);
            s.det_device = nullptr;
        }

        if (s.det_count_device) {
            cudaFree(s.det_count_device);
            s.det_count_device = nullptr;
        }

        if (s.det_host) {
            cudaFreeHost(s.det_host);
            s.det_host = nullptr;
        }

        if (s.det_count_host) {
            cudaFreeHost(s.det_count_host);
            s.det_count_host = nullptr;
        }

        s.frame_seq = 0;
        s.state.store(SlotState::FREE, std::memory_order_relaxed);
    }

    vec.clear();
}

static OsdDetSlot* acquire_free_slot_from(std::vector<std::unique_ptr<OsdDetSlot>>& vec)
{
    for (auto& sp : vec) {
        auto& s = *sp;
        SlotState st = s.state.load(std::memory_order_acquire);

        if (st == SlotState::FREE) {
            SlotState expected = SlotState::FREE;
            if (s.state.compare_exchange_strong(expected,
                                                SlotState::INFER_PENDING,
                                                std::memory_order_acq_rel,
                                                std::memory_order_acquire)) {
                return sp.get();
            }
            continue;
        }

        if (st == SlotState::INFER_PENDING) {
            cudaError_t q = cudaEventQuery(s.ready_event);
            if (q == cudaSuccess) {
                osd_release_slot(s);

                SlotState expected = SlotState::FREE;
                if (s.state.compare_exchange_strong(expected,
                                                    SlotState::INFER_PENDING,
                                                    std::memory_order_acq_rel,
                                                    std::memory_order_acquire)) {
                    return sp.get();
                }
            }
        }
    }

    return nullptr;
}

static OsdDetSlot* find_slot_by_frame_seq_from(std::vector<std::unique_ptr<OsdDetSlot>>& vec,
                                               std::uint64_t frame_seq)
{
    for (auto& sp : vec) {
        auto& s = *sp;
        SlotState st = s.state.load(std::memory_order_acquire);
        if (st != SlotState::FREE && s.frame_seq == frame_seq) {
            return sp.get();
        }
    }

    return nullptr;
}

} // namespace

bool osd_init_slots(OsdShared& shared, int slot_count, int max_det)
{
    if (slot_count <= 0 || max_det <= 0) return false;

    std::unique_lock<std::shared_mutex> lk(shared.mtx);

    if (!init_slot_vector(shared.slots, slot_count, max_det)) {
        return false;
    }

    if (!init_slot_vector(shared.track_slots, slot_count, max_det)) {
        return false;
    }

    {
        std::lock_guard<std::mutex> lk_latest(shared.latest_mtx);
        shared.latest_det_host.clear();
        shared.latest_det_host.resize(static_cast<size_t>(max_det));
        shared.latest_det_count = 0;
        shared.latest_frame_seq = 0;
        shared.latest_valid = false;
        shared.latest_empty_streak = 0;
    }

    {
        std::lock_guard<std::mutex> lk_latest_track(shared.latest_track_mtx);
        shared.latest_track_det_host.clear();
        shared.latest_track_det_host.resize(static_cast<size_t>(max_det));
        shared.latest_track_det_count = 0;
        shared.latest_track_frame_seq = 0;
        shared.latest_track_valid = false;
        shared.latest_track_empty_streak = 0;
    }

    return true;
}

void osd_destroy_slots(OsdShared& shared)
{
    std::unique_lock<std::shared_mutex> lk(shared.mtx);

    destroy_slot_vector(shared.slots);
    destroy_slot_vector(shared.track_slots);

    {
        std::lock_guard<std::mutex> lk_latest(shared.latest_mtx);
        shared.latest_det_host.clear();
        shared.latest_det_count = 0;
        shared.latest_frame_seq = 0;
        shared.latest_valid = false;
        shared.latest_empty_streak = 0;
    }

    {
        std::lock_guard<std::mutex> lk_latest_track(shared.latest_track_mtx);
        shared.latest_track_det_host.clear();
        shared.latest_track_det_count = 0;
        shared.latest_track_frame_seq = 0;
        shared.latest_track_valid = false;
        shared.latest_track_empty_streak = 0;
    }
}

// -------------------- detect path --------------------

OsdDetSlot* osd_acquire_free_slot(OsdShared& shared)
{
    std::shared_lock<std::shared_mutex> lk(shared.mtx);
    return acquire_free_slot_from(shared.slots);
}

OsdDetSlot* osd_find_slot_by_frame_seq(OsdShared& shared, std::uint64_t frame_seq)
{
    std::shared_lock<std::shared_mutex> lk(shared.mtx);
    return find_slot_by_frame_seq_from(shared.slots, frame_seq);
}

void osd_update_latest_result(OsdShared& shared,
                              std::uint64_t frame_seq,
                              const Detection* det,
                              int count)
{
    std::lock_guard<std::mutex> lk(shared.latest_mtx);

    if (count > 0 && det != nullptr) {
        if (shared.latest_det_host.size() < static_cast<size_t>(count)) {
            shared.latest_det_host.resize(static_cast<size_t>(count));
        }

        std::memcpy(shared.latest_det_host.data(),
                    det,
                    static_cast<size_t>(count) * sizeof(Detection));

        shared.latest_det_count = count;
        shared.latest_frame_seq = frame_seq;
        shared.latest_valid = true;
        shared.latest_empty_streak = 0;
        return;
    }

    shared.latest_empty_streak++;

    if (shared.latest_empty_streak >= shared.latest_clear_after_empty) {
        shared.latest_valid = false;
        shared.latest_det_count = 0;
        shared.latest_frame_seq = frame_seq;
    }
}

bool osd_copy_latest_result(OsdShared& shared,
                            std::uint64_t frame_seq,
                            std::vector<Detection>& out,
                            std::uint64_t* out_result_seq)
{
    std::lock_guard<std::mutex> lk(shared.latest_mtx);

    if (!shared.latest_valid) return false;
    if (shared.latest_det_count <= 0) return false;
    if (shared.latest_frame_seq == 0) return false;

    if (shared.latest_frame_seq > frame_seq) return false;

    if (frame_seq > shared.latest_frame_seq &&
        (frame_seq - shared.latest_frame_seq) >
            static_cast<std::uint64_t>(shared.latest_max_fallback_frames)) {
        return false;
    }

    out.resize(static_cast<size_t>(shared.latest_det_count));
    std::memcpy(out.data(),
                shared.latest_det_host.data(),
                static_cast<size_t>(shared.latest_det_count) * sizeof(Detection));

    if (out_result_seq) {
        *out_result_seq = shared.latest_frame_seq;
    }
    return true;
}

void osd_clear_latest_det(OsdShared& shared)
{
    std::lock_guard<std::mutex> lk(shared.latest_mtx);
    shared.latest_valid = false;
    shared.latest_det_count = 0;
    shared.latest_frame_seq = 0;
    shared.latest_empty_streak = 0;
    shared.latest_det_host.clear();
}

// -------------------- track path --------------------

OsdDetSlot* osd_acquire_free_track_slot(OsdShared& shared)
{
    std::shared_lock<std::shared_mutex> lk(shared.mtx);
    return acquire_free_slot_from(shared.track_slots);
}

OsdDetSlot* osd_find_track_slot_by_frame_seq(OsdShared& shared, std::uint64_t frame_seq)
{
    std::shared_lock<std::shared_mutex> lk(shared.mtx);
    return find_slot_by_frame_seq_from(shared.track_slots, frame_seq);
}

void osd_update_latest_track_result(OsdShared& shared,
                                    std::uint64_t frame_seq,
                                    const Detection* det,
                                    int count)
{
    std::lock_guard<std::mutex> lk(shared.latest_track_mtx);

    if (count > 0 && det != nullptr) {
        if (shared.latest_track_det_host.size() < static_cast<size_t>(count)) {
            shared.latest_track_det_host.resize(static_cast<size_t>(count));
        }

        std::memcpy(shared.latest_track_det_host.data(),
                    det,
                    static_cast<size_t>(count) * sizeof(Detection));

        shared.latest_track_det_count = count;
        shared.latest_track_frame_seq = frame_seq;
        shared.latest_track_valid = true;
        shared.latest_track_empty_streak = 0;
        return;
    }

    shared.latest_track_empty_streak++;

    if (shared.latest_track_empty_streak >= shared.latest_track_clear_after_empty) {
        shared.latest_track_valid = false;
        shared.latest_track_det_count = 0;
        shared.latest_track_frame_seq = frame_seq;
    }
}

bool osd_copy_latest_track_result(OsdShared& shared,
                                  std::uint64_t frame_seq,
                                  std::vector<Detection>& out,
                                  std::uint64_t* out_result_seq)
{
    std::lock_guard<std::mutex> lk(shared.latest_track_mtx);

    if (!shared.latest_track_valid) return false;
    if (shared.latest_track_det_count <= 0) return false;
    if (shared.latest_track_frame_seq == 0) return false;

    if (shared.latest_track_frame_seq > frame_seq) return false;

    if (frame_seq > shared.latest_track_frame_seq &&
        (frame_seq - shared.latest_track_frame_seq) >
            static_cast<std::uint64_t>(shared.latest_track_max_fallback_frames)) {
        return false;
    }

    out.resize(static_cast<size_t>(shared.latest_track_det_count));
    std::memcpy(out.data(),
                shared.latest_track_det_host.data(),
                static_cast<size_t>(shared.latest_track_det_count) * sizeof(Detection));

    if (out_result_seq) {
        *out_result_seq = shared.latest_track_frame_seq;
    }
    return true;
}

void osd_clear_latest_track_det(OsdShared& shared)
{
    std::lock_guard<std::mutex> lk(shared.latest_track_mtx);
    shared.latest_track_valid = false;
    shared.latest_track_det_count = 0;
    shared.latest_track_frame_seq = 0;
    shared.latest_track_empty_streak = 0;
    shared.latest_track_det_host.clear();
}

// -------------------- common --------------------

void osd_release_slot(OsdDetSlot& slot)
{
    slot.frame_seq = 0;

    if (slot.det_count_host) {
        *slot.det_count_host = 0;
    }

    slot.state.store(SlotState::FREE, std::memory_order_release);
}

void osd_reset_all_results(OsdShared& shared)
{
    {
        std::unique_lock<std::shared_mutex> lk(shared.mtx);

        for (auto& sp : shared.slots) {
            if (!sp) continue;
            osd_release_slot(*sp);
        }

        for (auto& sp : shared.track_slots) {
            if (!sp) continue;
            osd_release_slot(*sp);
        }
    }

    osd_clear_latest_det(shared);
    osd_clear_latest_track_det(shared);
}

} // namespace k180::osd