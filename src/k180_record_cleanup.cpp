#include "k180_record_cleanup.h"

#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
#include <dirent.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "gy_logging.h"

namespace {

constexpr double kDefaultHighWaterPercent = 95.0;
constexpr int kDefaultMinFileAgeSec = 300;
constexpr int kRuntimeIntervalSec = 60;

struct FsUsage {
    unsigned long long total_bytes = 0;
    unsigned long long avail_bytes = 0;
    double used_percent = 0.0;
};

struct CandidateFile {
    std::string path;
    std::time_t mtime = 0;
    unsigned long long size = 0;
};

std::mutex g_thread_mtx;
std::condition_variable g_thread_cv;
std::thread g_thread;
bool g_started = false;
bool g_stop = false;

static bool starts_with(const std::string& s, const char* prefix)
{
    const size_t n = std::strlen(prefix);
    return s.size() >= n && std::memcmp(s.data(), prefix, n) == 0;
}

static bool ends_with(const std::string& s, const char* suffix)
{
    const size_t n = std::strlen(suffix);
    return s.size() >= n && std::memcmp(s.data() + s.size() - n, suffix, n) == 0;
}

static bool is_recording_file_name(const std::string& name)
{
    if (!ends_with(name, ".mp4")) return false;

    static const char* kPrefixes[] = {
        "s1_ch1234_",
        "s1_ch1_",
        "s1_ch2_",
        "s1_ch3_",
        "s1_ch4_",
        "S1_ch1234_",
        "S1_ch1_",
        "S1_ch2_",
        "S1_ch3_",
        "S1_ch4_",
        "s1_1234_",
        "s1_1_",
        "s1_2_",
        "s1_3_",
        "s1_4_",
    };

    for (const char* prefix : kPrefixes) {
        if (starts_with(name, prefix)) return true;
    }
    return false;
}

static std::string join_path(const std::string& dir, const std::string& name)
{
    if (dir.empty() || dir == "/") return "/" + name;
    if (dir.back() == '/') return dir + name;
    return dir + "/" + name;
}

static bool get_fs_usage(const std::string& dir, FsUsage* out)
{
    if (!out) return false;

    struct statvfs st {};
    if (::statvfs(dir.c_str(), &st) != 0) {
        const int saved_errno = errno;
        log_error_fmt("[REC_CLEAN] statvfs('%s') failed: %s",
                      dir.c_str(), std::strerror(saved_errno));
        return false;
    }

    if (st.f_blocks == 0 || st.f_frsize == 0) {
        log_error_fmt("[REC_CLEAN] invalid statvfs result for '%s'", dir.c_str());
        return false;
    }

    const unsigned long long block_size =
        static_cast<unsigned long long>(st.f_frsize);
    const unsigned long long total_blocks =
        static_cast<unsigned long long>(st.f_blocks);
    const unsigned long long avail_blocks =
        static_cast<unsigned long long>(st.f_bavail);
    const unsigned long long used_blocks =
        (total_blocks > avail_blocks) ? (total_blocks - avail_blocks) : 0;

    out->total_bytes = total_blocks * block_size;
    out->avail_bytes = avail_blocks * block_size;
    out->used_percent =
        (static_cast<double>(used_blocks) * 100.0) /
        static_cast<double>(total_blocks);
    return true;
}

static std::vector<CandidateFile> collect_candidate_files(const std::string& dir,
                                                          int min_file_age_sec)
{
    std::vector<CandidateFile> files;

    DIR* dp = ::opendir(dir.c_str());
    if (!dp) {
        const int saved_errno = errno;
        log_error_fmt("[REC_CLEAN] opendir('%s') failed: %s",
                      dir.c_str(), std::strerror(saved_errno));
        return files;
    }

    const std::time_t now = std::time(nullptr);
    while (struct dirent* ent = ::readdir(dp)) {
        const std::string name(ent->d_name);
        if (name == "." || name == "..") continue;
        if (!is_recording_file_name(name)) continue;

        const std::string path = join_path(dir, name);
        struct stat st {};
        if (::lstat(path.c_str(), &st) != 0) {
            const int saved_errno = errno;
            log_error_fmt("[REC_CLEAN] lstat('%s') failed: %s",
                          path.c_str(), std::strerror(saved_errno));
            continue;
        }
        if (!S_ISREG(st.st_mode)) continue;

        // Avoid unlinking a splitmuxsink segment that may still be open.
        if (min_file_age_sec > 0 && now != static_cast<std::time_t>(-1)) {
            if (st.st_mtime > now - min_file_age_sec) {
                continue;
            }
        }

        CandidateFile f;
        f.path = path;
        f.mtime = st.st_mtime;
        f.size = static_cast<unsigned long long>(st.st_size);
        files.push_back(std::move(f));
    }

    ::closedir(dp);

    std::sort(files.begin(), files.end(),
              [](const CandidateFile& a, const CandidateFile& b) {
                  if (a.mtime != b.mtime) return a.mtime < b.mtime;
                  return a.path < b.path;
              });
    return files;
}

static void cleanup_thread_main(std::string dir)
{
    log_info_fmt("[REC_CLEAN] runtime cleanup started dir=%s interval=%ds",
                 dir.c_str(), kRuntimeIntervalSec);

    while (true) {
        std::unique_lock<std::mutex> lock(g_thread_mtx);
        if (g_thread_cv.wait_for(lock, std::chrono::seconds(kRuntimeIntervalSec),
                                 [] { return g_stop; })) {
            break;
        }
        lock.unlock();

        k180::record_cleanup::cleanup_once(dir.c_str(),
                                           kDefaultHighWaterPercent,
                                           kDefaultMinFileAgeSec);
    }

    log_info_fmt("[REC_CLEAN] runtime cleanup stopped");
}

} // namespace

namespace k180::record_cleanup {

bool cleanup_once(const char* dir,
                  double high_water_percent,
                  int min_file_age_sec)
{
    const std::string target_dir =
        (dir && dir[0] != '\0') ? std::string(dir) : std::string("/data");

    if (high_water_percent <= 0.0 || high_water_percent > 100.0) {
        high_water_percent = kDefaultHighWaterPercent;
    }
    if (min_file_age_sec < 0) {
        min_file_age_sec = kDefaultMinFileAgeSec;
    }

    FsUsage usage;
    if (!get_fs_usage(target_dir, &usage)) {
        return false;
    }

    if (usage.used_percent < high_water_percent) {
        return true;
    }

    log_info_fmt("[REC_CLEAN] %s usage %.2f%% >= %.2f%%, deleting old recordings",
                 target_dir.c_str(), usage.used_percent, high_water_percent);

    const auto files = collect_candidate_files(target_dir, min_file_age_sec);
    if (files.empty()) {
        log_error_fmt("[REC_CLEAN] no removable recording files found in %s",
                      target_dir.c_str());
        return false;
    }

    for (const auto& f : files) {
        if (!get_fs_usage(target_dir, &usage)) {
            return false;
        }
        if (usage.used_percent < high_water_percent) {
            log_info_fmt("[REC_CLEAN] %s usage now %.2f%%, cleanup done",
                         target_dir.c_str(), usage.used_percent);
            return true;
        }

        if (::unlink(f.path.c_str()) != 0) {
            const int saved_errno = errno;
            log_error_fmt("[REC_CLEAN] unlink('%s') failed: %s",
                          f.path.c_str(), std::strerror(saved_errno));
            continue;
        }

        log_info_fmt("[REC_CLEAN] deleted %s (%llu bytes)",
                     f.path.c_str(), f.size);
    }

    if (!get_fs_usage(target_dir, &usage)) {
        return false;
    }

    if (usage.used_percent >= high_water_percent) {
        log_error_fmt("[REC_CLEAN] %s usage still %.2f%% after cleanup",
                      target_dir.c_str(), usage.used_percent);
        return false;
    }

    log_info_fmt("[REC_CLEAN] %s usage now %.2f%%, cleanup done",
                 target_dir.c_str(), usage.used_percent);
    return true;
}

void start_runtime_cleanup(const char* dir)
{
    const std::string target_dir =
        (dir && dir[0] != '\0') ? std::string(dir) : std::string("/data");

    std::lock_guard<std::mutex> lock(g_thread_mtx);
    if (g_started) return;

    g_stop = false;
    g_thread = std::thread(cleanup_thread_main, target_dir);
    g_started = true;
}

void stop_runtime_cleanup()
{
    {
        std::lock_guard<std::mutex> lock(g_thread_mtx);
        if (!g_started) return;
        g_stop = true;
    }

    g_thread_cv.notify_all();
    if (g_thread.joinable()) {
        g_thread.join();
    }

    {
        std::lock_guard<std::mutex> lock(g_thread_mtx);
        g_started = false;
        g_stop = false;
    }
}

} // namespace k180::record_cleanup
