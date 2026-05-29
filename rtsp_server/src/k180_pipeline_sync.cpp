#include "k180_stage_sync.h"

namespace k180::pipeline {

// 這裡是「唯一」定義的地方（不能放在 header，不然每個編譯單元都會各有一份）
StageEvent camera_trig;

StageEventJoin blender_apply_sync;
StageEventJoin seam_find_sync;

std::atomic<bool> pipeline_sync_stop{false};

} // namespace k180::pipeline

