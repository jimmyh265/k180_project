
#pragma once
#include "user_def_json.h"
#include <iosfwd>

// 將 cfg 印到任意 output stream（std::cout / std::cerr / 檔案）
void user_cfg_dump(const UserConfig& cfg, std::ostream& os, bool pretty = true);
