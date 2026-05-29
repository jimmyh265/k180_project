#include "remap_rgba_kernel.h"

#include <cuda_runtime.h>
#include <device_launch_parameters.h>
#include <stdint.h>
#include <stdio.h>

static __device__ __forceinline__ unsigned char sat_u8(float v)
{
    v = (v < 0.0f) ? 0.0f : v;
    v = (v > 255.0f) ? 255.0f : v;
    return (unsigned char)(v + 0.5f);
}

static __device__ __forceinline__ uchar4 load_rgba_const_border(
    const uchar4* src,
    int src_w,
    int src_h,
    size_t src_pitch_elems,
    int x,
    int y)
{
    if ((unsigned)x >= (unsigned)src_w || (unsigned)y >= (unsigned)src_h) {
        return make_uchar4(0, 0, 0, 0);
    }
    return src[(size_t)y * src_pitch_elems + x];
}

static __global__ void remap_rgba_bilinear_const_kernel(
    const uchar4* __restrict__ src,
    int src_w,
    int src_h,
    size_t src_pitch_elems,

    uchar4* __restrict__ dst,
    int dst_w,
    int dst_h,
    size_t dst_pitch_elems,

    const float* __restrict__ map_x,
    size_t mapx_pitch_elems,
    const float* __restrict__ map_y,
    size_t mapy_pitch_elems)
{
    const int x = blockIdx.x * blockDim.x + threadIdx.x;
    const int y = blockIdx.y * blockDim.y + threadIdx.y;

    if (x >= dst_w || y >= dst_h) return;

    const float fx = map_x[(size_t)y * mapx_pitch_elems + x];
    const float fy = map_y[(size_t)y * mapy_pitch_elems + x];

    const int x0 = (int)floorf(fx);
    const int y0 = (int)floorf(fy);
    const int x1 = x0 + 1;
    const int y1 = y0 + 1;

    const float ax = fx - (float)x0;
    const float ay = fy - (float)y0;

    const float w00 = (1.0f - ax) * (1.0f - ay);
    const float w01 = ax * (1.0f - ay);
    const float w10 = (1.0f - ax) * ay;
    const float w11 = ax * ay;

    const uchar4 p00 = load_rgba_const_border(src, src_w, src_h, src_pitch_elems, x0, y0);
    const uchar4 p01 = load_rgba_const_border(src, src_w, src_h, src_pitch_elems, x1, y0);
    const uchar4 p10 = load_rgba_const_border(src, src_w, src_h, src_pitch_elems, x0, y1);
    const uchar4 p11 = load_rgba_const_border(src, src_w, src_h, src_pitch_elems, x1, y1);

    float r = w00 * p00.x + w01 * p01.x + w10 * p10.x + w11 * p11.x;
    float g = w00 * p00.y + w01 * p01.y + w10 * p10.y + w11 * p11.y;
    float b = w00 * p00.z + w01 * p01.z + w10 * p10.z + w11 * p11.z;
    float a = w00 * p00.w + w01 * p01.w + w10 * p10.w + w11 * p11.w;

    dst[(size_t)y * dst_pitch_elems + x] =
        make_uchar4(sat_u8(r), sat_u8(g), sat_u8(b), sat_u8(a));
}

bool launchRemapRGBA_Bilinear_ConstBorder(
    const cv::cuda::GpuMat& src_rgba,
    cv::cuda::GpuMat& dst_rgba,
    const cv::cuda::GpuMat& map_x,
    const cv::cuda::GpuMat& map_y,
    cudaStream_t stream)
{
    if (src_rgba.empty() || dst_rgba.empty() || map_x.empty() || map_y.empty()) {
        fprintf(stderr, "[remap_kernel] empty input\n");
        return false;
    }

    if (src_rgba.type() != CV_8UC4) {
        fprintf(stderr, "[remap_kernel] src type must be CV_8UC4\n");
        return false;
    }

    if (dst_rgba.type() != CV_8UC4) {
        fprintf(stderr, "[remap_kernel] dst type must be CV_8UC4\n");
        return false;
    }

    if (map_x.type() != CV_32FC1 || map_y.type() != CV_32FC1) {
        fprintf(stderr, "[remap_kernel] map_x/map_y must be CV_32FC1\n");
        return false;
    }

    if (map_x.size() != map_y.size()) {
        fprintf(stderr, "[remap_kernel] map_x/map_y size mismatch\n");
        return false;
    }

    if (dst_rgba.size() != map_x.size()) {
        fprintf(stderr, "[remap_kernel] dst size must equal map size\n");
        return false;
    }

    const int src_w = src_rgba.cols;
    const int src_h = src_rgba.rows;
    const int dst_w = dst_rgba.cols;
    const int dst_h = dst_rgba.rows;

    const uchar4* src_ptr = reinterpret_cast<const uchar4*>(src_rgba.ptr<uchar4>());
    uchar4* dst_ptr = reinterpret_cast<uchar4*>(dst_rgba.ptr<uchar4>());

    const float* mapx_ptr = map_x.ptr<float>();
    const float* mapy_ptr = map_y.ptr<float>();

    const size_t src_pitch_elems  = src_rgba.step / sizeof(uchar4);
    const size_t dst_pitch_elems  = dst_rgba.step / sizeof(uchar4);
    const size_t mapx_pitch_elems = map_x.step / sizeof(float);
    const size_t mapy_pitch_elems = map_y.step / sizeof(float);

    const dim3 block(32, 8);
    const dim3 grid((dst_w + block.x - 1) / block.x,
                    (dst_h + block.y - 1) / block.y);

    remap_rgba_bilinear_const_kernel<<<grid, block, 0, stream>>>(
        src_ptr,
        src_w,
        src_h,
        src_pitch_elems,
        dst_ptr,
        dst_w,
        dst_h,
        dst_pitch_elems,
        mapx_ptr,
        mapx_pitch_elems,
        mapy_ptr,
        mapy_pitch_elems);

    cudaError_t ce = cudaGetLastError();
    if (ce != cudaSuccess) {
        fprintf(stderr, "[remap_kernel] launch failed: %s\n", cudaGetErrorString(ce));
        return false;
    }

    return true;
}
