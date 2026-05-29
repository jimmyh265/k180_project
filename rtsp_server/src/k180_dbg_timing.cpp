#include "k180_dbg_timing.h"
#include <cstdio>
#include <time.h>

namespace k180::dbgtime {

uint64_t now_ns_mono()
{
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

uint64_t now_ns_raw()
{
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

// ---------------- FpsMon ----------------

FpsMon::FpsMon(const char* tag)
    : tag_(tag)
{
}

void FpsMon::set_tag(const char* tag)
{
    tag_ = tag;
}

void FpsMon::tick()
{
    add(1);
}

void FpsMon::add(uint64_t n)
{
    const uint64_t t = now_ns_raw();

    if (last_print_ns_ == 0) {
        last_print_ns_ = t;
    }

    count_ += n;

    const uint64_t dt = t - last_print_ns_;
    if (dt < 1000000000ull) return;

    const double sec = (double)dt / 1e9;
    const double fps = (sec > 0.0) ? ((double)count_ / sec) : 0.0;

    std::fprintf(stderr,
                 "[FPS][%s] count=%llu dt=%.3fs fps=%.2f\n",
                 tag_ ? tag_ : "unnamed",
                 (unsigned long long)count_,
                 sec,
                 fps);

    count_ = 0;
    last_print_ns_ = t;
}

void FpsMon::flush()
{
    const uint64_t t = now_ns_raw();

    if (last_print_ns_ == 0 || count_ == 0) {
        last_print_ns_ = t;
        return;
    }

    const uint64_t dt = t - last_print_ns_;
    const double sec = (double)dt / 1e9;
    const double fps = (sec > 0.0) ? ((double)count_ / sec) : 0.0;

    std::fprintf(stderr,
                 "[FPS][%s][flush] count=%llu dt=%.3fs fps=%.2f\n",
                 tag_ ? tag_ : "unnamed",
                 (unsigned long long)count_,
                 sec,
                 fps);

    count_ = 0;
    last_print_ns_ = t;
}

void FpsMon::reset()
{
    count_ = 0;
    last_print_ns_ = 0;
}

// ---------------- StageTimingAcc ----------------

StageTimingAcc::StageTimingAcc(const char* tag)
    : tag_(tag)
{
}

void StageTimingAcc::set_tag(const char* tag)
{
    tag_ = tag;
}

void StageTimingAcc::add_ns(uint64_t dt_ns)
{
    const uint64_t t = now_ns_raw();

    if (last_print_ns_ == 0) {
        last_print_ns_ = t;
    }

    ++n_;
    sum_ns_ += dt_ns;
    if (dt_ns > max_ns_) max_ns_ = dt_ns;

    const uint64_t win = t - last_print_ns_;
    if (win < 1000000000ull) return;

    const double avg_us = (n_ > 0) ? ((double)sum_ns_ / (double)n_ / 1000.0) : 0.0;
    const double max_us = (double)max_ns_ / 1000.0;

    std::fprintf(stderr,
                 "[TIMING][%s] n=%llu avg=%.1fus max=%.1fus\n",
                 tag_ ? tag_ : "unnamed",
                 (unsigned long long)n_,
                 avg_us,
                 max_us);

    n_ = 0;
    sum_ns_ = 0;
    max_ns_ = 0;
    last_print_ns_ = t;
}

void StageTimingAcc::add_us(double dt_us)
{
    if (dt_us <= 0.0) {
        add_ns(0);
        return;
    }

    const uint64_t dt_ns = (uint64_t)(dt_us * 1000.0);
    add_ns(dt_ns);
}

void StageTimingAcc::flush()
{
    if (n_ == 0) return;

    const double avg_us = (double)sum_ns_ / (double)n_ / 1000.0;
    const double max_us = (double)max_ns_ / 1000.0;

    std::fprintf(stderr,
                 "[TIMING][%s][flush] n=%llu avg=%.1fus max=%.1fus\n",
                 tag_ ? tag_ : "unnamed",
                 (unsigned long long)n_,
                 avg_us,
                 max_us);

    n_ = 0;
    sum_ns_ = 0;
    max_ns_ = 0;
    last_print_ns_ = now_ns_raw();
}

void StageTimingAcc::reset()
{
    n_ = 0;
    sum_ns_ = 0;
    max_ns_ = 0;
    last_print_ns_ = 0;
}

// ---------------- ScopeTimer ----------------

ScopeTimer::ScopeTimer(StageTimingAcc& acc)
    : acc_(&acc), t0_ns_(now_ns_raw())
{
}

ScopeTimer::~ScopeTimer()
{
    if (!acc_) return;
    const uint64_t t1 = now_ns_raw();
    acc_->add_ns(t1 - t0_ns_);
}

} // namespace k180::dbgtime
