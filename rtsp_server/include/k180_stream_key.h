#pragma once
#include <cstdint>

namespace k180 {

enum class StreamGroup : uint8_t { S1 = 0, S2 = 1, MAX };
enum class StreamView  : uint8_t { V1234 = 0, V1 = 1, V2 = 2, V3 = 3, V4 = 4, MAX };

struct StreamKey {
    StreamGroup g;
    StreamView  v;
};

inline constexpr int kGroups  = static_cast<int>(StreamGroup::MAX); // 2
inline constexpr int kViews   = static_cast<int>(StreamView::MAX);  // 5
inline constexpr int kStreams = kGroups * kViews;                   // 10

inline constexpr int stream_index(StreamKey k) {
    return static_cast<int>(k.g) * kViews + static_cast<int>(k.v);
}

inline constexpr const char* group_name(StreamGroup g) {
    return (g == StreamGroup::S1) ? "s1" : "s2";
}

inline constexpr int view_number(StreamView v) {
    return (v == StreamView::V1234) ? 1234 : static_cast<int>(v);
}

} // namespace k180

