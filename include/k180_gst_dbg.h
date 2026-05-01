#pragma once
#include <gst/gst.h>
#include <chrono>

#if defined(GST_DBG_MSG) && GST_DBG_MSG
  #define GSTD(...) g_printerr(__VA_ARGS__)
#else
  #define GSTD(...) do{}while(0)
#endif

namespace k180::gstdbg {

struct RateMon {
	std::string tag = "";
    std::chrono::steady_clock::time_point last{};
    double dts_ms[30]{};
    int idx = 0, cnt = 0;
    uint64_t n = 0;
};

inline void ratemon_hit(RateMon& m) {
    using clock = std::chrono::steady_clock;
    auto now = clock::now();
    if (m.last.time_since_epoch().count() != 0) {
        double dt = std::chrono::duration<double, std::milli>(now - m.last).count();
        m.dts_ms[m.idx] = dt;
        m.idx = (m.idx + 1) % 30;
        if (m.cnt < 30) m.cnt++;

        m.n++;
        if ((m.n % 60) == 0) {
            double sum = 0.0;
            for (int i = 0; i < m.cnt; ++i) sum += m.dts_ms[i];
            double avg = (m.cnt ? sum / m.cnt : 0.0);
            double fps = (avg > 0.0) ? (1000.0 / avg) : 0.0;
#if defined(GST_DBG_MSG) && GST_DBG_MSG			
            GSTD("[MON:%s] avg=%.3f ms (%.2f fps)\n", m.tag.c_str(), avg, fps);
#else
			(void)fps;
#endif
        }
    } else {
        m.n = 1;
    }
    m.last = now;
}

inline GstPadProbeReturn padprobe_rate(GstPad*, GstPadProbeInfo* info, gpointer user_data) {
    if (!(info->type & GST_PAD_PROBE_TYPE_BUFFER)) return GST_PAD_PROBE_OK;
    auto* m = static_cast<RateMon*>(user_data);
    ratemon_hit(*m);
    return GST_PAD_PROBE_OK;
}

} // namespace k180::gstdbg

