#include <gst/gst.h>
#include <gst/app/gstappsink.h>

#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>

#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr int kCamCount = 4;
constexpr int kDefaultWidth = 1920;
constexpr int kDefaultHeight = 1080;

constexpr int CTRL_WDR_MODE = 0x00981991;
constexpr int CTRL_TRIGGER_MODE = 0x009819c0;
constexpr int CTRL_TRIGGER_SHUTTER = 0x009819c1;
constexpr int CTRL_TRIGGER_GAIN = 0x009819c2;
constexpr int CTRL_TRIGGER_WB_MODE = 0x009819c3;
constexpr int CTRL_TRIGGER_DELAY = 0x009819c9;
constexpr int CTRL_SW_TRIGGER = 0x009819cb;

enum class TriggerDispatch {
    Serial,
    Parallel,
};

std::atomic<bool> g_running{true};
volatile sig_atomic_t g_signal_stop = 0;

uint64_t now_ns_raw()
{
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000000000ull +
           static_cast<uint64_t>(ts.tv_nsec);
}

void log_info(const char* fmt, ...)
{
    std::fprintf(stderr, "INFO: ");
    va_list ap;
    va_start(ap, fmt);
    std::vfprintf(stderr, fmt, ap);
    va_end(ap);
    std::fprintf(stderr, "\n");
}

void log_warn(const char* fmt, ...)
{
    std::fprintf(stderr, "WARN: ");
    va_list ap;
    va_start(ap, fmt);
    std::vfprintf(stderr, fmt, ap);
    va_end(ap);
    std::fprintf(stderr, "\n");
}

void log_error(const char* fmt, ...)
{
    std::fprintf(stderr, "ERROR: ");
    va_list ap;
    va_start(ap, fmt);
    std::vfprintf(stderr, fmt, ap);
    va_end(ap);
    std::fprintf(stderr, "\n");
}

class FpsMon {
public:
    FpsMon() = default;
    explicit FpsMon(const char* tag) : tag_(tag) {}

    void set_tag(const char* tag) { tag_ = tag; }

    void tick() { add(1); }

    void add(uint64_t n)
    {
        const uint64_t t = now_ns_raw();
        if (last_print_ns_ == 0) {
            last_print_ns_ = t;
        }

        count_ += n;

        const uint64_t dt = t - last_print_ns_;
        if (dt < 1000000000ull) {
            return;
        }

        const double sec = static_cast<double>(dt) / 1e9;
        const double fps = sec > 0.0 ? static_cast<double>(count_) / sec : 0.0;
        std::fprintf(stderr,
                     "[FPS][%s] count=%llu dt=%.3fs fps=%.2f\n",
                     tag_ ? tag_ : "unnamed",
                     static_cast<unsigned long long>(count_),
                     sec,
                     fps);

        count_ = 0;
        last_print_ns_ = t;
    }

    void flush()
    {
        if (count_ == 0) {
            return;
        }

        const uint64_t t = now_ns_raw();
        const uint64_t dt = last_print_ns_ == 0 ? 0 : t - last_print_ns_;
        const double sec = static_cast<double>(dt) / 1e9;
        const double fps = sec > 0.0 ? static_cast<double>(count_) / sec : 0.0;
        std::fprintf(stderr,
                     "[FPS][%s][flush] count=%llu dt=%.3fs fps=%.2f\n",
                     tag_ ? tag_ : "unnamed",
                     static_cast<unsigned long long>(count_),
                     sec,
                     fps);
        count_ = 0;
        last_print_ns_ = t;
    }

private:
    const char* tag_ = nullptr;
    uint64_t count_ = 0;
    uint64_t last_print_ns_ = 0;
};

class StageTimingAcc {
public:
    StageTimingAcc() = default;
    explicit StageTimingAcc(const char* tag) : tag_(tag) {}

    void set_tag(const char* tag) { tag_ = tag; }

    void add_ns(uint64_t dt_ns)
    {
        const uint64_t t = now_ns_raw();
        if (last_print_ns_ == 0) {
            last_print_ns_ = t;
        }

        ++n_;
        sum_ns_ += dt_ns;
        if (dt_ns > max_ns_) {
            max_ns_ = dt_ns;
        }

        const uint64_t win = t - last_print_ns_;
        if (win < 1000000000ull) {
            return;
        }

        const double avg_us = n_ > 0 ? static_cast<double>(sum_ns_) / n_ / 1000.0 : 0.0;
        const double max_us = static_cast<double>(max_ns_) / 1000.0;
        std::fprintf(stderr,
                     "[TIMING][%s] n=%llu avg=%.1fus max=%.1fus\n",
                     tag_ ? tag_ : "unnamed",
                     static_cast<unsigned long long>(n_),
                     avg_us,
                     max_us);

        n_ = 0;
        sum_ns_ = 0;
        max_ns_ = 0;
        last_print_ns_ = t;
    }

    void flush()
    {
        if (n_ == 0) {
            return;
        }

        const double avg_us = static_cast<double>(sum_ns_) / n_ / 1000.0;
        const double max_us = static_cast<double>(max_ns_) / 1000.0;
        std::fprintf(stderr,
                     "[TIMING][%s][flush] n=%llu avg=%.1fus max=%.1fus\n",
                     tag_ ? tag_ : "unnamed",
                     static_cast<unsigned long long>(n_),
                     avg_us,
                     max_us);

        n_ = 0;
        sum_ns_ = 0;
        max_ns_ = 0;
        last_print_ns_ = now_ns_raw();
    }

private:
    const char* tag_ = nullptr;
    uint64_t n_ = 0;
    uint64_t sum_ns_ = 0;
    uint64_t max_ns_ = 0;
    uint64_t last_print_ns_ = 0;
};

struct Config {
    int width = kDefaultWidth;
    int height = kDefaultHeight;
    int exposure = 100;
    int shutter_prime = 200;
    int gain = 10000;
    int gain_prime = 10000;
    int wb_mode1 = 1;
    int wb_mode2 = 2;
    int trigger_mode1 = 1;
    int trigger_mode2 = 2;
    int wdr_mode1 = 1;
    int wdr_mode2 = 0;
    int trigger_delay = -1;
    int cap_buffers = 12;
    int conv_buffers = 8;
    int appsink_buffers = 2;
    int inter_trigger_delay_us = 0;
    int serial_last_trigger_delay_us = -1;
    int trigger_interval_us = 17000;
    int trigger_fps = 0;
    int duration_sec = 0;
    bool pull_samples = true;
    bool trigger_fps_enabled = false;
    bool gst_null_on_shutdown = false;
    TriggerDispatch trigger_dispatch = TriggerDispatch::Serial;
    std::array<std::string, kCamCount> devices{
        "/dev/video0", "/dev/video1", "/dev/video2", "/dev/video3"};
    std::array<int, kCamCount> trigger_order{0, 1, 2, 3};
    std::array<int, kCamCount> init_order{0, 1, 2, 3};
};

struct GstPtsProbeMon {
    int cam_id = -1;
    char probe_name[32]{};
    char fps_tag[64]{};
    char pts_delta_tag[64]{};
    FpsMon fps;
    StageTimingAcc pts_delta;
    GstClockTime last_pts = GST_CLOCK_TIME_NONE;
};

struct CamCtx {
    int cam_id = -1;
    std::string dev;
    int fd = -1;
    GstElement* pipeline = nullptr;
    GstElement* sink_elem = nullptr;
    GstAppSink* sink = nullptr;
    std::thread pull_thread;
    std::thread bus_thread;
};

std::array<GstPtsProbeMon, kCamCount> g_nvvidconv_in_probe_mon;
std::array<GstPtsProbeMon, kCamCount> g_appsink_pull_mon;
std::array<CamCtx, kCamCount> g_cams;

const char* trigger_dispatch_name(TriggerDispatch dispatch);

struct TriggerStampSnapshot {
    uint64_t seq = 0;
    uint64_t epoch_ns = 0;
    std::array<uint64_t, kCamCount> ts_ns{};
    std::array<bool, kCamCount> ok{};
};

struct TriggerTimingMon {
    TriggerTimingMon()
    {
        skew.set_tag("trigger_skew");
        for (int i = 0; i < kCamCount; ++i) {
            std::snprintf(offset_tags[i], sizeof(offset_tags[i]), "trigger_offset_cam_%d", i);
            offsets[i].set_tag(offset_tags[i]);
        }
    }

    void add(TriggerDispatch dispatch, const TriggerStampSnapshot& s)
    {
        uint64_t min_ts = ~0ull;
        uint64_t max_ts = 0;
        int ok_count = 0;
        double offset_us[kCamCount]{};

        for (int i = 0; i < kCamCount; ++i) {
            if (!s.ok[i] || s.ts_ns[i] == 0) {
                continue;
            }

            ++ok_count;
            if (s.ts_ns[i] < min_ts) {
                min_ts = s.ts_ns[i];
            }
            if (s.ts_ns[i] > max_ts) {
                max_ts = s.ts_ns[i];
            }

            const uint64_t offset_ns = s.ts_ns[i] >= s.epoch_ns ? s.ts_ns[i] - s.epoch_ns : 0;
            offset_us[i] = static_cast<double>(offset_ns) / 1000.0;
            offsets[i].add_ns(offset_ns);
        }

        uint64_t skew_ns = 0;
        if (ok_count >= 2) {
            skew_ns = max_ts - min_ts;
            skew.add_ns(skew_ns);
        }

        const uint64_t now = now_ns_raw();
        if (last_print_ns == 0) {
            last_print_ns = now;
        }
        if (now - last_print_ns >= 1000000000ull) {
            char ok_text[kCamCount + 1]{};
            for (int i = 0; i < kCamCount; ++i) {
                ok_text[i] = s.ok[i] ? '1' : '0';
            }

            std::fprintf(stderr,
                         "[TRIGGER_TS][%s] seq=%llu epoch=%llu "
                         "cam0=%llu(+%.1fus) cam1=%llu(+%.1fus) "
                         "cam2=%llu(+%.1fus) cam3=%llu(+%.1fus) "
                         "skew=%.1fus ok=%s\n",
                         trigger_dispatch_name(dispatch),
                         static_cast<unsigned long long>(s.seq),
                         static_cast<unsigned long long>(s.epoch_ns),
                         static_cast<unsigned long long>(s.ts_ns[0]),
                         offset_us[0],
                         static_cast<unsigned long long>(s.ts_ns[1]),
                         offset_us[1],
                         static_cast<unsigned long long>(s.ts_ns[2]),
                         offset_us[2],
                         static_cast<unsigned long long>(s.ts_ns[3]),
                         offset_us[3],
                         static_cast<double>(skew_ns) / 1000.0,
                         ok_text);
            last_print_ns = now;
        }
    }

    void flush()
    {
        skew.flush();
        for (auto& offset : offsets) {
            offset.flush();
        }
    }

    StageTimingAcc skew;
    char offset_tags[kCamCount][64]{};
    std::array<StageTimingAcc, kCamCount> offsets;
    uint64_t last_print_ns = 0;
};

const char* trigger_dispatch_name(TriggerDispatch dispatch)
{
    switch (dispatch) {
    case TriggerDispatch::Serial:
        return "serial";
    case TriggerDispatch::Parallel:
        return "parallel";
    }
    return "unknown";
}

void handle_signal(int)
{
    g_signal_stop = 1;
}

bool parse_int(const std::string& name,
               const std::string& text,
               int min_value,
               int max_value,
               int* out)
{
    if (text.empty()) {
        log_error("[ARGS] %s missing value", name.c_str());
        return false;
    }

    char* end = nullptr;
    errno = 0;
    long parsed = std::strtol(text.c_str(), &end, 10);
    if (errno != 0 || end == text.c_str() || *end != '\0') {
        log_error("[ARGS] %s invalid integer: %s", name.c_str(), text.c_str());
        return false;
    }
    if (parsed < min_value || parsed > max_value) {
        log_error("[ARGS] %s out of range: %ld (range %d..%d)",
                  name.c_str(),
                  parsed,
                  min_value,
                  max_value);
        return false;
    }

    *out = static_cast<int>(parsed);
    return true;
}

bool read_arg_value(int* index,
                    int argc,
                    char** argv,
                    const std::string& arg,
                    const char* name,
                    std::string* value)
{
    const std::string prefix = std::string(name) + "=";
    if (arg.rfind(prefix, 0) == 0) {
        *value = arg.substr(prefix.size());
        return true;
    }
    if (arg == name) {
        if (*index + 1 >= argc) {
            log_error("[ARGS] %s missing value", name);
            return false;
        }
        *value = argv[++(*index)];
        return true;
    }
    return false;
}

std::vector<std::string> split_csv(const std::string& text)
{
    std::vector<std::string> out;
    std::stringstream ss(text);
    std::string item;
    while (std::getline(ss, item, ',')) {
        out.push_back(item);
    }
    return out;
}

bool parse_cam_order(const std::string& name,
                     const std::string& text,
                     std::array<int, kCamCount>* order)
{
    const auto parts = split_csv(text);
    if (parts.size() != kCamCount) {
        log_error("[ARGS] %s requires exactly %d comma-separated camera ids",
                  name.c_str(),
                  kCamCount);
        return false;
    }

    std::array<bool, kCamCount> seen{};
    for (int i = 0; i < kCamCount; ++i) {
        int v = -1;
        if (!parse_int(name, parts[i], 0, kCamCount - 1, &v)) {
            return false;
        }
        if (seen[v]) {
            log_error("[ARGS] %s contains duplicate camera id %d", name.c_str(), v);
            return false;
        }
        seen[v] = true;
        (*order)[i] = v;
    }
    return true;
}

bool parse_devices(const std::string& text, std::array<std::string, kCamCount>* devices)
{
    const auto parts = split_csv(text);
    if (parts.size() != kCamCount) {
        log_error("[ARGS] --devices requires exactly %d comma-separated paths", kCamCount);
        return false;
    }
    for (int i = 0; i < kCamCount; ++i) {
        if (parts[i].empty()) {
            log_error("[ARGS] --devices contains an empty path");
            return false;
        }
        (*devices)[i] = parts[i];
    }
    return true;
}

bool parse_trigger_dispatch(const std::string& text, TriggerDispatch* dispatch)
{
    if (text == "serial") {
        *dispatch = TriggerDispatch::Serial;
        return true;
    }
    if (text == "parallel") {
        *dispatch = TriggerDispatch::Parallel;
        return true;
    }

    log_error("[ARGS] --trigger-dispatch must be serial or parallel: %s", text.c_str());
    return false;
}

void print_usage(const char* argv0)
{
    std::fprintf(stderr,
                 "Usage: %s [options]\n"
                 "\n"
                 "Minimal 4-camera half-rate repro. No RTSP, recording, blender, or CUDA postprocess.\n"
                 "\n"
                 "Core options:\n"
                 "  --exposure=100\n"
                 "  --inter-trigger-delay-us=0\n"
                 "  --serial-last-trigger-delay-us=N  (default: same as --inter-trigger-delay-us)\n"
                 "  --trigger-order=0,1,2,3\n"
                 "  --trigger-dispatch=serial\n"
                 "  --cap-buffers=12\n"
                 "  --conv-buffers=8\n"
                 "  --appsink-buffers=2\n"
                 "\n"
                 "Useful comparison options:\n"
                 "  --devices=/dev/video0,/dev/video1,/dev/video2,/dev/video3\n"
                 "  --init-order=0,1,2,3\n"
                 "  --shutter-prime=200\n"
                 "  --wb-mode2=2\n"
                 "  --trigger-mode1=1\n"
                 "  --trigger-mode2=2\n"
                 "  --trigger-interval-us=17000\n"
                 "  --trigger-fps=60\n"
                 "  --duration-sec=0\n"
                 "  --no-pull\n"
                 "  --shutdown-gst-null\n",
                 argv0);
}

bool parse_args(int argc, char** argv, Config* cfg)
{
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        std::string value;

        if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            std::exit(0);
        }
        if (arg == "--no-pull") {
            cfg->pull_samples = false;
            continue;
        }
        if (arg == "--shutdown-gst-null") {
            cfg->gst_null_on_shutdown = true;
            continue;
        }

        if (read_arg_value(&i, argc, argv, arg, "--exposure", &value) ||
            read_arg_value(&i, argc, argv, arg, "--trigger-shutter", &value)) {
            if (!parse_int("--exposure", value, 0, 1000000, &cfg->exposure)) {
                return false;
            }
            continue;
        }
        if (read_arg_value(&i, argc, argv, arg, "--shutter-prime", &value)) {
            if (!parse_int("--shutter-prime", value, 0, 1000000, &cfg->shutter_prime)) {
                return false;
            }
            continue;
        }
        if (read_arg_value(&i, argc, argv, arg, "--gain", &value)) {
            if (!parse_int("--gain", value, 0, 2000000, &cfg->gain)) {
                return false;
            }
            continue;
        }
        if (read_arg_value(&i, argc, argv, arg, "--gain-prime", &value)) {
            if (!parse_int("--gain-prime", value, 0, 2000000, &cfg->gain_prime)) {
                return false;
            }
            continue;
        }
        if (read_arg_value(&i, argc, argv, arg, "--wb-mode2", &value)) {
            if (!parse_int("--wb-mode2", value, 0, 10, &cfg->wb_mode2)) {
                return false;
            }
            continue;
        }
        if (read_arg_value(&i, argc, argv, arg, "--trigger-mode1", &value)) {
            if (!parse_int("--trigger-mode1", value, 0, 3, &cfg->trigger_mode1)) {
                return false;
            }
            continue;
        }
        if (read_arg_value(&i, argc, argv, arg, "--trigger-mode2", &value)) {
            if (!parse_int("--trigger-mode2", value, 0, 3, &cfg->trigger_mode2)) {
                return false;
            }
            continue;
        }
        if (read_arg_value(&i, argc, argv, arg, "--trigger-delay", &value)) {
            if (!parse_int("--trigger-delay", value, 0, 30, &cfg->trigger_delay)) {
                return false;
            }
            continue;
        }
        if (read_arg_value(&i, argc, argv, arg, "--cap-buffers", &value)) {
            if (!parse_int("--cap-buffers", value, 1, 256, &cfg->cap_buffers)) {
                return false;
            }
            continue;
        }
        if (read_arg_value(&i, argc, argv, arg, "--conv-buffers", &value)) {
            if (!parse_int("--conv-buffers", value, 1, 256, &cfg->conv_buffers)) {
                return false;
            }
            continue;
        }
        if (read_arg_value(&i, argc, argv, arg, "--appsink-buffers", &value)) {
            if (!parse_int("--appsink-buffers", value, 1, 256, &cfg->appsink_buffers)) {
                return false;
            }
            continue;
        }
        if (read_arg_value(&i, argc, argv, arg, "--inter-trigger-delay-us", &value) ||
            read_arg_value(&i, argc, argv, arg, "--trigger-step-delay-us", &value)) {
            if (!parse_int("--inter-trigger-delay-us", value, 0, 30000,
                           &cfg->inter_trigger_delay_us)) {
                return false;
            }
            continue;
        }
        if (read_arg_value(&i, argc, argv, arg, "--serial-last-trigger-delay-us", &value) ||
            read_arg_value(&i, argc, argv, arg, "--fourth-trigger-delay-us", &value)) {
            if (!parse_int("--serial-last-trigger-delay-us", value, 0, 1000000,
                           &cfg->serial_last_trigger_delay_us)) {
                return false;
            }
            continue;
        }
        if (read_arg_value(&i, argc, argv, arg, "--trigger-interval-us", &value)) {
            if (!parse_int("--trigger-interval-us", value, 1000, 1000000,
                           &cfg->trigger_interval_us)) {
                return false;
            }
            cfg->trigger_fps_enabled = false;
            cfg->trigger_fps = 0;
            continue;
        }
        if (read_arg_value(&i, argc, argv, arg, "--trigger-fps", &value) ||
            read_arg_value(&i, argc, argv, arg, "--trigger-rate-hz", &value)) {
            if (!parse_int("--trigger-fps", value, 1, 1000, &cfg->trigger_fps)) {
                return false;
            }
            cfg->trigger_fps_enabled = true;
            continue;
        }
        if (read_arg_value(&i, argc, argv, arg, "--duration-sec", &value)) {
            if (!parse_int("--duration-sec", value, 0, 86400, &cfg->duration_sec)) {
                return false;
            }
            continue;
        }
        if (read_arg_value(&i, argc, argv, arg, "--width", &value)) {
            if (!parse_int("--width", value, 1, 16384, &cfg->width)) {
                return false;
            }
            continue;
        }
        if (read_arg_value(&i, argc, argv, arg, "--height", &value)) {
            if (!parse_int("--height", value, 1, 16384, &cfg->height)) {
                return false;
            }
            continue;
        }
        if (read_arg_value(&i, argc, argv, arg, "--trigger-order", &value)) {
            if (!parse_cam_order("--trigger-order", value, &cfg->trigger_order)) {
                return false;
            }
            continue;
        }
        if (read_arg_value(&i, argc, argv, arg, "--trigger-dispatch", &value)) {
            if (!parse_trigger_dispatch(value, &cfg->trigger_dispatch)) {
                return false;
            }
            continue;
        }
        if (read_arg_value(&i, argc, argv, arg, "--init-order", &value)) {
            if (!parse_cam_order("--init-order", value, &cfg->init_order)) {
                return false;
            }
            continue;
        }
        if (read_arg_value(&i, argc, argv, arg, "--devices", &value)) {
            if (!parse_devices(value, &cfg->devices)) {
                return false;
            }
            continue;
        }

        log_error("[ARGS] unknown option: %s", arg.c_str());
        return false;
    }

    return true;
}

std::string order_to_string(const std::array<int, kCamCount>& order)
{
    std::ostringstream os;
    for (int i = 0; i < kCamCount; ++i) {
        if (i != 0) {
            os << ",";
        }
        os << order[i];
    }
    return os.str();
}

std::string devices_to_string(const std::array<std::string, kCamCount>& devices)
{
    std::ostringstream os;
    for (int i = 0; i < kCamCount; ++i) {
        if (i != 0) {
            os << ",";
        }
        os << devices[i];
    }
    return os.str();
}

bool set_control_nolock(int cam_id, int fd, uint32_t id, int value, const char* tag)
{
    v4l2_control ctrl{};
    ctrl.id = id;
    ctrl.value = value;
    if (ioctl(fd, VIDIOC_S_CTRL, &ctrl) == -1) {
        log_error("[CAM%d][set_control][%s] ioctl failed id=0x%08x value=%d errno=%d (%s)",
                  cam_id,
                  tag,
                  id,
                  value,
                  errno,
                  std::strerror(errno));
        return false;
    }
    return true;
}

bool single_mipi_camera_init(int cam_id, int fd, const Config& cfg)
{
    if (fd < 0) {
        log_error("[CAM%d] single_mipi_camera_init: fd < 0", cam_id);
        return false;
    }

    bool ok = true;
    ok &= set_control_nolock(cam_id, fd, CTRL_TRIGGER_MODE, cfg.trigger_mode1,
                             "camera_init CTRL_TRIGGER_MODE1");
    sleep(2);
    ok &= set_control_nolock(cam_id, fd, CTRL_TRIGGER_MODE, cfg.trigger_mode2,
                             "camera_init CTRL_TRIGGER_MODE2");
    sleep(1);

    if (cfg.trigger_delay >= 0) {
        ok &= set_control_nolock(cam_id, fd, CTRL_TRIGGER_DELAY, cfg.trigger_delay,
                                 "camera_init CTRL_TRIGGER_DELAY");
        usleep(1000);
    }

    ok &= set_control_nolock(cam_id, fd, CTRL_TRIGGER_WB_MODE, cfg.wb_mode1,
                             "camera_init CTRL_TRIGGER_WB_MODE1");
    usleep(1000);
    ok &= set_control_nolock(cam_id, fd, CTRL_TRIGGER_WB_MODE, cfg.wb_mode2,
                             "camera_init CTRL_TRIGGER_WB_MODE2");
    usleep(1000);

    ok &= set_control_nolock(cam_id, fd, CTRL_TRIGGER_SHUTTER, cfg.shutter_prime,
                             "camera_init CTRL_TRIGGER_SHUTTER1");
    usleep(1000);
    ok &= set_control_nolock(cam_id, fd, CTRL_TRIGGER_SHUTTER, cfg.exposure,
                             "camera_init CTRL_TRIGGER_SHUTTER2");
    usleep(1000);

    ok &= set_control_nolock(cam_id, fd, CTRL_TRIGGER_GAIN, cfg.gain_prime,
                             "camera_init CTRL_TRIGGER_GAIN1");
    usleep(1000);
    ok &= set_control_nolock(cam_id, fd, CTRL_TRIGGER_GAIN, cfg.gain,
                             "camera_init CTRL_TRIGGER_GAIN2");
    usleep(1000);

    ok &= set_control_nolock(cam_id, fd, CTRL_WDR_MODE, cfg.wdr_mode1,
                             "camera_init CTRL_WDR_MODE1");
    usleep(1000);
    ok &= set_control_nolock(cam_id, fd, CTRL_WDR_MODE, cfg.wdr_mode2,
                             "camera_init CTRL_WDR_MODE2");

    if (ok) {
        log_info("[CAM%d] single_mipi_camera_init done fd=%d", cam_id, fd);
    }
    return ok;
}

GstPadProbeReturn gst_pts_probe(GstPad*, GstPadProbeInfo* info, gpointer user_data)
{
    if (!(info->type & GST_PAD_PROBE_TYPE_BUFFER)) {
        return GST_PAD_PROBE_OK;
    }

    auto* mon = static_cast<GstPtsProbeMon*>(user_data);
    if (!mon) {
        return GST_PAD_PROBE_OK;
    }

    GstBuffer* buf = GST_PAD_PROBE_INFO_BUFFER(info);
    if (!buf) {
        return GST_PAD_PROBE_OK;
    }

    mon->fps.tick();

    const GstClockTime pts = GST_BUFFER_PTS(buf);
    if (GST_CLOCK_TIME_IS_VALID(pts)) {
        if (GST_CLOCK_TIME_IS_VALID(mon->last_pts)) {
            if (pts >= mon->last_pts) {
                mon->pts_delta.add_ns(static_cast<uint64_t>(pts - mon->last_pts));
            } else {
                log_error("[%s][PTS_BACKWARD] cam=%d last=%llu now=%llu",
                          mon->probe_name,
                          mon->cam_id,
                          static_cast<unsigned long long>(mon->last_pts),
                          static_cast<unsigned long long>(pts));
            }
        }
        mon->last_pts = pts;
    }

    return GST_PAD_PROBE_OK;
}

bool install_gst_pts_probe(GstElement* elem,
                           const char* pad_name,
                           GstPtsProbeMon* mon,
                           int cam_id,
                           const char* probe_name,
                           const char* fps_tag,
                           const char* pts_delta_tag)
{
    if (!elem || !mon) {
        return false;
    }

    mon->cam_id = cam_id;
    std::snprintf(mon->probe_name, sizeof(mon->probe_name), "%s", probe_name);
    std::snprintf(mon->fps_tag, sizeof(mon->fps_tag), "%s", fps_tag);
    std::snprintf(mon->pts_delta_tag, sizeof(mon->pts_delta_tag), "%s", pts_delta_tag);
    mon->fps.set_tag(mon->fps_tag);
    mon->pts_delta.set_tag(mon->pts_delta_tag);
    mon->last_pts = GST_CLOCK_TIME_NONE;

    GstPad* pad = gst_element_get_static_pad(elem, pad_name);
    if (!pad) {
        log_error("[%s][PROBE_INSTALL_FAIL] cam=%d no %s pad",
                  probe_name,
                  cam_id,
                  pad_name);
        return false;
    }

    gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_BUFFER, gst_pts_probe, mon, nullptr);
    gst_object_unref(pad);
    return true;
}

bool install_nvvidconv_in_probe(GstElement* conv_elem, int cam_id)
{
    char fps_tag[64];
    char pts_delta_tag[64];
    std::snprintf(fps_tag, sizeof(fps_tag), "nvvidconv_in_%d", cam_id);
    std::snprintf(pts_delta_tag, sizeof(pts_delta_tag), "nvvidconv_in_pts_delta_%d", cam_id);
    return install_gst_pts_probe(conv_elem,
                                 "sink",
                                 &g_nvvidconv_in_probe_mon[cam_id],
                                 cam_id,
                                 "NVVIDCONV_IN",
                                 fps_tag,
                                 pts_delta_tag);
}

void appsink_pull_loop(CamCtx* cam)
{
    if (!cam || !cam->sink) {
        return;
    }

    auto& mon = g_appsink_pull_mon[cam->cam_id];
    mon.cam_id = cam->cam_id;
    std::snprintf(mon.probe_name, sizeof(mon.probe_name), "APPSINK_PULL");
    std::snprintf(mon.fps_tag, sizeof(mon.fps_tag), "appsink_pull_%d", cam->cam_id);
    std::snprintf(mon.pts_delta_tag, sizeof(mon.pts_delta_tag), "appsink_pull_pts_delta_%d",
                  cam->cam_id);
    mon.fps.set_tag(mon.fps_tag);
    mon.pts_delta.set_tag(mon.pts_delta_tag);
    mon.last_pts = GST_CLOCK_TIME_NONE;

    while (g_running.load(std::memory_order_relaxed)) {
        GstSample* sample = gst_app_sink_try_pull_sample(cam->sink, 100 * GST_MSECOND);
        if (!sample) {
            continue;
        }

        mon.fps.tick();
        GstBuffer* buf = gst_sample_get_buffer(sample);
        if (buf) {
            const GstClockTime pts = GST_BUFFER_PTS(buf);
            if (GST_CLOCK_TIME_IS_VALID(pts)) {
                if (GST_CLOCK_TIME_IS_VALID(mon.last_pts)) {
                    if (pts >= mon.last_pts) {
                        mon.pts_delta.add_ns(static_cast<uint64_t>(pts - mon.last_pts));
                    } else {
                        log_error("[%s][PTS_BACKWARD] cam=%d last=%llu now=%llu",
                                  mon.probe_name,
                                  mon.cam_id,
                                  static_cast<unsigned long long>(mon.last_pts),
                                  static_cast<unsigned long long>(pts));
                    }
                }
                mon.last_pts = pts;
            }
        }

        gst_sample_unref(sample);
    }

    mon.fps.flush();
    mon.pts_delta.flush();
    log_info("[CAM%d] appsink pull thread exit", cam->cam_id);
}

void bus_loop(CamCtx* cam)
{
    if (!cam || !cam->pipeline) {
        return;
    }

    GstBus* bus = gst_element_get_bus(cam->pipeline);
    if (!bus) {
        return;
    }

    while (g_running.load(std::memory_order_relaxed)) {
        GstMessage* msg = gst_bus_timed_pop_filtered(
            bus,
            200 * GST_MSECOND,
            static_cast<GstMessageType>(GST_MESSAGE_ERROR | GST_MESSAGE_EOS | GST_MESSAGE_WARNING));
        if (!msg) {
            continue;
        }

        switch (GST_MESSAGE_TYPE(msg)) {
        case GST_MESSAGE_ERROR: {
            GError* err = nullptr;
            gchar* debug = nullptr;
            gst_message_parse_error(msg, &err, &debug);
            log_error("[CAM%d][GST_ERROR] src=%s msg=%s debug=%s",
                      cam->cam_id,
                      GST_OBJECT_NAME(msg->src),
                      err ? err->message : "(null)",
                      debug ? debug : "(null)");
            if (err) {
                g_error_free(err);
            }
            if (debug) {
                g_free(debug);
            }
            g_running.store(false, std::memory_order_relaxed);
            break;
        }
        case GST_MESSAGE_WARNING: {
            GError* err = nullptr;
            gchar* debug = nullptr;
            gst_message_parse_warning(msg, &err, &debug);
            log_warn("[CAM%d][GST_WARNING] src=%s msg=%s debug=%s",
                     cam->cam_id,
                     GST_OBJECT_NAME(msg->src),
                     err ? err->message : "(null)",
                     debug ? debug : "(null)");
            if (err) {
                g_error_free(err);
            }
            if (debug) {
                g_free(debug);
            }
            break;
        }
        case GST_MESSAGE_EOS:
            log_warn("[CAM%d][GST_EOS]", cam->cam_id);
            g_running.store(false, std::memory_order_relaxed);
            break;
        default:
            break;
        }

        gst_message_unref(msg);
    }

    gst_object_unref(bus);
}

bool build_cam_pipeline(const Config& cfg, int cam_id, CamCtx* cam)
{
    cam->cam_id = cam_id;
    cam->dev = cfg.devices[cam_id];

    cam->fd = ::open(cam->dev.c_str(), O_RDWR);
    if (cam->fd < 0) {
        log_error("[CAM%d] open failed: %s errno=%d (%s)",
                  cam_id,
                  cam->dev.c_str(),
                  errno,
                  std::strerror(errno));
        return false;
    }

    char pipe[2048];
    std::snprintf(pipe,
                  sizeof(pipe),
                  "nvv4l2camerasrc name=cam_src_%d device=%s cap-buffers=%d ! "
                  "video/x-raw(memory:NVMM),width=%d,height=%d,format=UYVY ! "
                  "nvvidconv name=cam_conv_%d output-buffers=%d ! "
                  "video/x-raw(memory:NVMM),format=RGBA ! "
                  "appsink name=sink_%d sync=false max-buffers=%d drop=true",
                  cam_id,
                  cam->dev.c_str(),
                  cfg.cap_buffers,
                  cfg.width,
                  cfg.height,
                  cam_id,
                  cfg.conv_buffers,
                  cam_id,
                  cfg.appsink_buffers);

    log_info("[CAM%d] launch: %s", cam_id, pipe);

    GError* err = nullptr;
    cam->pipeline = gst_parse_launch(pipe, &err);
    if (!cam->pipeline) {
        log_error("[CAM%d] gst_parse_launch failed: %s", cam_id, err ? err->message : "(null)");
        if (err) {
            g_error_free(err);
        }
        ::close(cam->fd);
        cam->fd = -1;
        return false;
    }
    if (err) {
        log_warn("[CAM%d] gst_parse_launch warning: %s", cam_id, err->message);
        g_error_free(err);
    }

    char conv_name[32];
    std::snprintf(conv_name, sizeof(conv_name), "cam_conv_%d", cam_id);
    GstElement* conv_elem = gst_bin_get_by_name(GST_BIN(cam->pipeline), conv_name);
    if (!conv_elem) {
        log_error("[CAM%d] cannot find nvvidconv: %s", cam_id, conv_name);
        return false;
    }
    if (!install_nvvidconv_in_probe(conv_elem, cam_id)) {
        gst_object_unref(conv_elem);
        return false;
    }
    gst_object_unref(conv_elem);

    char sink_name[32];
    std::snprintf(sink_name, sizeof(sink_name), "sink_%d", cam_id);
    cam->sink_elem = gst_bin_get_by_name(GST_BIN(cam->pipeline), sink_name);
    if (!cam->sink_elem) {
        log_error("[CAM%d] cannot find appsink: %s", cam_id, sink_name);
        return false;
    }
    cam->sink = GST_APP_SINK(cam->sink_elem);
    gst_app_sink_set_emit_signals(cam->sink, FALSE);
    gst_app_sink_set_drop(cam->sink, TRUE);
    gst_app_sink_set_max_buffers(cam->sink, cfg.appsink_buffers);
    g_object_set(G_OBJECT(cam->sink), "wait-on-eos", FALSE, nullptr);

    const GstStateChangeReturn ret = gst_element_set_state(cam->pipeline, GST_STATE_PLAYING);
    log_info("[CAM%d] set_state(PLAYING) ret=%d", cam_id, static_cast<int>(ret));
    if (ret == GST_STATE_CHANGE_FAILURE) {
        log_error("[CAM%d] set_state(PLAYING) failed", cam_id);
        return false;
    }

    GstState cur = GST_STATE_NULL;
    GstState pending = GST_STATE_NULL;
    gst_element_get_state(cam->pipeline, &cur, &pending, 2 * GST_SECOND);
    log_info("[CAM%d] get_state cur=%s pending=%s",
             cam_id,
             gst_element_state_get_name(cur),
             gst_element_state_get_name(pending));

    cam->bus_thread = std::thread(bus_loop, cam);
    return true;
}

bool cam_do_trigger(int cam_id, CamCtx* cam, uint64_t* trigger_ts_ns)
{
    if (!cam || cam->fd < 0) {
        if (trigger_ts_ns) {
            *trigger_ts_ns = now_ns_raw();
        }
        return false;
    }
    const bool ok = set_control_nolock(cam_id, cam->fd, CTRL_SW_TRIGGER, 1, "trigger");
    if (trigger_ts_ns) {
        *trigger_ts_ns = now_ns_raw();
    }
    return ok;
}

void log_trigger_failures(const TriggerStampSnapshot& snap)
{
    for (int i = 0; i < kCamCount; ++i) {
        if (!snap.ok[i]) {
            log_error("[TRIGGER][FAIL] cam=%d", i);
        }
    }
}

class TriggerScheduler {
public:
    using clock = std::chrono::steady_clock;

    explicit TriggerScheduler(const Config& cfg)
        : fps_mode_(cfg.trigger_fps_enabled),
          fps_(cfg.trigger_fps),
          interval_(std::chrono::microseconds(cfg.trigger_interval_us)),
          start_(clock::now()),
          next_interval_(start_ + interval_)
    {
    }

    void sleep_until_next()
    {
        std::this_thread::sleep_until(next_deadline());
    }

    void advance()
    {
        if (!fps_mode_) {
            next_interval_ += interval_;

            const auto now = clock::now();
            if (now > next_interval_) {
                const auto lag = now - next_interval_;
                const auto skip = (lag / interval_) + 1;
                next_interval_ += skip * interval_;
            }
            return;
        }

        ++tick_;
        const auto elapsed_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            clock::now() - start_).count();
        if (elapsed_ns <= 0) {
            return;
        }

        const uint64_t now_ns = static_cast<uint64_t>(elapsed_ns);
        const uint64_t next_ns = deadline_offset_ns(tick_);
        if (now_ns > next_ns) {
            const uint64_t skip_to_tick = (now_ns * static_cast<uint64_t>(fps_)) /
                                          1000000000ull + 1;
            if (skip_to_tick > tick_) {
                tick_ = skip_to_tick;
            }
        }
    }

private:
    clock::time_point next_deadline() const
    {
        if (!fps_mode_) {
            return next_interval_;
        }
        return start_ + std::chrono::nanoseconds(deadline_offset_ns(tick_));
    }

    uint64_t deadline_offset_ns(uint64_t tick) const
    {
        return (tick * 1000000000ull) / static_cast<uint64_t>(fps_);
    }

    bool fps_mode_ = false;
    int fps_ = 0;
    std::chrono::microseconds interval_{17000};
    clock::time_point start_;
    clock::time_point next_interval_;
    uint64_t tick_ = 1;
};

void trigger_loop_serial(const Config& cfg)
{
    TriggerScheduler scheduler(cfg);
    FpsMon fps("trigger_thread");
    TriggerTimingMon timing;
    uint64_t seq = 0;

    while (g_running.load(std::memory_order_relaxed)) {
        scheduler.sleep_until_next();

        TriggerStampSnapshot snap;
        snap.seq = ++seq;
        snap.epoch_ns = now_ns_raw();

        for (int pos = 0; pos < kCamCount; ++pos) {
            const int cam_id = cfg.trigger_order[pos];
            snap.ok[cam_id] = cam_do_trigger(cam_id, &g_cams[cam_id], &snap.ts_ns[cam_id]);
            if (cfg.inter_trigger_delay_us > 0 && pos + 1 < kCamCount) {
                usleep(cfg.inter_trigger_delay_us);
            }
        }

        log_trigger_failures(snap);
        timing.add(TriggerDispatch::Serial, snap);
        fps.tick();
        scheduler.advance();
    }

    fps.flush();
    timing.flush();
    log_info("trigger_thread serial exit");
}

struct ParallelTriggerState {
    std::mutex mtx;
    std::condition_variable cv_start;
    std::condition_variable cv_done;
    bool stop = false;
    uint64_t seq = 0;
    uint64_t epoch_ns = 0;
    int done_count = 0;
    std::array<uint64_t, kCamCount> ts_ns{};
    std::array<bool, kCamCount> ok{};
};

void parallel_trigger_worker(int cam_id, ParallelTriggerState* state)
{
    uint64_t seen_seq = 0;

    while (true) {
        uint64_t seq = 0;
        {
            std::unique_lock<std::mutex> lock(state->mtx);
            state->cv_start.wait(lock, [&] {
                return state->stop || state->seq != seen_seq;
            });
            if (state->stop) {
                break;
            }
            seq = state->seq;
            seen_seq = seq;
        }

        uint64_t trigger_ts_ns = 0;
        const bool ok = cam_do_trigger(cam_id, &g_cams[cam_id], &trigger_ts_ns);

        {
            std::lock_guard<std::mutex> lock(state->mtx);
            if (state->seq == seq) {
                state->ts_ns[cam_id] = trigger_ts_ns;
                state->ok[cam_id] = ok;
                ++state->done_count;
                if (state->done_count >= kCamCount) {
                    state->cv_done.notify_one();
                }
            }
        }
    }
}

void trigger_loop_parallel(const Config& cfg)
{
    TriggerScheduler scheduler(cfg);
    FpsMon fps("trigger_thread");
    TriggerTimingMon timing;
    ParallelTriggerState state;
    std::array<std::thread, kCamCount> workers;

    if (cfg.inter_trigger_delay_us > 0) {
        log_warn("[TRIGGER] --inter-trigger-delay-us is ignored in parallel dispatch");
    }

    for (int i = 0; i < kCamCount; ++i) {
        workers[i] = std::thread(parallel_trigger_worker, i, &state);
    }

    while (g_running.load(std::memory_order_relaxed)) {
        scheduler.sleep_until_next();

        {
            std::lock_guard<std::mutex> lock(state.mtx);
            state.done_count = 0;
            state.ts_ns.fill(0);
            state.ok.fill(false);
            state.epoch_ns = now_ns_raw();
            ++state.seq;
        }
        state.cv_start.notify_all();

        TriggerStampSnapshot snap;
        {
            std::unique_lock<std::mutex> lock(state.mtx);
            const bool all_done = state.cv_done.wait_for(lock, std::chrono::milliseconds(100), [&] {
                return state.done_count >= kCamCount;
            });
            snap.seq = state.seq;
            snap.epoch_ns = state.epoch_ns;
            snap.ts_ns = state.ts_ns;
            snap.ok = state.ok;
            if (!all_done) {
                log_error("[TRIGGER][PARALLEL_TIMEOUT] seq=%llu done=%d",
                          static_cast<unsigned long long>(state.seq),
                          state.done_count);
            }
        }

        log_trigger_failures(snap);
        timing.add(TriggerDispatch::Parallel, snap);
        fps.tick();
        scheduler.advance();
    }

    {
        std::lock_guard<std::mutex> lock(state.mtx);
        state.stop = true;
    }
    state.cv_start.notify_all();

    for (auto& worker : workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }

    fps.flush();
    timing.flush();
    log_info("trigger_thread parallel exit");
}

void trigger_loop(const Config& cfg)
{
    if (cfg.trigger_dispatch == TriggerDispatch::Parallel) {
        trigger_loop_parallel(cfg);
        return;
    }
    trigger_loop_serial(cfg);
}

void cleanup_cameras(bool gst_null_on_shutdown)
{
    log_info("[CLEANUP] begin gst_null_on_shutdown=%d", gst_null_on_shutdown ? 1 : 0);

    for (auto& cam : g_cams) {
        if (cam.pull_thread.joinable()) {
            log_info("[CLEANUP][CAM%d] join appsink pull begin", cam.cam_id);
            cam.pull_thread.join();
            log_info("[CLEANUP][CAM%d] join appsink pull done", cam.cam_id);
        }
    }

    if (gst_null_on_shutdown) {
        for (auto& cam : g_cams) {
            if (!cam.pipeline) {
                continue;
            }

            log_info("[CLEANUP][CAM%d] set_state(PAUSED) begin", cam.cam_id);
            gst_element_set_state(cam.pipeline, GST_STATE_PAUSED);
            gst_element_get_state(cam.pipeline, nullptr, nullptr, 500 * GST_MSECOND);

            log_info("[CLEANUP][CAM%d] flush pipeline", cam.cam_id);
            gst_element_send_event(cam.pipeline, gst_event_new_flush_start());
            gst_element_send_event(cam.pipeline, gst_event_new_flush_stop(FALSE));

            log_info("[CLEANUP][CAM%d] set_state(NULL) begin", cam.cam_id);
            gst_element_set_state(cam.pipeline, GST_STATE_NULL);

            GstState cur = GST_STATE_VOID_PENDING;
            GstState pending = GST_STATE_VOID_PENDING;
            const GstStateChangeReturn ret =
                gst_element_get_state(cam.pipeline, &cur, &pending, 2 * GST_SECOND);
            log_info("[CLEANUP][CAM%d] set_state(NULL) ret=%d cur=%s pending=%s",
                     cam.cam_id,
                     static_cast<int>(ret),
                     gst_element_state_get_name(cur),
                     gst_element_state_get_name(pending));
        }
    } else {
        log_info("[CLEANUP] skip gst_element_set_state(NULL)");
    }

    for (auto& cam : g_cams) {
        if (cam.bus_thread.joinable()) {
            log_info("[CLEANUP][CAM%d] join bus begin", cam.cam_id);
            cam.bus_thread.join();
            log_info("[CLEANUP][CAM%d] join bus done", cam.cam_id);
        }
    }

    for (auto& mon : g_nvvidconv_in_probe_mon) {
        mon.fps.flush();
        mon.pts_delta.flush();
    }

    for (auto& cam : g_cams) {
        if (cam.sink_elem) {
            log_info("[CLEANUP][CAM%d] unref sink begin", cam.cam_id);
            gst_object_unref(cam.sink_elem);
            cam.sink_elem = nullptr;
            cam.sink = nullptr;
            log_info("[CLEANUP][CAM%d] unref sink done", cam.cam_id);
        }
        if (cam.pipeline) {
            log_info("[CLEANUP][CAM%d] unref pipeline begin", cam.cam_id);
            gst_object_unref(cam.pipeline);
            cam.pipeline = nullptr;
            log_info("[CLEANUP][CAM%d] unref pipeline done", cam.cam_id);
        }
        if (cam.fd >= 0) {
            log_info("[CLEANUP][CAM%d] trigger mode clean begin fd=%d", cam.cam_id, cam.fd);
            set_control_nolock(cam.cam_id, cam.fd, CTRL_TRIGGER_MODE, 0,
                               "TRIGGER_MODE clean");
            ::close(cam.fd);
            cam.fd = -1;
            log_info("[CLEANUP][CAM%d] close fd done", cam.cam_id);
        }
    }

    log_info("[CLEANUP] done");
}

} // namespace

int main(int argc, char** argv)
{
    Config cfg;
    if (!parse_args(argc, argv, &cfg)) {
        print_usage(argv[0]);
        return 2;
    }

    ::signal(SIGINT, handle_signal);
    ::signal(SIGTERM, handle_signal);

    gst_init(&argc, &argv);

    log_info("cam_half_rate_min start");
    log_info("devices=%s", devices_to_string(cfg.devices).c_str());
    log_info("width=%d height=%d exposure=%d shutter_prime=%d gain=%d gain_prime=%d",
             cfg.width,
             cfg.height,
             cfg.exposure,
             cfg.shutter_prime,
             cfg.gain,
             cfg.gain_prime);
    log_info("wb=%d->%d trigger_mode=%d->%d wdr=%d->%d trigger_delay=%d",
             cfg.wb_mode1,
             cfg.wb_mode2,
             cfg.trigger_mode1,
             cfg.trigger_mode2,
             cfg.wdr_mode1,
             cfg.wdr_mode2,
             cfg.trigger_delay);
    log_info("cap_buffers=%d conv_buffers=%d appsink_buffers=%d pull_samples=%d",
             cfg.cap_buffers,
             cfg.conv_buffers,
             cfg.appsink_buffers,
             cfg.pull_samples ? 1 : 0);
    log_info("shutdown_gst_null=%d", cfg.gst_null_on_shutdown ? 1 : 0);
    log_info("trigger_dispatch=%s trigger_order=%s init_order=%s inter_trigger_delay_us=%d serial_last_trigger_delay_us=%d trigger_interval_us=%d trigger_fps=%d trigger_schedule=%s",
             trigger_dispatch_name(cfg.trigger_dispatch),
             order_to_string(cfg.trigger_order).c_str(),
             order_to_string(cfg.init_order).c_str(),
             cfg.inter_trigger_delay_us,
             cfg.serial_last_trigger_delay_us,
             cfg.trigger_interval_us,
             cfg.trigger_fps,
             cfg.trigger_fps_enabled ? "fps" : "interval-us");

    int ret = 0;
    for (int i = 0; i < kCamCount; ++i) {
        if (!build_cam_pipeline(cfg, i, &g_cams[i])) {
            ret = 1;
            g_running.store(false, std::memory_order_relaxed);
            cleanup_cameras(cfg.gst_null_on_shutdown);
            return ret;
        }
        if (!g_running.load(std::memory_order_relaxed)) {
            ret = 1;
            cleanup_cameras(cfg.gst_null_on_shutdown);
            return ret;
        }
    }

    for (int pos = 0; pos < kCamCount; ++pos) {
        const int cam_id = cfg.init_order[pos];
        if (!single_mipi_camera_init(cam_id, g_cams[cam_id].fd, cfg)) {
            ret = 3;
            g_running.store(false, std::memory_order_relaxed);
            cleanup_cameras(cfg.gst_null_on_shutdown);
            return ret;
        }
    }
    log_info("camera v4l2 init (post-PLAYING) done");

    if (cfg.pull_samples) {
        for (auto& cam : g_cams) {
            cam.pull_thread = std::thread(appsink_pull_loop, &cam);
        }
    }

    std::thread trig_thread(trigger_loop, std::cref(cfg));
    log_info("stream ready");

    const auto start = std::chrono::steady_clock::now();
    while (g_running.load(std::memory_order_relaxed)) {
        if (g_signal_stop) {
            log_info("[SIG] stop requested");
            g_running.store(false, std::memory_order_relaxed);
            break;
        }
        if (cfg.duration_sec > 0) {
            const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - start);
            if (elapsed.count() >= cfg.duration_sec) {
                log_info("duration reached: %d sec", cfg.duration_sec);
                g_running.store(false, std::memory_order_relaxed);
                break;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    if (trig_thread.joinable()) {
        trig_thread.join();
    }

    cleanup_cameras(cfg.gst_null_on_shutdown);
    log_info("cam_half_rate_min exit ret=%d", ret);
    return ret;
}
