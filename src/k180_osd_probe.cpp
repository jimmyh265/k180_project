// k180_osd_probe.cpp
#include "k180_osd_meta.h"

#include "k180_frame_tag_meta.h"
#include "k180_osd_slots.h"
#include "k180_tracking.h"

#include "gstnvdsmeta.h"
#include "nvdsmeta.h"

#include <cstdio>
#include <vector>

#include <chrono>

namespace k180::osd {
struct ProbeRateStat {
    std::atomic<uint64_t> buf{0};
    std::atomic<uint64_t> no_tag{0};
    std::atomic<uint64_t> no_batch{0};

    std::atomic<uint64_t> exact_found{0};
    std::atomic<uint64_t> exact_ready{0};
    std::atomic<uint64_t> exact_not_ready{0};
    std::atomic<uint64_t> exact_error{0};

    std::atomic<uint64_t> attach_ok{0};
    std::atomic<uint64_t> attach_det_sum{0};

    std::atomic<uint64_t> fb_used{0};
    std::atomic<uint64_t> fb_miss{0};
    std::atomic<uint64_t> fb_det_sum{0};

    std::atomic<uint64_t> not_found{0};

    std::atomic<uint64_t> last_log_ms{0};
};

static ProbeRateStat g_probe_rate;

static inline uint64_t mono_ms_now_probe()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(
        steady_clock::now().time_since_epoch()).count();
}

static inline void probe_rate_maybe_log()
{
    uint64_t now = mono_ms_now_probe();
    uint64_t last = g_probe_rate.last_log_ms.load(std::memory_order_relaxed);

    if (last == 0) {
        g_probe_rate.last_log_ms.store(now, std::memory_order_relaxed);
        return;
    }

    if (now - last < 1000) return;

    if (!g_probe_rate.last_log_ms.compare_exchange_strong(
            last, now, std::memory_order_relaxed)) {
        return;
    }

    uint64_t buf            = g_probe_rate.buf.exchange(0, std::memory_order_relaxed);
    uint64_t no_tag         = g_probe_rate.no_tag.exchange(0, std::memory_order_relaxed);
    uint64_t no_batch       = g_probe_rate.no_batch.exchange(0, std::memory_order_relaxed);

    uint64_t exact_found    = g_probe_rate.exact_found.exchange(0, std::memory_order_relaxed);
    uint64_t exact_ready    = g_probe_rate.exact_ready.exchange(0, std::memory_order_relaxed);
    uint64_t exact_not_ready= g_probe_rate.exact_not_ready.exchange(0, std::memory_order_relaxed);
    uint64_t exact_error    = g_probe_rate.exact_error.exchange(0, std::memory_order_relaxed);

    uint64_t attach_ok      = g_probe_rate.attach_ok.exchange(0, std::memory_order_relaxed);
    uint64_t attach_det_sum = g_probe_rate.attach_det_sum.exchange(0, std::memory_order_relaxed);

    uint64_t fb_used        = g_probe_rate.fb_used.exchange(0, std::memory_order_relaxed);
    uint64_t fb_miss        = g_probe_rate.fb_miss.exchange(0, std::memory_order_relaxed);
    uint64_t fb_det_sum     = g_probe_rate.fb_det_sum.exchange(0, std::memory_order_relaxed);

    uint64_t not_found      = g_probe_rate.not_found.exchange(0, std::memory_order_relaxed);

    double avg_exact_det = (exact_ready > 0)
        ? (double)attach_det_sum / (double)exact_ready
        : 0.0;

    double avg_fb_det = (fb_used > 0)
        ? (double)fb_det_sum / (double)fb_used
        : 0.0;

    fprintf(stderr,
        "[MUX-PROBE] buf=%llu/s attach=%llu/s "
        "exact_found=%llu exact_ready=%llu exact_not_ready=%llu exact_error=%llu "
        "fb_used=%llu fb_miss=%llu "
        "no_tag=%llu no_batch=%llu not_found=%llu "
        "avg_exact_det=%.2f avg_fb_det=%.2f\n",
        (unsigned long long)buf,
        (unsigned long long)attach_ok,
        (unsigned long long)exact_found,
        (unsigned long long)exact_ready,
        (unsigned long long)exact_not_ready,
        (unsigned long long)exact_error,
        (unsigned long long)fb_used,
        (unsigned long long)fb_miss,
        (unsigned long long)no_tag,
        (unsigned long long)no_batch,
        (unsigned long long)not_found,
        avg_exact_det,
        avg_fb_det
    );
}

static bool copy_exact_slot_if_ready(OsdDetSlot* slot, std::vector<Detection>& out)
{
    out.clear();
    if (!slot) return false;

    cudaError_t q = cudaEventQuery(slot->ready_event);
    if (q != cudaSuccess) {
        return false;
    }

    int count = 0;
    if (slot->det_count_host) {
        count = *slot->det_count_host;
    }

    if (count <= 0 || !slot->det_host) {
        return false;
    }

    out.resize((size_t)count);
    std::memcpy(out.data(), slot->det_host, (size_t)count * sizeof(Detection));
    return true;
}

GstPadProbeReturn mux_src_pad_probe(GstPad* pad,
                                    GstPadProbeInfo* info,
                                    gpointer user_data)
{
    (void)pad;

    if (!(info->type & GST_PAD_PROBE_TYPE_BUFFER)) {
        return GST_PAD_PROBE_OK;
    }

    g_probe_rate.buf.fetch_add(1, std::memory_order_relaxed);
    // probe_rate_maybe_log();

    auto* shared = static_cast<OsdShared*>(user_data);
    if (!shared) {
        return GST_PAD_PROBE_OK;
    }

    GstBuffer* buf = GST_PAD_PROBE_INFO_BUFFER(info);
    if (!buf) {
        return GST_PAD_PROBE_OK;
    }

    auto* tag = k180_buffer_get_frame_tag_meta(buf);
    if (!tag) {
        shared->probe_miss_not_found.fetch_add(1, std::memory_order_relaxed);
        g_probe_rate.no_tag.fetch_add(1, std::memory_order_relaxed);
        return GST_PAD_PROBE_OK;
    }

    const std::uint64_t frame_seq = tag->frame_seq;

    NvDsBatchMeta* batch_meta = gst_buffer_get_nvds_batch_meta(buf);
    if (!batch_meta) {
        g_probe_rate.no_batch.fetch_add(1, std::memory_order_relaxed);
        return GST_PAD_PROBE_OK;
    }

    std::vector<Detection> det_boxes;
    std::vector<Detection> track_boxes;

    bool det_attached = false;
    bool track_attached = false;

    // --------------------------------------------------
    // 1) detect exact -> detect latest
    // --------------------------------------------------
    OsdDetSlot* exact_det_slot = osd_find_slot_by_frame_seq(*shared, frame_seq);
    if (exact_det_slot) {
        g_probe_rate.exact_found.fetch_add(1, std::memory_order_relaxed);

        cudaError_t q = cudaEventQuery(exact_det_slot->ready_event);
        if (q == cudaSuccess) {
            if (copy_exact_slot_if_ready(exact_det_slot, det_boxes)) {
                shared->probe_exact_ready_ok.fetch_add(1, std::memory_order_relaxed);
                g_probe_rate.exact_ready.fetch_add(1, std::memory_order_relaxed);
                g_probe_rate.attach_det_sum.fetch_add(
                    (uint64_t)det_boxes.size(), std::memory_order_relaxed);
                det_attached = true;
            }
            osd_release_slot(*exact_det_slot);
        } else if (q == cudaErrorNotReady) {
            g_probe_rate.exact_not_ready.fetch_add(1, std::memory_order_relaxed);
        } else {
            g_probe_rate.exact_error.fetch_add(1, std::memory_order_relaxed);
            osd_release_slot(*exact_det_slot);
        }
    }

    if (!det_attached) {
        std::uint64_t det_fallback_seq = 0;
        if (osd_copy_latest_result(*shared, frame_seq, det_boxes, &det_fallback_seq)) {
            shared->probe_fallback_used.fetch_add(1, std::memory_order_relaxed);
            g_probe_rate.fb_used.fetch_add(1, std::memory_order_relaxed);
            g_probe_rate.fb_det_sum.fetch_add(
                (uint64_t)det_boxes.size(), std::memory_order_relaxed);
            det_attached = !det_boxes.empty();
        } else {
            if (exact_det_slot) {
                shared->probe_miss_not_ready.fetch_add(1, std::memory_order_relaxed);
            } else {
                shared->probe_miss_not_found.fetch_add(1, std::memory_order_relaxed);
                g_probe_rate.not_found.fetch_add(1, std::memory_order_relaxed);
            }
            shared->probe_fallback_miss.fetch_add(1, std::memory_order_relaxed);
            g_probe_rate.fb_miss.fetch_add(1, std::memory_order_relaxed);
        }
    }

    // --------------------------------------------------
    // 2) track exact -> track latest
    // --------------------------------------------------
    OsdDetSlot* exact_track_slot = osd_find_track_slot_by_frame_seq(*shared, frame_seq);
    if (exact_track_slot) {
        cudaError_t q = cudaEventQuery(exact_track_slot->ready_event);
        if (q == cudaSuccess) {
            if (copy_exact_slot_if_ready(exact_track_slot, track_boxes)) {
                shared->track_probe_exact_ready_ok.fetch_add(1, std::memory_order_relaxed);
                track_attached = true;
            }
            osd_release_slot(*exact_track_slot);
        } else if (q == cudaErrorNotReady) {
            // 先留簡單版本，不額外加 rate log
        } else {
            osd_release_slot(*exact_track_slot);
        }
    }

    if (!track_attached) {
        std::uint64_t track_fallback_seq = 0;
        if (osd_copy_latest_track_result(*shared, frame_seq, track_boxes, &track_fallback_seq)) {
            shared->track_probe_fallback_used.fetch_add(1, std::memory_order_relaxed);
            track_attached = !track_boxes.empty();
        } else {
            if (exact_track_slot) {
                shared->track_probe_miss_not_ready.fetch_add(1, std::memory_order_relaxed);
            } else {
                shared->track_probe_miss_not_found.fetch_add(1, std::memory_order_relaxed);
            }
            shared->track_probe_fallback_miss.fetch_add(1, std::memory_order_relaxed);
        }
    }

    // --------------------------------------------------
    // 3) attach both
    // --------------------------------------------------
    if (!det_attached && !track_attached) {
        return GST_PAD_PROBE_OK;
    }

    nvds_acquire_meta_lock(batch_meta);

    for (NvDsMetaList* l_frame = batch_meta->frame_meta_list;
         l_frame != nullptr;
         l_frame = l_frame->next) {
        auto* frame_meta = static_cast<NvDsFrameMeta*>(l_frame->data);
        if (!frame_meta) continue;

        for (const auto& d : det_boxes) {
            osd_add_detection_meta(batch_meta, frame_meta, d, BoxColor::White);
        }

        for (const auto& d : track_boxes) {
            osd_add_detection_meta(batch_meta, frame_meta, d, BoxColor::Yellow);
        }
    }

    nvds_release_meta_lock(batch_meta);

    if (det_attached) {
        shared->probe_attach_ok.fetch_add(1, std::memory_order_relaxed);
        g_probe_rate.attach_ok.fetch_add(1, std::memory_order_relaxed);
    }

    if (track_attached) {
        shared->track_probe_attach_ok.fetch_add(1, std::memory_order_relaxed);
    }

    return GST_PAD_PROBE_OK;
}
#if 0
GstPadProbeReturn mux_src_pad_probe(GstPad* pad,
                                    GstPadProbeInfo* info,
                                    gpointer user_data)
{
    (void)pad;

    if (!(info->type & GST_PAD_PROBE_TYPE_BUFFER)) {
        return GST_PAD_PROBE_OK;
    }

    auto* shared = static_cast<OsdShared*>(user_data);
    if (!shared) {
        return GST_PAD_PROBE_OK;
    }

    GstBuffer* buf = GST_PAD_PROBE_INFO_BUFFER(info);
    if (!buf) {
        return GST_PAD_PROBE_OK;
    }

    auto* tag = k180_buffer_get_frame_tag_meta(buf);
    if (!tag) {
        shared->probe_miss_not_found.fetch_add(1, std::memory_order_relaxed);
        return GST_PAD_PROBE_OK;
    }

    const std::uint64_t frame_seq = tag->frame_seq;

    NvDsBatchMeta* batch_meta = gst_buffer_get_nvds_batch_meta(buf);
    if (!batch_meta) {
        return GST_PAD_PROBE_OK;
    }

    OsdDetSlot* exact_slot = osd_find_slot_by_frame_seq(*shared, frame_seq);
    if (exact_slot) {
        cudaError_t q = cudaEventQuery(exact_slot->ready_event);

		if (q == cudaSuccess) {
			int count = 0;
			if (exact_slot->det_count_host) {
				count = *exact_slot->det_count_host;
			}

			if (count > 0) {
				shared->probe_exact_ready_ok.fetch_add(1, std::memory_order_relaxed);

				nvds_acquire_meta_lock(batch_meta);

				for (NvDsMetaList* l_frame = batch_meta->frame_meta_list;
					 l_frame != nullptr;
					 l_frame = l_frame->next) {
					auto* frame_meta = static_cast<NvDsFrameMeta*>(l_frame->data);
					if (!frame_meta) continue;

					for (int i = 0; i < count; ++i) {
						osd_add_detection_meta(batch_meta, frame_meta, exact_slot->det_host[i]);
					}
				}

				nvds_release_meta_lock(batch_meta);
				shared->probe_attach_ok.fetch_add(1, std::memory_order_relaxed);
			}

			osd_release_slot(*exact_slot);
			return GST_PAD_PROBE_OK;
		}

        if (q != cudaErrorNotReady) {
            osd_release_slot(*exact_slot);
        }
    }

    std::vector<Detection> fallback_local;
    std::uint64_t fallback_seq = 0;

    if (!osd_copy_latest_result(*shared, frame_seq, fallback_local, &fallback_seq)) {
        if (exact_slot) {
            shared->probe_miss_not_ready.fetch_add(1, std::memory_order_relaxed);
        } else {
            shared->probe_miss_not_found.fetch_add(1, std::memory_order_relaxed);
        }
        shared->probe_fallback_miss.fetch_add(1, std::memory_order_relaxed);
        return GST_PAD_PROBE_OK;
    }

    shared->probe_fallback_used.fetch_add(1, std::memory_order_relaxed);

    nvds_acquire_meta_lock(batch_meta);

    for (NvDsMetaList* l_frame = batch_meta->frame_meta_list;
         l_frame != nullptr;
         l_frame = l_frame->next) {
        auto* frame_meta = static_cast<NvDsFrameMeta*>(l_frame->data);
        if (!frame_meta) continue;

        for (const auto& d : fallback_local) {
            osd_add_detection_meta(batch_meta, frame_meta, d);
        }
    }

    nvds_release_meta_lock(batch_meta);
    shared->probe_attach_ok.fetch_add(1, std::memory_order_relaxed);

    return GST_PAD_PROBE_OK;
}
#endif

#if 0
bool attach_osd_probe_to_mux(GstElement* pipeline, OsdShared* shared)
{
    if (!pipeline || !shared) return false;

    GstElement* rawsrc = gst_bin_get_by_name(GST_BIN(pipeline), "rawsrc");
    if (!rawsrc) {
        std::fprintf(stderr, "[OSD] cannot find appsrc by name=rawsrc\n");
        return false;
    }

    GstPad* srcpad = gst_element_get_static_pad(rawsrc, "src");
    if (!srcpad) {
        std::fprintf(stderr, "[OSD] cannot get rawsrc src pad\n");
        gst_object_unref(rawsrc);
        return false;
    }

    gulong probe_id = gst_pad_add_probe(
        srcpad,
        GST_PAD_PROBE_TYPE_BUFFER,
        mux_src_pad_probe,
        shared,
        nullptr
    );

    std::fprintf(stderr, "[OSD] rawsrc src probe_id=%lu\n", (unsigned long)probe_id);

    gst_object_unref(srcpad);
    gst_object_unref(rawsrc);
    return probe_id != 0;
}
#endif
#if 0
bool attach_osd_probe_to_mux(GstElement* pipeline, OsdShared* shared)
{
    if (!pipeline || !shared) return false;

    GstElement* osd = gst_bin_get_by_name(GST_BIN(pipeline), "OSD");
    if (!osd) {
        std::fprintf(stderr, "[OSD] cannot find nvdsosd by name=OSD\n");
        return false;
    }

    GstPad* sinkpad = gst_element_get_static_pad(osd, "sink");
    if (!sinkpad) {
        std::fprintf(stderr, "[OSD] cannot get OSD sink pad\n");
        gst_object_unref(osd);
        return false;
    }

    gulong probe_id = gst_pad_add_probe(
        sinkpad,
        GST_PAD_PROBE_TYPE_BUFFER,
        mux_src_pad_probe,
        shared,
        nullptr
    );

    std::fprintf(stderr, "[OSD] OSD sink probe_id=%lu\n", (unsigned long)probe_id);

    gst_object_unref(sinkpad);
    gst_object_unref(osd);
    return probe_id != 0;
}
#endif

bool attach_osd_probe_to_mux(GstElement* pipeline, OsdShared* shared)
{
    if (!pipeline || !shared) return false;
    GstElement* mux = gst_bin_get_by_name(GST_BIN(pipeline), "mux");
    if (!mux) {
        std::fprintf(stderr, "[OSD] cannot find nvstreammux by name=mux\n");
        return false;
    }
    GstPad* srcpad = gst_element_get_static_pad(mux, "src");
    if (!srcpad) {
        std::fprintf(stderr, "[OSD] cannot get mux src pad\n");
        gst_object_unref(mux);
        return false;
    }

	gulong probe_id = gst_pad_add_probe(
		srcpad,
		GST_PAD_PROBE_TYPE_BUFFER,
		mux_src_pad_probe,
		shared,
		nullptr
	);

	fprintf(stderr, "[OSD] mux src probe_id=%lu\n", (unsigned long)probe_id);

		std::fprintf(stderr, "[OSD] mux src pad probe attached\n");

		gst_object_unref(srcpad);
		gst_object_unref(mux);
		return true;
}

} // namespace k180::osd