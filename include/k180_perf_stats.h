// perf_stats.h
#pragma once
#include <cstdint>
#include <chrono>

struct TimeTesting {

    float fps_duration = 0.0f;
    float fps_duration1 = 0.0f;
    float fps_duration2 = 0.0f;
    float fps_duration3 = 0.0f;
	
	int please_up = 0;
	
    std::chrono::steady_clock::time_point ts_fps_beg{}, ts_fps_end{};
    std::chrono::steady_clock::time_point ts_fps_beg1{}, ts_fps_end1{};
    std::chrono::steady_clock::time_point ts_fps_beg2{}, ts_fps_end2{};
    std::chrono::steady_clock::time_point ts_fps_beg3{}, ts_fps_end3{};
};
