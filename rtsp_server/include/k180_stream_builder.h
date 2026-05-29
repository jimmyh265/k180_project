#pragma once
#include <string>
#include "k180_h265_hub.h"
#include "k180_stream_key.h"
#include "user_def_json.h"

namespace k180::streambuilder {

// struct UserConfig;

// Parse "/s1_1" "/s2_1234" (rtsp_media_get_path gives "/mount")
bool parse_stream_path(const char* path, StreamKey* out);
bool build_and_start_all_hubs_from_cfg(HubManager& mgr, const UserConfig& cfggg, k180::osd::OsdShared* osd_shared);

}

