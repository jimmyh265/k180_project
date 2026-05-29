// k180_frame_tag_meta.cpp
#include "k180_frame_tag_meta.h"

static gboolean k180_frame_tag_meta_init(GstMeta* meta, gpointer params, GstBuffer* buffer)
{
    (void)params;
    (void)buffer;

    auto* m = reinterpret_cast<K180FrameTagMeta*>(meta);
    m->frame_seq = 0;
    return TRUE;
}

static gboolean k180_frame_tag_meta_transform(GstBuffer* dest,
                                              GstMeta* meta,
                                              GstBuffer* src,
                                              GQuark type,
                                              gpointer data)
{
    (void)src;
    (void)type;
    (void)data;

    auto* src_meta = reinterpret_cast<K180FrameTagMeta*>(meta);
    auto* dst_meta = reinterpret_cast<K180FrameTagMeta*>(
        gst_buffer_add_meta(dest, K180_FRAME_TAG_META_INFO, nullptr));
    if (!dst_meta) return FALSE;

    dst_meta->frame_seq = src_meta->frame_seq;
    return TRUE;
}

GType k180_frame_tag_meta_api_get_type(void)
{
    static GType type = 0;
    static const gchar* tags[] = { "k180-frame-tag-meta", nullptr };

    if (g_once_init_enter(&type)) {
        GType t = gst_meta_api_type_register("K180FrameTagMetaAPI", tags);
        g_once_init_leave(&type, t);
    }
    return type;
}

const GstMetaInfo* k180_frame_tag_meta_get_info(void)
{
    static const GstMetaInfo* meta_info = nullptr;

    if (g_once_init_enter((gsize*)&meta_info)) {
        const GstMetaInfo* mi = gst_meta_register(
            K180_FRAME_TAG_META_API_TYPE,
            "K180FrameTagMeta",
            sizeof(K180FrameTagMeta),
            k180_frame_tag_meta_init,
            nullptr,
            k180_frame_tag_meta_transform);
        g_once_init_leave((gsize*)&meta_info, (gsize)mi);
    }

    return meta_info;
}

K180FrameTagMeta* k180_buffer_add_frame_tag_meta(GstBuffer* buffer,
                                                 std::uint64_t frame_seq)
{
    if (!buffer) return nullptr;

    auto* meta = reinterpret_cast<K180FrameTagMeta*>(
        gst_buffer_add_meta(buffer, K180_FRAME_TAG_META_INFO, nullptr));
    if (!meta) return nullptr;

    meta->frame_seq = frame_seq;
    return meta;
}

K180FrameTagMeta* k180_buffer_get_frame_tag_meta(GstBuffer* buffer)
{
    if (!buffer) return nullptr;

    return reinterpret_cast<K180FrameTagMeta*>(
        gst_buffer_get_meta(buffer, K180_FRAME_TAG_META_API_TYPE));
}