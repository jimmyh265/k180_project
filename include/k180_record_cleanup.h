#pragma once

namespace k180::record_cleanup {

bool cleanup_once(const char* dir = "/data",
                  double high_water_percent = 95.0,
                  int min_file_age_sec = 300);

void start_runtime_cleanup(const char* dir = "/data");
void stop_runtime_cleanup();

} // namespace k180::record_cleanup
