// k180_osd_meta.h
#pragma once

#include <gst/gst.h>

#include "nvdsmeta.h"      // declares NvDsBatchMeta / NvDs* types
#include "types.h"	// yolo_inc/types.h : Detection, Object
#include "k180_osd_shared.h"

namespace k180::osd {

enum class BoxColor {
    White,
    Yellow,
};

// add one Detection as one NvDsObjectMeta to a frame
void osd_add_detection_meta(NvDsBatchMeta* batch_meta,
                            NvDsFrameMeta* frame_meta,
                            const Detection& d,
							BoxColor color = BoxColor::White);

// attach probe to nvstreammux src pad (element name = "mux")
bool attach_osd_probe_to_mux(GstElement* pipeline, OsdShared* shared);
// bool attach_nvtracker_probe(GstElement* pipeline, OsdShared* shared);
} // namespace k180::osd