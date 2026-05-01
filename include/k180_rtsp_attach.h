#pragma once
#include <gst/rtsp-server/rtsp-server.h>
#include "k180_stream_key.h"

namespace k180 {
struct HubManager;   // <- add
// attach RTSP factories for:
// /s1_1 /s1_2 /s1_3 /s1_4 /s1_1234
// /s2_1 /s2_2 /s2_3 /s2_4 /s2_1234
void attach_factories(GstRTSPMountPoints* mounts, HubManager& mgr);
}