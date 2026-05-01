// k180_tracking.cpp
#include "k180_tracking.h"

#include <algorithm>

namespace k180::runtime::tracking {

// -------------------------
// Globals (defined exactly once here)
// -------------------------
std::unordered_map<int, ObjectTrackData> g_tracks_by_id;
// std::vector<ObjectTrackData>             g_untracked_buffer;

std::mutex g_tracks_mutex;
std::mutex g_untracked_mutex;

// -------------------------
// Helpers
// -------------------------
std::vector<Object> convertDetectionsToObjects(const std::vector<Detection>& detections)
{
    std::vector<Object> objects;
    objects.reserve(detections.size());
    for (const auto& det : detections)
    {
        Object obj;
        obj.rect  = cv::Rect2f(det.bbox[0], det.bbox[1], det.bbox[2], det.bbox[3]);
        obj.label = det.class_id;
        obj.prob  = det.conf;
        objects.push_back(obj);
    }
    return objects;
}

static inline float IoU_tlwh_vs_tlwh(const float a[4], const float b[4])
{
    float ax1 = a[0], ay1 = a[1];
    float ax2 = a[0] + a[2], ay2 = a[1] + a[3];

    float bx1 = b[0], by1 = b[1];
    float bx2 = b[0] + b[2], by2 = b[1] + b[3];

    float xx1 = std::max(ax1, bx1);
    float yy1 = std::max(ay1, by1);
    float xx2 = std::min(ax2, bx2);
    float yy2 = std::min(ay2, by2);

    float inter = std::max(0.0f, xx2 - xx1) * std::max(0.0f, yy2 - yy1);
    float areaA = (ax2 - ax1) * (ay2 - ay1);
    float areaB = (bx2 - bx1) * (by2 - by1);
    float uni = areaA + areaB - inter;

    return (uni > 0.0f) ? (inter / uni) : 0.0f;
}

float IoU_tlwh_vs_bbox(const std::vector<float>& tlwh, const Detection& det)
{
    if (tlwh.size() < 4) return 0.0f;
    // Assumes det.bbox is TLWH (same format). If your det.bbox is XYXY, change this function accordingly.
	// 這邊要檢查，我自己的bbox裡面，tlwh的順序!!!
    // det.bbox 假設可用 [] 取到 0..3（array / vector / std::array 都可）
    float a[4] = { tlwh[0], tlwh[1], tlwh[2], tlwh[3] };
    float b[4] = { det.bbox[0], det.bbox[1], det.bbox[2], det.bbox[3] };

    // 若 det.bbox 不是 TLWH（而是 XYXY），這裡要另外轉換
    return IoU_tlwh_vs_tlwh(a, b);
}

// （可選）float[4] tlwh 版
float IoU_tlwh_vs_bbox(const float tlwh[4], const Detection& det)
{
    float b[4] = { det.bbox[0], det.bbox[1], det.bbox[2], det.bbox[3] };
    return IoU_tlwh_vs_tlwh(tlwh, b);
}

static inline Detection make_detection_from_track(const STrack& track)
{
    Detection d{};

    d.bbox[0] = track.tlwh[0];
    d.bbox[1] = track.tlwh[1];
    d.bbox[2] = track.tlwh[2];
    d.bbox[3] = track.tlwh[3];

    d.conf = track.score;
    d.class_id = 0.0f;   // 若 STrack 沒保存 class_id，先給 0

    return d;
}

void updateTrackTable(
    const std::vector<Detection>& res,
    const std::vector<STrack>& output_tracks)
{
    const float IOU_THRESHOLD = 0.3f;
    const int   MAX_LOST      = 5;

    std::vector<char> res_used(res.size(), 0);
    std::unordered_map<int, ObjectTrackData> updated_tracks;
    updated_tracks.reserve(output_tracks.size());

    // snapshot old tracks under lock
    std::unordered_map<int, ObjectTrackData> prev_tracks;
    {
        std::lock_guard<std::mutex> lock(g_tracks_mutex);
        prev_tracks = g_tracks_by_id;
    }

    // --- 1) build updated_tracks from output_tracks ---
    for (const auto& track : output_tracks)
    {
        ObjectTrackData obj{};
        auto it = prev_tracks.find(track.track_id);

        if (it != prev_tracks.end())
        {
            // existing: start from previous snapshot
            obj = it->second;

            float prev_x = obj.tracker.tlwh[0];
            float prev_y = obj.tracker.tlwh[1];

            // update tracker tlwh
            obj.tracker.tlwh[0] = track.tlwh[0];
            obj.tracker.tlwh[1] = track.tlwh[1];
            obj.tracker.tlwh[2] = track.tlwh[2];
            obj.tracker.tlwh[3] = track.tlwh[3];

            obj.tracker.age += 1.0f;
            obj.tracker.checked = true;
            obj.tracker.lost_count = 0;

            // velocity based on tracker bbox
            obj.tracker.velocity[0] = track.tlwh[0] - prev_x;
            obj.tracker.velocity[1] = track.tlwh[1] - prev_y;

            int   best_idx = -1;
            float best_iou = 0.0f;

            for (size_t i = 0; i < res.size(); ++i)
            {
                if (res_used[i]) continue;

                float iou = IoU_tlwh_vs_bbox(track.tlwh, res[i]);
                if (iou > best_iou) {
                    best_iou = iou;
                    best_idx = static_cast<int>(i);
                }
            }

            if (best_idx >= 0 && best_iou >= IOU_THRESHOLD) {
                obj.det = res[best_idx];
                res_used[best_idx] = 1;
            } else {
                obj.det = make_detection_from_track(track);
            }
        }
        else
        {
            // new object
            obj.tracker.track_id = track.track_id;
            obj.tracker.age = 1.0f;
            obj.tracker.checked = true;
            obj.tracker.velocity[0] = 0.0f;
            obj.tracker.velocity[1] = 0.0f;
            obj.tracker.lost_count = 0;

            obj.tracker.tlwh[0] = track.tlwh[0];
            obj.tracker.tlwh[1] = track.tlwh[1];
            obj.tracker.tlwh[2] = track.tlwh[2];
            obj.tracker.tlwh[3] = track.tlwh[3];

            int   best_idx = -1;
            float best_iou = 0.0f;

            for (size_t i = 0; i < res.size(); ++i)
            {
                if (res_used[i]) continue;

                float iou = IoU_tlwh_vs_bbox(track.tlwh, res[i]);
                if (iou > best_iou) {
                    best_iou = iou;
                    best_idx = static_cast<int>(i);
                }
            }

            if (best_idx >= 0 && best_iou >= IOU_THRESHOLD) {
                obj.det = res[best_idx];
                res_used[best_idx] = 1;
            } else {
                obj.det = make_detection_from_track(track);
            }
        }

        updated_tracks[track.track_id] = obj;
    }

    // --- 2) push unmatched detections to untracked buffer ---
    {
        std::lock_guard<std::mutex> lock(g_untracked_mutex);
        for (size_t i = 0; i < res.size(); ++i)
        {
            if (res_used[i]) continue;

            ObjectTrackData obj{};
            obj.det = res[i];
            obj.tracker.track_id = -1;
            // 你目前這行是註解掉的，就維持原狀
            // g_untracked_buffer.push_back(obj);
        }
    }

    // --- 3) update global table with lost aging ---
    {
        std::lock_guard<std::mutex> lock(g_tracks_mutex);

        // mark all existing unchecked
        for (auto& kv : g_tracks_by_id) {
            kv.second.tracker.checked = false;
        }

        // apply current frame updates
        for (const auto& kv : updated_tracks) {
            g_tracks_by_id[kv.first] = kv.second;
        }

        // age/remove tracks that were not updated this frame
        std::vector<int> to_remove;
        to_remove.reserve(g_tracks_by_id.size());

        for (auto& kv : g_tracks_by_id) {
            if (!kv.second.tracker.checked) {
                kv.second.tracker.lost_count++;
                if (kv.second.tracker.lost_count > MAX_LOST) {
                    to_remove.push_back(kv.first);
                }
            }
        }

        for (int id : to_remove) {
            g_tracks_by_id.erase(id);
        }
    }
}

std::vector<Detection> buildTrackRenderResult(
    const std::vector<STrack>& output_tracks)
{
    std::vector<Detection> res_track;
    res_track.reserve(output_tracks.size());

    for (const auto& track : output_tracks)
    {
        res_track.push_back(make_detection_from_track(track));
    }

    return res_track;
}

} // namespace k180::rt::tracking
