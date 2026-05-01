// k180_osd_meta.cpp
#include "k180_osd_meta.h"

#include <cstdio>

namespace k180::osd {

static inline void fill_rect_params_from_detection(NvOSD_RectParams& r,
                                                   const Detection& d,
                                                   BoxColor color)
{
    r.left = d.bbox[0];
    r.top = d.bbox[1];
    r.width = d.bbox[2];
    r.height = d.bbox[3];

    r.border_width = 4;
    r.has_bg_color = 0;

    if (color == BoxColor::Yellow) {
        r.border_color.red   = 1.0f;
        r.border_color.green = 1.0f;
        r.border_color.blue  = 0.0f;
        r.border_color.alpha = 1.0f;
    } else {
        r.border_color.red   = 1.0f;
        r.border_color.green = 1.0f;
        r.border_color.blue  = 1.0f;
        r.border_color.alpha = 1.0f;
    }
}

void osd_add_detection_meta(NvDsBatchMeta* batch_meta,
                            NvDsFrameMeta* frame_meta,
                            const Detection& d,
                            BoxColor color)
{
    if (!batch_meta || !frame_meta) return;

    NvDsObjectMeta* obj_meta = nvds_acquire_obj_meta_from_pool(batch_meta);
    if (!obj_meta) return;

    obj_meta->unique_component_id = 1;
    obj_meta->class_id = static_cast<int>(d.class_id);
    obj_meta->object_id = UNTRACKED_OBJECT_ID;
    obj_meta->confidence = d.conf;
    obj_meta->parent = nullptr;

    fill_rect_params_from_detection(obj_meta->rect_params, d, color);

    obj_meta->detector_bbox_info.org_bbox_coords.left   = d.bbox[0];
    obj_meta->detector_bbox_info.org_bbox_coords.top    = d.bbox[1];
    obj_meta->detector_bbox_info.org_bbox_coords.width  = d.bbox[2];
    obj_meta->detector_bbox_info.org_bbox_coords.height = d.bbox[3];

    std::snprintf(obj_meta->obj_label, MAX_LABEL_SIZE, "%d", (int)d.class_id);
    obj_meta->text_params.display_text = nullptr;

    nvds_add_obj_meta_to_frame(frame_meta, obj_meta, nullptr);
}

} // namespace k180::osd