#pragma once

#include "k180_constants.h"

namespace k180::record_cleanup {

bool cleanup_once(const char* dir = k180::constants::RECORD_DIR,
                  double high_water_percent = 95.0,
                  int min_file_age_sec = 300);

void start_runtime_cleanup(const char* dir = k180::constants::RECORD_DIR);
void stop_runtime_cleanup();

} // namespace k180::record_cleanup
