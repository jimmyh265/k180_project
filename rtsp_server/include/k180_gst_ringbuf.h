#pragma once
#include <gst/gst.h>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>
#include <cstddef>

class GstFrameRing {
public:
    GstFrameRing() = default;

    bool init(size_t slots, size_t bytes_per_frame) {
        if (slots == 0 || bytes_per_frame == 0) return false;

        slots_ = slots;
        bytes_ = bytes_per_frame;

        storage_.clear();
        storage_.resize(slots_);
        for (size_t i = 0; i < slots_; ++i) storage_[i].resize(bytes_);

        inuse_.reset(new std::atomic_bool[slots_]);
        for (size_t i = 0; i < slots_; ++i) inuse_[i].store(false, std::memory_order_relaxed);

        wr_.store(0, std::memory_order_relaxed);
        reset_stats();
        return true;
    }

    size_t bytes_per_frame() const { return bytes_; }
    size_t slots() const { return slots_; }

    uint8_t* try_acquire_slot(size_t* out_idx) {
        if (!out_idx || slots_ == 0 || !inuse_) return nullptr;

        size_t start = wr_.load(std::memory_order_relaxed);
        for (size_t k = 0; k < slots_; ++k) {
            size_t i = (start + k) % slots_;
            bool expected = false;
            if (inuse_[i].compare_exchange_strong(expected, true,
                                                  std::memory_order_acq_rel,
                                                  std::memory_order_relaxed)) {
                *out_idx = i;
                wr_.store((i + 1) % slots_, std::memory_order_relaxed);

                acquire_ok_.fetch_add(1, std::memory_order_relaxed);
                uint32_t now = inuse_now_.fetch_add(1, std::memory_order_relaxed) + 1;

                uint32_t curmax = inuse_max_.load(std::memory_order_relaxed);
                while (now > curmax &&
                       !inuse_max_.compare_exchange_weak(curmax, now,
                                                         std::memory_order_relaxed)) {
                }
                return storage_[i].data();
            }
        }

        acquire_fail_.fetch_add(1, std::memory_order_relaxed);
        return nullptr;
    }

    // ✅ 給外部在 wrap 失敗時手動釋放 slot（同時修正 inuse_now_）
    void release_slot(size_t idx) {
        if (idx >= slots_ || !inuse_) return;
        bool was = inuse_[idx].exchange(false, std::memory_order_acq_rel);
        if (was) inuse_now_.fetch_sub(1, std::memory_order_relaxed);
    }

	GstBuffer* wrap_slot_as_buffer(size_t idx, size_t valid_size) {
		if (idx >= slots_ || !inuse_) return nullptr;
		if (valid_size > bytes_) return nullptr;

		auto* cookie = new Cookie{this, idx};

		GstBuffer* buf = gst_buffer_new_wrapped_full(
			(GstMemoryFlags)0,
			storage_[idx].data(),
			bytes_,      // maxsize
			0,           // offset
			valid_size,  // size
			cookie,
			&GstFrameRing::free_cookie_cb
		);

		if (!buf) {
			// 建 buffer 失敗：把 slot 還回去，並補回 inuse_now_
			inuse_[idx].store(false, std::memory_order_release);

			// try_acquire_slot() 成功時已經 inuse_now_++ 了，這裡要補回去
			// 避免 underflow：正常情況不會 <0，但仍保守寫法
			uint32_t now = inuse_now_.load(std::memory_order_relaxed);
			if (now > 0) {
				inuse_now_.fetch_sub(1, std::memory_order_relaxed);
			}

			delete cookie;
			return nullptr;
		}

		return buf;
	}

    uint64_t acquire_fail() const { return acquire_fail_.load(std::memory_order_relaxed); }
    uint64_t acquire_ok()   const { return acquire_ok_.load(std::memory_order_relaxed); }
    uint32_t inuse_now()    const { return inuse_now_.load(std::memory_order_relaxed); }
    uint32_t inuse_max()    const { return inuse_max_.load(std::memory_order_relaxed); }

    void reset_stats() {
        acquire_fail_.store(0, std::memory_order_relaxed);
        acquire_ok_.store(0, std::memory_order_relaxed);
        inuse_now_.store(0, std::memory_order_relaxed);
        inuse_max_.store(0, std::memory_order_relaxed);
    }
	
    void reset() {
        // 清 counters / flags
        wr_.store(0, std::memory_order_relaxed);
        // 其他 atomic counters 全部 store(0)
        // ...

        // 清容器（視你的設計）
        storage_.clear();
        inuse_.reset();

        slots_ = 0;
        bytes_ = 0;
    }
	
private:
    struct Cookie { GstFrameRing* ring; size_t idx; };

    static void free_cookie_cb(gpointer user_data) {
        auto* c = static_cast<Cookie*>(user_data);
        if (c && c->ring) {
            c->ring->release_slot(c->idx);
        }
        delete c;
    }

private:
    size_t slots_ = 0;
    size_t bytes_ = 0;

    std::vector<std::vector<uint8_t>> storage_;
    std::unique_ptr<std::atomic_bool[]> inuse_;

    std::atomic_size_t wr_{0};
    std::atomic<uint64_t> acquire_fail_{0};
    std::atomic<uint64_t> acquire_ok_{0};
    std::atomic<uint32_t> inuse_now_{0};
    std::atomic<uint32_t> inuse_max_{0};
};
