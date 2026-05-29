#include "k180_runtime.h"
#include <cuda.h> 
#include <mutex>
#include <cstdio>
#include <cstdlib>

namespace k180::runtime {
	// UserConfig cfggg;
K180Runtime& rt() {
    static K180Runtime g;
    return g;
}

// ---- CUDA primary context (single instance in this TU) ----
static std::once_flag g_cuda_once;
static CUcontext g_cu_ctx = nullptr;

static inline void ck_cu(CUresult r, const char* msg) {
    if (r != CUDA_SUCCESS) {
        const char* s = nullptr;
        cuGetErrorString(r, &s);
        std::fprintf(stderr, "[CUDA] %s failed: %s\n", msg, s ? s : "unknown");
        std::abort();
    }
}

void init_cuda_primary_ctx_once() {
    std::call_once(g_cuda_once, []{
        ck_cu(cuInit(0), "cuInit");
        CUdevice dev;
        ck_cu(cuDeviceGet(&dev, 0), "cuDeviceGet(0)");
        ck_cu(cuDevicePrimaryCtxRetain(&g_cu_ctx, dev), "cuDevicePrimaryCtxRetain");
        ck_cu(cuCtxSetCurrent(g_cu_ctx), "cuCtxSetCurrent(init)");
    });
}

void cuda_set_current_for_thread(const char* /*tag*/) {
    if (!g_cu_ctx) init_cuda_primary_ctx_once();
    ck_cu(cuCtxSetCurrent(g_cu_ctx), "cuCtxSetCurrent(thread)");
}

}