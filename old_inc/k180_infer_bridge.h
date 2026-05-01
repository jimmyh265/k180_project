#pragma once

#include <vector>

#include "types.h"              // Detection
#include "k180_osd_shared.h"

namespace k180::osd {

// 將單一 Detection 轉成 OsdBox
OsdBox detection_to_osd_box(const Detection& d);

// 將一批 Detection 轉成 OSD boxes
std::vector<OsdBox> detections_to_osd_boxes(const std::vector<Detection>& dets);

// 發布 detections 到 shared
void publish_detections_to_osd(OsdShared& shared,
                               std::uint64_t frame_seq,
                               const std::vector<Detection>& dets);

} // namespace k180::osd
