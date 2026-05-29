#include "k180_osd_publish.h"

#include <utility>
#include <mutex>

namespace k180::osd {

void osd_publish_frame(OsdShared& shared, OsdFrameData&& frame)
{
    std::unique_lock<std::shared_mutex> lk(shared.mtx);

    // epoch 由 shared 端統一遞增，避免外部誤設
    frame.epoch = shared.latest.epoch + 1;
    shared.latest = std::move(frame);
}

void osd_publish_boxes(OsdShared& shared,
                       std::uint64_t frame_seq,
                       std::vector<OsdBox>&& boxes)
{
    OsdFrameData frame;
    frame.frame_seq = frame_seq;
    frame.boxes = std::move(boxes);
    osd_publish_frame(shared, std::move(frame));
}

OsdFrameData osd_snapshot(const OsdShared& shared)
{
    std::shared_lock<std::shared_mutex> lk(shared.mtx);
    return shared.latest;
}

} // namespace k180::osd
