//
// postprocess.cu
//
#include "postprocess.h"
#include "types.h"

#include <cuda_runtime.h>
#include <cstdio>
#include <algorithm>

#define THREADS_PER_BLOCK 256

// ----------------------------------------------------------------------------
// decode kernel
// raw predict layout:
//   predict[0] = count (float)
//   predict[1 ...] = detection entries
//
// each input entry stride = predict_element
//
// output parray layout:
//   parray[0] = kept_count (float)
//   parray[1 ...] = [left, top, right, bottom, conf, class_id, keep_flag]
// each output entry stride = bbox_element
// ----------------------------------------------------------------------------
__global__ void decode_kernel(const float* predict,
                              int predict_element,
                              float confidence_threshold,
                              float* parray,
                              int bbox_element,
                              int max_objects)
{
    int idx = blockIdx.x * blockDim.x + threadIdx.x;

    int count = static_cast<int>(predict[0]);
    if (idx >= count) return;

    const float* pitem = predict + 1 + idx * predict_element;

    float confidence = pitem[4];
    if (confidence < confidence_threshold) return;

    // parray[0] is a float counter, keep old layout semantics
    int out_index = static_cast<int>(atomicAdd(parray, 1.0f));
    if (out_index >= max_objects) return;

    float* pout_item = parray + 1 + out_index * bbox_element;
    pout_item[0] = pitem[0];   // left
    pout_item[1] = pitem[1];   // top
    pout_item[2] = pitem[2];   // right
    pout_item[3] = pitem[3];   // bottom
    pout_item[4] = confidence; // confidence
    pout_item[5] = pitem[5];   // class / label
    pout_item[6] = 1.0f;       // keep flag
}

// ----------------------------------------------------------------------------
// IoU
// ----------------------------------------------------------------------------
__device__ float box_iou(float aleft, float atop, float aright, float abottom,
                         float bleft, float btop, float bright, float bbottom)
{
    float cleft   = max(aleft, bleft);
    float ctop    = max(atop,  btop);
    float cright  = min(aright, bright);
    float cbottom = min(abottom, bbottom);

    float c_area = max(cright - cleft, 0.0f) * max(cbottom - ctop, 0.0f);
    if (c_area <= 0.0f) return 0.0f;

    float a_area = max(aright - aleft, 0.0f) * max(abottom - atop, 0.0f);
    float b_area = max(bright - bleft, 0.0f) * max(bbottom - btop, 0.0f);

    float denom = a_area + b_area - c_area;
    if (denom <= 0.0f) return 0.0f;

    return c_area / denom;
}

// ----------------------------------------------------------------------------
// NMS kernel
// input/output bbox layout:
//   [left, top, right, bottom, conf, class_id, keep_flag]
// ----------------------------------------------------------------------------
__global__ void nms_kernel(float* bboxes,
                           int bbox_element,
                           float nms_threshold)
{
    int idx = blockIdx.x * blockDim.x + threadIdx.x;

    int count = static_cast<int>(bboxes[0]);
    if (idx >= count) return;

    float* pcurrent = bboxes + 1 + idx * bbox_element;
    if (static_cast<int>(pcurrent[6]) != 1) return;

    for (int i = 0; i < count; ++i) {
        if (i == idx) continue;

        float* pitem = bboxes + 1 + i * bbox_element;
        if (pitem[5] != pcurrent[5]) continue;

        // 只有更高分，或同分但 index 更小者，才能 suppress current
        // higher confidence wins;
        // if equal confidence, smaller index wins
        if (pitem[4] > pcurrent[4] || (pitem[4] == pcurrent[4] && i < idx)) {
            float iou = box_iou(pcurrent[0], pcurrent[1], pcurrent[2], pcurrent[3],
                                pitem[0],    pitem[1],    pitem[2],    pitem[3]);
            if (iou > nms_threshold) {
                pcurrent[6] = 0.0f;
                return;
            }
        }
    }
}


// ----------------------------------------------------------------------------
// GPU kernel: convert decode_ptr_device -> Detection array
// decode_ptr_device layout:
//   decode_ptr_device[0] = count (float)
//   decode_ptr_device[1 ...] = bbox entries
// ----------------------------------------------------------------------------
__global__ void process_decode_ptr_device_kernel(const float* decode_ptr_device,
                                                 int bbox_element,
                                                 int count,
                                                 Detection* out,
                                                 int* out_count,
                                                 int max_output)
{
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= count) return;

    int basic_pos = 1 + idx * bbox_element;

    int keep_flag = static_cast<int>(decode_ptr_device[basic_pos + 6]);
    if (keep_flag != 1) return;

    Detection det{};
    det.bbox[0]  = decode_ptr_device[basic_pos + 0];
    det.bbox[1]  = decode_ptr_device[basic_pos + 1];
    det.bbox[2]  = decode_ptr_device[basic_pos + 2];
    det.bbox[3]  = decode_ptr_device[basic_pos + 3];
    det.conf     = decode_ptr_device[basic_pos + 4];
    det.class_id = decode_ptr_device[basic_pos + 5];

    int pos = atomicAdd(out_count, 1);
    if (pos < max_output)
    {
        out[pos] = det;
    }
}

// ----------------------------------------------------------------------------
// helper: map raw bbox (letterbox/input space) -> image ltrb
// same math as your CPU get_rect/map_raw_bbox_to_image_ltrb logic
// ----------------------------------------------------------------------------
__device__ inline void map_raw_bbox_to_image_ltrb_device(float bbox[4],
                                                         int img_w, int img_h,
                                                         int input_w, int input_h)
{
    float l, r, t, b;
    float r_w = input_w / (img_w * 1.0f);
    float r_h = input_h / (img_h * 1.0f);

    if (r_h > r_w) {
        l = bbox[0];
        r = bbox[2];
        t = bbox[1] - (input_h - r_w * img_h) / 2.0f;
        b = bbox[3] - (input_h - r_w * img_h) / 2.0f;

        l /= r_w;
        r /= r_w;
        t /= r_w;
        b /= r_w;
    } else {
        l = bbox[0] - (input_w - r_h * img_w) / 2.0f;
        r = bbox[2] - (input_w - r_h * img_w) / 2.0f;
        t = bbox[1];
        b = bbox[3];

        l /= r_h;
        r /= r_h;
        t /= r_h;
        b /= r_h;
    }

    l = fmaxf(0.0f, fminf(l, img_w - 1.0f));
    t = fmaxf(0.0f, fminf(t, img_h - 1.0f));
    r = fmaxf(0.0f, fminf(r, img_w - 1.0f));
    b = fmaxf(0.0f, fminf(b, img_h - 1.0f));

    bbox[0] = l;
    bbox[1] = t;
    bbox[2] = r;
    bbox[3] = b;
}

// ----------------------------------------------------------------------------
// helper: ltrb -> xywh
// output is left, top, width, height
// ----------------------------------------------------------------------------
__device__ inline void convert_ltrb_to_xywh_device(float bbox[4])
{
    float l = bbox[0];
    float t = bbox[1];
    float r = bbox[2];
    float b = bbox[3];

    bbox[0] = l;
    bbox[1] = t;
    bbox[2] = fmaxf(0.0f, r - l);
    bbox[3] = fmaxf(0.0f, b - t);
}

// ----------------------------------------------------------------------------
// finalize kernel:
// decode_ptr_device -> compacted final Detection array
// ----------------------------------------------------------------------------
__global__ void finalize_detections_kernel(const float* decode_ptr_device,
                                           int bbox_element,
                                           int img_w,
                                           int img_h,
                                           int input_w,
                                           int input_h,
                                           Detection* out,
                                           int* out_count,
                                           int max_output)
{
    int idx = blockIdx.x * blockDim.x + threadIdx.x;

    // decode_ptr_device[0] = count
    int count = static_cast<int>(decode_ptr_device[0]);

    // 注意：這裡用 max_output 當安全上限，避免 thread 太多或 count 異常
    if (idx >= count || idx >= max_output) return;

    int p = 1 + idx * bbox_element;
    int keep_flag = static_cast<int>(decode_ptr_device[p + 6]);
    if (keep_flag != 1) return;

    Detection det{};
    det.bbox[0] = decode_ptr_device[p + 0];
    det.bbox[1] = decode_ptr_device[p + 1];
    det.bbox[2] = decode_ptr_device[p + 2];
    det.bbox[3] = decode_ptr_device[p + 3];
    det.conf    = decode_ptr_device[p + 4];
    det.class_id= decode_ptr_device[p + 5];

    // model/input space -> original image space
    map_raw_bbox_to_image_ltrb_device(det.bbox, img_w, img_h, input_w, input_h);

    // ltrb -> xywh
    convert_ltrb_to_xywh_device(det.bbox);

    int out_idx = atomicAdd(out_count, 1);
    if (out_idx < max_output) {
        out[out_idx] = det;
    }
}

// ----------------------------------------------------------------------------
// Host interface
// ----------------------------------------------------------------------------

// 注意：
// predict_element = raw predict 每筆資料的 stride(float數量)
// 對你目前的舊版語意而言，通常應該傳：sizeof(Detection) / sizeof(float)
void cuda_decode(float* predict,
                 int num_bboxes,
                 float confidence_threshold,
                 float* parray,
                 int predict_element,
                 int bbox_element,
                 int max_objects,
                 cudaStream_t stream)
{
    if (num_bboxes <= 0 || max_objects <= 0) return;

    int threads = THREADS_PER_BLOCK;
    int blocks  = (num_bboxes + threads - 1) / threads;

    decode_kernel<<<blocks, threads, 0, stream>>>(
        predict,
        predict_element,
        confidence_threshold,
        parray,
        bbox_element,
        max_objects
    );

    cudaError_t err = cudaGetLastError();
    if (err != cudaSuccess)
    {
        fprintf(stderr, "decode_kernel launch failed: %s\n", cudaGetErrorString(err));
    }
}

void cuda_nms(float* parray,
              int bbox_element,
              float nms_threshold,
              int max_objects,
              cudaStream_t stream)
{
    if (max_objects <= 0) return;

    int threads = THREADS_PER_BLOCK;
    if (max_objects < threads) threads = max_objects;
    if (threads <= 0) return;

    int blocks = (max_objects + threads - 1) / threads;

    nms_kernel<<<blocks, threads, 0, stream>>>(
        parray,
        bbox_element,
        nms_threshold
    );

    cudaError_t err = cudaGetLastError();
    if (err != cudaSuccess)
    {
        fprintf(stderr, "nms_kernel launch failed: %s\n", cudaGetErrorString(err));
    }
}

// host wrapper:
// 1. 先從 device 讀出 decode_ptr_device[0] (float count)
// 2. 轉成 int count
// 3. launch process_decode_ptr_device_kernel
void cuda_process_decode_ptr_device(const float* decode_ptr_device,
                                    int bbox_element,
                                    Detection* out,
                                    int* out_count,
                                    int max_output,
                                    cudaStream_t stream)
{
    if (max_output <= 0) return;

    float count_f = 0.0f;
    cudaError_t err = cudaMemcpyAsync(&count_f,
                                      decode_ptr_device,
                                      sizeof(float),
                                      cudaMemcpyDeviceToHost,
                                      stream);
    if (err != cudaSuccess)
    {
        fprintf(stderr, "cudaMemcpyAsync(count) failed: %s\n", cudaGetErrorString(err));
        return;
    }

    err = cudaStreamSynchronize(stream);
    if (err != cudaSuccess)
    {
        fprintf(stderr, "cudaStreamSynchronize failed: %s\n", cudaGetErrorString(err));
        return;
    }

    int count = static_cast<int>(count_f);
    if (count <= 0) return;

    int threads = THREADS_PER_BLOCK;
    if (count < threads) threads = count;
    if (threads <= 0) return;

    int blocks = (count + threads - 1) / threads;

    process_decode_ptr_device_kernel<<<blocks, threads, 0, stream>>>(
        decode_ptr_device,
        bbox_element,
        count,
        out,
        out_count,
        max_output
    );

    err = cudaGetLastError();
    if (err != cudaSuccess)
    {
        fprintf(stderr, "process_decode_ptr_device_kernel launch failed: %s\n",
                cudaGetErrorString(err));
    }
}

void cuda_finalize_detections(const float* decode_ptr_device,
                              int bbox_element,
                              int img_w,
                              int img_h,
                              int input_w,
                              int input_h,
                              Detection* out_device,
                              int* out_count_device,
                              int max_output,
                              cudaStream_t stream)
{
    if (max_output <= 0) return;

    cudaError_t err = cudaMemsetAsync(out_count_device, 0, sizeof(int), stream);
    if (err != cudaSuccess) {
        fprintf(stderr, "cudaMemsetAsync(out_count_device) failed: %s\n", cudaGetErrorString(err));
        return;
    }

    int threads = THREADS_PER_BLOCK;
    int blocks  = (max_output + threads - 1) / threads;

    finalize_detections_kernel<<<blocks, threads, 0, stream>>>(
        decode_ptr_device,
        bbox_element,
        img_w,
        img_h,
        input_w,
        input_h,
        out_device,
        out_count_device,
        max_output
    );

    err = cudaGetLastError();
    if (err != cudaSuccess) {
        fprintf(stderr, "finalize_detections_kernel launch failed: %s\n",
                cudaGetErrorString(err));
    }
}