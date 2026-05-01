#pragma once
#include <condition_variable>
#include <mutex>
#include <cstdint>
#include <atomic>

namespace k180::pipeline {

struct StageEvent {
    std::mutex mu;
    std::condition_variable cv;
    std::uint32_t seq{0};

    void signal() {
        {
            std::lock_guard<std::mutex> lk(mu);
            ++seq;
        }
        cv.notify_one(); // 單等待者：notify_one
    }

    void signal_all() {
        {
            std::lock_guard<std::mutex> lk(mu);
            ++seq;
        }
        cv.notify_all(); // 多等待者：notify_all
    }
	
    // 回傳 false 代表 stop=true（用來優雅結束 thread）
    bool wait(std::uint32_t& last_seen, const std::atomic<bool>& stop) {
        std::unique_lock<std::mutex> lk(mu);
        cv.wait(lk, [&]{
            return stop.load(std::memory_order_acquire) || (seq > last_seen);
        });
        if (stop.load(std::memory_order_acquire)) return false;
        last_seen = seq;
        return true;
    }

    void notify_all() { cv.notify_all(); } // 用於 shutdown 時喚醒卡住的 wait
};

struct StageEventJoin {
    std::mutex mu;
    std::condition_variable cv;
    uint32_t done_mask = 0;                // bit0..bit3
    // uint32_t gen = 0;                      // 第幾輪（避免跳輪時亂掉）
    static constexpr uint32_t ALL = 0xF;   // 4 bits

    // worker 完成一輪後呼叫：id=0..3
    void signal_id(int id) {
        {
            std::lock_guard<std::mutex> lk(mu);
            done_mask |= (1u << id);
        }
        cv.notify_one(); // 叫醒 t_all 來檢查 done_mask 是否已齊
    }

    // t_all：等四個都 done（可接受跳輪的話，這樣就夠）
    // last_gen 由 t_all 自己保存
    bool wait_all(const std::atomic<bool>& stop) {
        std::unique_lock<std::mutex> lk(mu);

        cv.wait(lk, [&]{
            return stop.load(std::memory_order_acquire)
                // || (done_mask == ALL && gen > last_gen);
                || (done_mask == ALL);
        });

        if (stop.load(std::memory_order_acquire)) return false;

        // 消化這一輪：gen++，mask 清掉
        // last_gen = gen;
        done_mask = 0;
        // ++gen;
        return true;
    }

    // shutdown 時喚醒
    void notify_all() { cv.notify_all(); }
};

// 你要直接操作的全域物件（跨 .cpp 共用同一份）
extern StageEvent camera_trig;

extern StageEventJoin blender_apply_sync;
extern StageEventJoin seam_find_sync;

extern std::atomic<bool> pipeline_sync_stop;

} // namespace k180::pipeline

