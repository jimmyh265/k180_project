#pragma once

#include <stdint.h>
#include <stdio.h>

namespace k180::dbgtime {

// monotonic: 適合做 pipeline / PTS / 相對時間
uint64_t now_ns_mono();

// monotonic raw: 適合做 debug 計時，盡量不受時間校正影響
uint64_t now_ns_raw();

class FpsMon {
public:
    FpsMon() = default;
    explicit FpsMon(const char* tag);

    void set_tag(const char* tag);

    // 每發生一次事件呼叫一次
    void tick();

    // 一次加 n 個事件（較少用，但保留）
    void add(uint64_t n);

    // 強制印出目前視窗資料；若視窗內沒資料則不印
    void flush();

    // reset 全部統計
    void reset();

private:
    const char* tag_ = nullptr;
    uint64_t count_ = 0;
    uint64_t last_print_ns_ = 0;
};

class StageTimingAcc {
public:
    StageTimingAcc() = default;
    explicit StageTimingAcc(const char* tag);

    void set_tag(const char* tag);

    // 傳入一筆耗時（ns）
    void add_ns(uint64_t dt_ns);

    // 傳入一筆耗時（us）
    void add_us(double dt_us);

    void flush();
    void reset();

private:
    const char* tag_ = nullptr;
    uint64_t n_ = 0;
    uint64_t sum_ns_ = 0;
    uint64_t max_ns_ = 0;
    uint64_t last_print_ns_ = 0;
};

// scope timer：想量某個小區段時可直接 stack 宣告
class ScopeTimer {
public:
    explicit ScopeTimer(StageTimingAcc& acc);
    ~ScopeTimer();

    ScopeTimer(const ScopeTimer&) = delete;
    ScopeTimer& operator=(const ScopeTimer&) = delete;

private:
    StageTimingAcc* acc_ = nullptr;
    uint64_t t0_ns_ = 0;
};

} // namespace k180::dbgtime
