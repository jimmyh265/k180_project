#include "k180_infer_bridge.h"
#include "k180_osd_publish.h"

namespace k180::osd {

OsdBox detection_to_osd_box(const Detection& d)
{
    OsdBox b;
    b.left = d.bbox[0];
    b.top = d.bbox[1];
    b.width = d.bbox[2];
    b.height = d.bbox[3];
    b.class_id = d.class_id;
    b.confidence = d.conf;
    b.track_id = 0;
    b.label = std::to_string(d.class_id);
    return b;
}

std::vector<OsdBox> detections_to_osd_boxes(const std::vector<Detection>& dets)
{
    std::vector<OsdBox> out;
    out.reserve(dets.size());

    for (const auto& d : dets) {
        out.push_back(detection_to_osd_box(d));
    }
    return out;
}

void publish_detections_to_osd(OsdShared& shared,
                               std::uint64_t frame_seq,
                               const std::vector<Detection>& dets)
{
    auto boxes = detections_to_osd_boxes(dets);
    osd_publish_boxes(shared, frame_seq, std::move(boxes));
}

} // namespace k180::osd
