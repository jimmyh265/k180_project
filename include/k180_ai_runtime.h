#pragma once

#include <atomic>
#include <cstdint>

namespace k180::ai {

enum class AiMode : int {
    OFF = 0,
    DETECT_ONLY = 1,
    DETECT_TO_TRACK = 2,
};

// 保留 enum，避免其他檔案若仍引用不致於壞掉。
// 但新語意下，state 不再代表 detector/tracker runtime 切換。
enum class AiRuntimeState : int {
    OFF = 0,
    RUNNING = 1,
};

struct AiPolicy {
    bool video_output_enabled = true;
    bool detector_enabled = false;
    bool tracking_enabled = false;   // DETECT_TO_TRACK 時代表會執行 ByteTrack update
};

struct AiRenderFlags {
    bool draw_det_boxes = false;     // 例如白框
    bool draw_track_boxes = false;   // 例如黃框
    bool highlight_selected_track = false;
};

struct ClickRequest {
    std::atomic<bool> pending{false};
    float x = 0.0f;
    float y = 0.0f;
};

struct SelectedTrack {
    std::atomic<bool> active{false};
    int track_id = -1;
    std::uint64_t selected_frame_seq = 0;
};

struct AiCounters {
    std::atomic<std::uint64_t> click_accept{0};
    std::atomic<std::uint64_t> click_reject{0};
    std::atomic<std::uint64_t> click_ignored{0};
    std::atomic<std::uint64_t> selection_clear{0};
};

struct AiRuntime {
    std::atomic<AiMode> mode{AiMode::OFF};
    std::atomic<AiRuntimeState> state{AiRuntimeState::OFF};

    AiPolicy policy;
    AiRenderFlags render;

    ClickRequest click;
    SelectedTrack selected;

    AiCounters counters;
};

// global runtime
extern AiRuntime g_ai_rt;

// cfg -> mode
AiMode cfg_to_ai_mode(int objectdet);

// init from cfg
void init_ai_runtime_from_cfg(int objectdet);

// apply mode directly
void ai_apply_mode(AiRuntime& rt, AiMode mode);

// helpers
void ai_switch_to_off(AiRuntime& rt);

// stage decisions
bool ai_should_run_detector(const AiRuntime& rt);
bool ai_should_run_tracking(const AiRuntime& rt);

// draw decisions
bool ai_should_draw_det(const AiRuntime& rt);
bool ai_should_draw_track(const AiRuntime& rt);
bool ai_should_highlight_selected_track(const AiRuntime& rt);

// detector schedule
bool ai_should_run_detector_this_frame(const AiRuntime& rt, std::uint64_t frame_seq);

// click/select APIs
void on_mouse_click(float x, float y);
void on_mouse_click(AiRuntime& rt, float x, float y);

bool ai_has_pending_click(const AiRuntime& rt);
void ai_clear_click(AiRuntime& rt);

// selected target APIs
void ai_select_track(AiRuntime& rt, int track_id, std::uint64_t frame_seq);
void ai_clear_selected_track(AiRuntime& rt);
bool ai_has_selected_track(const AiRuntime& rt);
int ai_get_selected_track_id(const AiRuntime& rt);

} // namespace k180::ai