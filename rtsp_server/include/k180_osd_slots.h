// k180_osd_slots.h
#pragma once

#include <cstdint>
#include <vector>

#include "k180_osd_shared.h"

namespace k180::osd {

// allocate/free all slot resources
bool osd_init_slots(OsdShared& shared, int slot_count, int max_det);
void osd_destroy_slots(OsdShared& shared);

// -------------------- detect path --------------------

// acquire one free slot for a new inference request
OsdDetSlot* osd_acquire_free_slot(OsdShared& shared);

// exact frame_seq lookup
OsdDetSlot* osd_find_slot_by_frame_seq(OsdShared& shared, std::uint64_t frame_seq);

// inference-complete side updates latest fallback snapshot
void osd_update_latest_result(OsdShared& shared,
                              std::uint64_t frame_seq,
                              const Detection* det,
                              int count);

// probe side copies latest fallback snapshot
bool osd_copy_latest_result(OsdShared& shared,
                            std::uint64_t frame_seq,
                            std::vector<Detection>& out,
                            std::uint64_t* out_result_seq = nullptr);

// -------------------- track path --------------------

// acquire one free slot for a new tracking render request
OsdDetSlot* osd_acquire_free_track_slot(OsdShared& shared);

// exact frame_seq lookup for tracking result
OsdDetSlot* osd_find_track_slot_by_frame_seq(OsdShared& shared, std::uint64_t frame_seq);

// tracking-complete side updates latest fallback snapshot
void osd_update_latest_track_result(OsdShared& shared,
                                    std::uint64_t frame_seq,
                                    const Detection* det,
                                    int count);

// probe side copies latest fallback snapshot for tracking result
bool osd_copy_latest_track_result(OsdShared& shared,
                                  std::uint64_t frame_seq,
                                  std::vector<Detection>& out,
                                  std::uint64_t* out_result_seq = nullptr);

// -------------------- common --------------------

void osd_release_slot(OsdDetSlot& slot);
void osd_clear_latest_det(OsdShared& shared);
void osd_clear_latest_track_det(OsdShared& shared);
void osd_reset_all_results(OsdShared& shared);

} // namespace k180::osd