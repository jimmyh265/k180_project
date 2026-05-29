#pragma once

#include "k180_osd_shared.h"

namespace k180::osd {

// 用整包資料直接發布
void osd_publish_frame(OsdShared& shared, OsdFrameData&& frame);

// 用 frame_seq + boxes 快速發布
void osd_publish_boxes(OsdShared& shared,
                       std::uint64_t frame_seq,
                       std::vector<OsdBox>&& boxes);

// 取得目前快照；probe 端只讀這份
OsdFrameData osd_snapshot(const OsdShared& shared);

} // namespace k180::osd
