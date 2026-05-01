// k180_frame_tag_meta.h
#pragma once

#include <gst/gst.h>
#include <cstdint>

G_BEGIN_DECLS

typedef struct _K180FrameTagMeta {
    GstMeta meta;
    std::uint64_t frame_seq;
} K180FrameTagMeta;

// meta API type
GType k180_frame_tag_meta_api_get_type(void);
// meta info
const GstMetaInfo* k180_frame_tag_meta_get_info(void);

#define K180_FRAME_TAG_META_API_TYPE (k180_frame_tag_meta_api_get_type())
#define K180_FRAME_TAG_META_INFO     (k180_frame_tag_meta_get_info())

// add/get helpers
K180FrameTagMeta* k180_buffer_add_frame_tag_meta(GstBuffer* buffer, std::uint64_t frame_seq);
K180FrameTagMeta* k180_buffer_get_frame_tag_meta(GstBuffer* buffer);

G_END_DECLS
