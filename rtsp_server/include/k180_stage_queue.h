#pragma once

#include <array>
#include <cstdint>
#include <cstddef>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <utility>   // std::move

// Forward declarations to avoid pulling heavy GStreamer headers here.
// In the .cpp that uses these pointers, include <gst/gst.h> and <gst/app/gstappsink.h>.
struct _GstElement;
struct _GstAppSink;
using GstElement = _GstElement;
using GstAppSink = _GstAppSink;

template<typename T>
class StageQueue {
public:
    explicit StageQueue(size_t max_items) : max_(max_items) {}

    // drop-oldest when full
    void push(T item) {
        std::unique_lock<std::mutex> lk(m_);
        if (closed_) return;

        while (q_.size() >= max_) {
            q_.pop(); // drop oldest
        }
        q_.push(std::move(item));
        lk.unlock();
        cv_.notify_one();
    }

    // non-blocking push variant (same semantics here, kept for clarity)
    bool try_push(T item) {
        std::unique_lock<std::mutex> lk(m_);
        if (closed_) return false;
        while (q_.size() >= max_) q_.pop();
        q_.push(std::move(item));
        lk.unlock();
        cv_.notify_one();
        return true;
    }

    bool pop(T& out) {
        std::unique_lock<std::mutex> lk(m_);
        cv_.wait(lk, [&]{ return closed_ || !q_.empty(); });
        if (q_.empty()) return false;
        out = std::move(q_.front());
        q_.pop();
        return true;
    }

    // pop latest (drop all but newest). for ultra-low latency paths.
    bool pop_latest(T& out) {
        std::unique_lock<std::mutex> lk(m_);
        cv_.wait(lk, [&]{ return closed_ || !q_.empty(); });
        if (q_.empty()) return false;

        while (q_.size() > 1) q_.pop();  // drop old
        out = std::move(q_.front());
        q_.pop();
        return true;
    }

	bool try_pop_latest(T& out) {
		std::unique_lock<std::mutex> lk(m_);
		if (q_.empty()) return false;
		while (q_.size() > 1) q_.pop();  // drop old
		out = std::move(q_.front());
		q_.pop();
		return true;
	}
	
	bool try_pop(T& out) {
		std::unique_lock<std::mutex> lk(m_);
		if (q_.empty()) return false;
		out = std::move(q_.front());
		q_.pop();
		return true;
	}

    void close() {
		{
			std::lock_guard<std::mutex> lk(m_);
			closed_ = true;
		}
		cv_.notify_all();
    }

private:
    std::mutex m_;
    std::condition_variable cv_;
    std::queue<T> q_;
    size_t max_;
    bool closed_ = false;
};
