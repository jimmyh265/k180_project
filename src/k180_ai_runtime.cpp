#include "k180_ai_runtime.h"
#include "k180_runtime.h"

namespace k180::ai {

AiRuntime g_ai_rt;

AiMode cfg_to_ai_mode(int objectdet)
{
    switch (objectdet) {
    case 1:
        return AiMode::DETECT_ONLY;
    case 2:
        return AiMode::DETECT_TO_TRACK;
    case 0:
    default:
        return AiMode::OFF;
    }
}

void init_ai_runtime_from_cfg(int objectdet)
{
    ai_apply_mode(g_ai_rt, cfg_to_ai_mode(objectdet));
}

void ai_apply_mode(AiRuntime& rt, AiMode mode)
{
    rt.mode.store(mode, std::memory_order_release);

    // reset transient input
    rt.click.pending.store(false, std::memory_order_release);
    rt.click.x = 0.0f;
    rt.click.y = 0.0f;

    // reset selected target
    rt.selected.active.store(false, std::memory_order_release);
    rt.selected.track_id = -1;
    rt.selected.selected_frame_seq = 0;

    switch (mode) {
    case AiMode::OFF:
        rt.policy.video_output_enabled = true;
        rt.policy.detector_enabled = false;
        rt.policy.tracking_enabled = false;

        rt.render.draw_det_boxes = false;
        rt.render.draw_track_boxes = false;
        rt.render.highlight_selected_track = false;

        rt.state.store(AiRuntimeState::OFF, std::memory_order_release);
        break;

    case AiMode::DETECT_ONLY:
        rt.policy.video_output_enabled = true;
        rt.policy.detector_enabled = true;
        rt.policy.tracking_enabled = false;

        rt.render.draw_det_boxes = k180::runtime::cfggg.draw_det;
        rt.render.draw_track_boxes = false;
        rt.render.highlight_selected_track = false;

        rt.state.store(AiRuntimeState::RUNNING, std::memory_order_release);
        break;

    case AiMode::DETECT_TO_TRACK:
        rt.policy.video_output_enabled = true;
        rt.policy.detector_enabled = true;
        rt.policy.tracking_enabled = true;

        // 依你目前規劃：
        // detect boxes 照常可畫
        // track boxes 也可畫
        // click 選中的 track 再做特別顏色
        rt.render.draw_det_boxes = k180::runtime::cfggg.draw_det;
        rt.render.draw_track_boxes = k180::runtime::cfggg.draw_trk;
        rt.render.highlight_selected_track = true;

        rt.state.store(AiRuntimeState::RUNNING, std::memory_order_release);
        break;
    }
}

void ai_switch_to_off(AiRuntime& rt)
{
    ai_apply_mode(rt, AiMode::OFF);
}

bool ai_should_run_detector(const AiRuntime& rt)
{
    if (!rt.policy.detector_enabled) return false;
    return rt.state.load(std::memory_order_acquire) == AiRuntimeState::RUNNING;
}

bool ai_should_run_tracking(const AiRuntime& rt)
{
    if (!rt.policy.tracking_enabled) return false;
    return rt.state.load(std::memory_order_acquire) == AiRuntimeState::RUNNING;
}

bool ai_should_draw_det(const AiRuntime& rt)
{
    if (rt.state.load(std::memory_order_acquire) != AiRuntimeState::RUNNING) {
        return false;
    }
    return rt.render.draw_det_boxes;
}

bool ai_should_draw_track(const AiRuntime& rt)
{
    if (rt.state.load(std::memory_order_acquire) != AiRuntimeState::RUNNING) {
        return false;
    }
    return rt.render.draw_track_boxes;
}

bool ai_should_highlight_selected_track(const AiRuntime& rt)
{
    if (rt.state.load(std::memory_order_acquire) != AiRuntimeState::RUNNING) {
        return false;
    }
    return rt.render.highlight_selected_track &&
           rt.selected.active.load(std::memory_order_acquire);
}

bool ai_should_run_detector_this_frame(const AiRuntime& rt, std::uint64_t /*frame_seq*/)
{
    // 依你目前規劃：
    // OFF             -> 不跑 detector
    // DETECT_ONLY     -> 每幀都跑
    // DETECT_TO_TRACK -> 每幀都跑，因為 ByteTrack 需要持續更新 detector result
    return ai_should_run_detector(rt);
}

void on_mouse_click(AiRuntime& rt, float x, float y)
{
    const auto mode = rt.mode.load(std::memory_order_acquire);
    const auto st   = rt.state.load(std::memory_order_acquire);

    if (mode != AiMode::DETECT_TO_TRACK || st != AiRuntimeState::RUNNING) {
        rt.counters.click_ignored.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    rt.click.x = x;
    rt.click.y = y;
    rt.click.pending.store(true, std::memory_order_release);
}

void on_mouse_click(float x, float y)
{
    on_mouse_click(g_ai_rt, x, y);
}

bool ai_has_pending_click(const AiRuntime& rt)
{
    return rt.click.pending.load(std::memory_order_acquire);
}

void ai_clear_click(AiRuntime& rt)
{
    rt.click.pending.store(false, std::memory_order_release);
}

void ai_select_track(AiRuntime& rt, int track_id, std::uint64_t frame_seq)
{
    rt.selected.track_id = track_id;
    rt.selected.selected_frame_seq = frame_seq;
    rt.selected.active.store(track_id >= 0, std::memory_order_release);
}

void ai_clear_selected_track(AiRuntime& rt)
{
    rt.selected.active.store(false, std::memory_order_release);
    rt.selected.track_id = -1;
    rt.selected.selected_frame_seq = 0;
    rt.counters.selection_clear.fetch_add(1, std::memory_order_relaxed);
}

bool ai_has_selected_track(const AiRuntime& rt)
{
    return rt.selected.active.load(std::memory_order_acquire);
}

int ai_get_selected_track_id(const AiRuntime& rt)
{
    if (!rt.selected.active.load(std::memory_order_acquire)) {
        return -1;
    }
    return rt.selected.track_id;
}

} // namespace k180::ai