#pragma once
// k180_tracking.h
// ------------------------------------------------------------
// Tracking runtime state & helpers (K180)
// Namespace: k180::rt::tracking
//
// NOTE: You MUST adjust the includes below to match your project.
// Required external types:
//   - Detection  : must have bbox[0..3], class_id, conf
//   - STrack     : must have track_id, score, tlwh[0..3]
//   - Object     : must have rect (cv::Rect2f), label, prob
// ------------------------------------------------------------


// ------------------------------------------------------------
#include "BYTETracker.h"	// struct STrack
#include "types.h"	// struct Detection
#include <unordered_map>
#include <vector>
#include <mutex>
#include <cstdint>

// OpenCV Rect2f
#include <opencv2/core/types.hpp>

namespace k180::runtime::tracking {

// -------------------------
// Data types
// -------------------------
struct TrackerInfo {
    int   track_id = -1;                 // tracker ID
    float velocity[2] = {0.0f, 0.0f};    // vx, vy (pixel/frame or your unit)
    float age = 0.0f;                    // lifetime (frames)
    bool  checked = false;               // used for aging/removal
    int   lost_count = 0;                // missed frames count
    float tlwh[4] = {0.0f, 0.0f, 0.0f, 0.0f}; // tracker bbox (smooth/KF)
};

struct StereoInfo {
    float z = 0.0f;        // depth (m)
    float h_angle = 0.0f;  // deg
    float v_angle = 0.0f;  // deg
};

struct GpsInfo {
    float lng = 0.0f;
    float lat = 0.0f;
};

struct ObjectTrackData {
    Detection   det;       // detection (or fallback bbox from track)
    TrackerInfo tracker;   // tracking state
    StereoInfo  stereo;    // stereo info
    GpsInfo     gps;       // GPS info
};

// -------------------------
// Runtime containers + locks
// -------------------------
extern std::unordered_map<int, ObjectTrackData> g_tracks_by_id;
// extern std::vector<ObjectTrackData>             g_untracked_buffer;

extern std::mutex g_tracks_mutex;
extern std::mutex g_untracked_mutex;

// -------------------------
// Helpers
// -------------------------
std::vector<Object> convertDetectionsToObjects(const std::vector<Detection>& detections);

// IoU between track tlwh[4] and detection bbox-like (must support operator[] with indices 0..3)
float IoU_tlwh_vs_bbox(const std::vector<float>& tlwh, const Detection& det);
float IoU_tlwh_vs_bbox(const float tlwh[4], const Detection& det);

// 新版：更新全域 tracking table
void updateTrackTable(
    const std::vector<Detection>& res,
    const std::vector<STrack>& output_tracks);

// 新版：建立本幀 render 用黃框
std::vector<Detection> buildTrackRenderResult(
    const std::vector<STrack>& output_tracks);
} // namespace k180::rt::tracking
