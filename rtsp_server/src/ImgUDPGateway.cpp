//==============================================================================
/*
    File Name:      ImgUDPGateway.cpp

    Created:        2023//

    Author:         ychsiao168

    Description:

*/
//==============================================================================
//
//
//  Copyright (C) 2023 XXX Technology Co. Ltd. All rights reserved.
//
//
//==============================================================================
//------------------------------------------------------------------------------
//  Include Files
//------------------------------------------------------------------------------
#include <ctime>
#include <memory>
// STD
#include <chrono>
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <unistd.h>
// 3RD-PARTY
#include <fmt/format.h>
#include <opencv2/highgui.hpp>
#include <opencv2/cudaimgproc.hpp>	// cv::cuda::cvtColor
// LOCAL
#include "ImgUDPGateway.hpp"
#include "../common/utils.hpp"
// #include "config.h"
#include "cuda_utils.h"
#include "preprocess.h"
#include "postprocess.h"
#include "k180_osd_slots.h"
#include "k180_nvtracker.h"

// #include <opencv2/core/cuda.hpp>
//------------------------------------------------------------------------------
//  Local Defines
//------------------------------------------------------------------------------
#define PIC_FORMAT  (0) // 0: png, 1: bmp, 2:jpg, for MODE 0,1
#define EN_TRACKING (0)	// 20260316 off (1)
#define EN_OPENGL   (0)
#define EN_DBG_MAIN (1)
#define EN_DBG_TRK  (0)

#define COLOR_TRK   cv::Scalar(255,   0, 255)   // BGR
#define COLOR_BAD   cv::Scalar(  0,   0, 255)   // BGR
//------------------------------------------------------------------------------
//  Local Data Structures
//------------------------------------------------------------------------------
const string ImgUDPGateway::WINDOW_NAME = "ImgUDPGateway";
const string ImgUDPGateway::MODE[5] =
{
    "NO_SAVE", "DO_SAVE", "FORWARD", "LOCAL", "RELEASE1",
};
const string ImgUDPGateway::TRACKER_TYPE[8] =
{
    "BOOSTING",   "MIL",    "KCF",   "TLD",
    "MEDIANFLOW", "GOTURN", "MOSSE", "CSRT",
};

const std::vector<int> ImgUDPGateway::PNG_COMPRESSION_PARAMS
{
    cv::IMWRITE_PNG_COMPRESSION, 4,
    cv::IMWRITE_PNG_STRATEGY,    cv::IMWRITE_PNG_STRATEGY_DEFAULT,
};

const std::vector<int> ImgUDPGateway::JPG_COMPRESSION_PARAMS
{
    cv::IMWRITE_JPEG_QUALITY,   100,
};
//------------------------------------------------------------------------------
//  Global Variables
//------------------------------------------------------------------------------
const static int kOutputSize = kMaxNumOutputBbox * sizeof(Detection) / sizeof(float) + 1;
//------------------------------------------------------------------------------
//  Local Prototypes
//------------------------------------------------------------------------------
// #if 0
void deserialize_engine(std::string& engine_name, IRuntime** runtime, ICudaEngine** engine,
                        IExecutionContext** context);
void prepare_buffer(ICudaEngine* engine, float** input_buffer_device, float** output_buffer_device,
                    float** output_buffer_host, float** decode_ptr_host, float** decode_ptr_device,
                    std::string cuda_post_process);
// void infer(IExecutionContext& context, cudaStream_t& stream, void** buffers, float* output, int batchsize,
           // float* decode_ptr_host, float* decode_ptr_device, int model_bboxes, std::string cuda_post_process);
void infer_gpu(IExecutionContext& context, cudaStream_t& stream, void** buffers, float* output, int batchsize,
           float* decode_ptr_host, float* decode_ptr_device, int model_bboxes, std::string cuda_post_process);
// #endif
/*##############################################################################
##  Public Function Implementation                                            ##
##############################################################################*/
//------------------------------------------------------------------------------
//
//
//
//
//------------------------------------------------------------------------------
#if 0
ImgUDPGateway::ImgUDPGateway(string iface_in, int port, Mode mode, string iface_out, string engine, int tracker_type):
        m_iface_in(iface_in), m_port(port), m_mode(mode), m_iface_out(iface_out), m_engine_fname(engine), m_tracker_type(tracker_type)
{
    // m_frame_src = new FrameSource(iface_in);

    // ===============================
    // init m_sock_img for sendmmsg
    // ===============================
    if(!Utils::isValidIpAddress(iface_out))
    {
        m_sock_img = Utils::init_socket("sock_img", m_iface_out, -1);
    }

    memset(&m_dstaddr, 0x00, sizeof(struct sockaddr_in));
    m_dstaddr.sin_family = AF_INET;
    m_dstaddr.sin_port = htons(m_port);
    m_dstaddr.sin_addr.s_addr = INADDR_BROADCAST;

    // ===============================
    // init m_sock_cmd for receiving nvcmd
    // ===============================
    m_sock_cmd = Utils::init_socket("sock_nvcmd", "", NVCMD_PORT);

    // ===============================
    // init m_sock_det for sending det boxes
    // ===============================
    if(!Utils::isValidIpAddress(iface_out))
    {
        m_sock_det = Utils::init_socket("sock_det", m_iface_out, -1);   // same as m_sock_img
    }

    memset(&m_detaddr, 0x00, sizeof(struct sockaddr_in));
    m_detaddr.sin_family = AF_INET;
    m_detaddr.sin_port = htons(DETBOX_PORT);
    m_detaddr.sin_addr.s_addr = INADDR_BROADCAST;

    // ===============================
    // init m_tracker
    // ===============================
    // _init_tracker();


    // ===============================
    // init mp_netconn
    // ===============================
    if((m_mode == RELEASE1))
    {
        mp_netconn = new Netcmd(m_iface_out);
    }

    // yolo
    // if((m_mode == FORWARD) || (m_mode == LOCAL) || (m_mode == RELEASE1))
    if((m_mode == FORWARD) || (m_mode == LOCAL) || (m_mode == RELEASE1) || (m_mode == NO_SAVE) )
    {
        _init_yolov7();		
    }
    // fmt::print("{}: init done: {}, tracker_type = {}, engine = {}, opencv = {}\n", __func__,
                // MODE[m_mode],
                // TRACKER_TYPE[tracker_type],
                // m_engine_fname,
                // cv::getVersionString());
}
#endif
ImgUDPGateway::ImgUDPGateway(string iface_in, int port, Mode mode,
                             string iface_out, string engine, int tracker_type)
    : m_iface_in(std::move(iface_in))
    , m_port(port)
    , m_mode(mode)
    , m_iface_out(std::move(iface_out))
    , m_engine_fname(std::move(engine))
    , m_tracker_type(tracker_type)
    , m_sock_img(-1)
    , m_sock_cmd(-1)
    , m_sock_det(-1)
    , mp_netconn(nullptr)
    , m_runtime(nullptr)
    , m_engine(nullptr)
    , m_context(nullptr)
    , m_stream(nullptr)
    , m_output_buffer_host(nullptr)
    , m_decode_ptr_host(nullptr)
    , m_decode_ptr_device(nullptr)
    , m_model_bboxes(0)
    , m_run(false)
{
    m_device_buffers[0] = nullptr;
    m_device_buffers[1] = nullptr;

#if EN_TRACKING
    m_tracker.release();
#endif

    if (!Utils::isValidIpAddress(iface_out)) {
        m_sock_img = Utils::init_socket("sock_img", m_iface_out, -1);
    }

    memset(&m_dstaddr, 0x00, sizeof(struct sockaddr_in));
    m_dstaddr.sin_family = AF_INET;
    m_dstaddr.sin_port = htons(m_port);
    m_dstaddr.sin_addr.s_addr = INADDR_BROADCAST;

    m_sock_cmd = Utils::init_socket("sock_nvcmd", "", NVCMD_PORT);

    if (!Utils::isValidIpAddress(iface_out)) {
        m_sock_det = Utils::init_socket("sock_det", m_iface_out, -1);
    }

    memset(&m_detaddr, 0x00, sizeof(struct sockaddr_in));
    m_detaddr.sin_family = AF_INET;
    m_detaddr.sin_port = htons(DETBOX_PORT);
    m_detaddr.sin_addr.s_addr = INADDR_BROADCAST;

    if (m_mode == RELEASE1) {
        mp_netconn = new Netcmd(m_iface_out);
    }

    if ((m_mode == FORWARD) || (m_mode == LOCAL) ||
        (m_mode == RELEASE1) || (m_mode == NO_SAVE)) {
        _init_yolov7();
    }
CUDA_CHECK(cudaMalloc((void**)&m_final_det_device,
                      kMaxNumOutputBbox * sizeof(Detection)));
CUDA_CHECK(cudaMalloc((void**)&m_final_det_count_device,
                      sizeof(int)));

m_final_det_host = new Detection[kMaxNumOutputBbox];

cudaMalloc((void**)&m_det_device, kMaxNumOutputBbox * sizeof(Detection));
cudaMalloc((void**)&m_det_count_device, sizeof(int));

cudaMallocHost((void**)&m_det_host, kMaxNumOutputBbox * sizeof(Detection));
cudaMallocHost((void**)&m_det_count_host, sizeof(int));

*m_det_count_host = 0;
}
//------------------------------------------------------------------------------
//
//
//
//
//------------------------------------------------------------------------------
#if 0
ImgUDPGateway::~ImgUDPGateway()
{
    _deinit_yolov7();
    #if EN_TRACKING
    m_tracker->clear();
    #endif

    if((m_mode == RELEASE1))
    {
        delete mp_netconn;
    }
    // delete m_frame_src;
}
#endif
ImgUDPGateway::~ImgUDPGateway()
{
    fprintf(stderr, "[ImgUDPGateway] ~ImgUDPGateway begin\n");

    _deinit_yolov7();

#if EN_TRACKING
    fprintf(stderr, "[ImgUDPGateway] tracker ptr = %p\n", (void*)m_tracker.get());
    if (m_tracker) {
        m_tracker->clear();
        m_tracker.release();
    }
#endif

    if (m_mode == RELEASE1 && mp_netconn) {
        delete mp_netconn;
        mp_netconn = nullptr;
    }
if (m_final_det_device) {
    cudaFree(m_final_det_device);
    m_final_det_device = nullptr;
}
if (m_final_det_count_device) {
    cudaFree(m_final_det_count_device);
    m_final_det_count_device = nullptr;
}
delete[] m_final_det_host;
m_final_det_host = nullptr;
    fprintf(stderr, "[ImgUDPGateway] ~ImgUDPGateway end\n");


if (m_det_device) {
    cudaFree(m_det_device);
    m_det_device = nullptr;
}
if (m_det_count_device) {
    cudaFree(m_det_count_device);
    m_det_count_device = nullptr;
}
if (m_det_host) {
    cudaFreeHost(m_det_host);
    m_det_host = nullptr;
}
if (m_det_count_host) {
    cudaFreeHost(m_det_count_host);
    m_det_count_host = nullptr;
}
}
//------------------------------------------------------------------------------
//
//
//
//
//------------------------------------------------------------------------------
void ImgUDPGateway::start()
{

}

/*##############################################################################
##  Private Function Implementation                                            ##
##############################################################################*/
//------------------------------------------------------------------------------
//
//
//
//
//------------------------------------------------------------------------------
void ImgUDPGateway::_init_tracker()
{
    switch((TrackerType)(m_tracker_type))
    {
        case BOOSTING:
            m_tracker = cv::legacy::TrackerBoosting::create();
            break;
        case MIL:
            m_tracker = cv::legacy::TrackerMIL::create();
            break;
        case KCF:
        m_tracker = cv::legacy::TrackerKCF::create();
                    break;
        case TLD:
            m_tracker = cv::legacy::TrackerTLD::create();
            break;
        case MEDIANFLOW:
            m_tracker = cv::legacy::TrackerMedianFlow::create();
            break;
        case GOTURN:
            // TODO
            // m_tracker = cv::TrackerGOTURN::create();
            break;
        case MOSSE:
            m_tracker = cv::legacy::TrackerMOSSE::create();
            break;
        case CSRT:
            m_tracker = cv::legacy::TrackerCSRT::create();
            break;
        default:
            fmt::print("invalid tracer type: {}\n", m_tracker_type);
            exit(0);
            break;
    }
}

//------------------------------------------------------------------------------
//
//
//
//
//------------------------------------------------------------------------------
void ImgUDPGateway::_init_yolov7()
{
    cudaSetDevice(kGpuId);
    deserialize_engine(m_engine_fname, &m_runtime, &m_engine, &m_context);
    CUDA_CHECK(cudaStreamCreate(&m_stream));
    cuda_preprocess_init(kMaxInputImageSize);
	auto m_out_dims = m_engine->getTensorShape(kOutputTensorName);
	m_model_bboxes = m_out_dims.d[1];
    prepare_buffer(m_engine, &m_device_buffers[0], &m_device_buffers[1], &m_output_buffer_host, &m_decode_ptr_host, &m_decode_ptr_device, m_cuda_post_process);
	// int nIO = m_engine->getNbIOTensors(); // input + output tensor 數量
	// for (int i = 0; i < nIO; ++i) {
		// const char* name = m_engine->getIOTensorName(i);
		// auto shape = m_engine->getTensorShape(name);
		// auto dtype = m_engine->getTensorDataType(name);

		// std::cout << "tensor " << name 
				  // << ", dtype=" << (int)dtype 
				  // << ", dims=[";
		// for (int d = 0; d < shape.nbDims; ++d) {
			// std::cout << shape.d[d];
			// if (d < shape.nbDims-1) std::cout << ",";
		// }
		// std::cout << "]\n";
	// }
    // fmt::print("CUDA Init done: kOutputTensorName = {}, kInputTensorName = {}\n", kOutputTensorName, kInputTensorName);
}

//------------------------------------------------------------------------------
//
//
//
//
//------------------------------------------------------------------------------
#if 0
void ImgUDPGateway::_deinit_yolov7()
{
	// #if 0
    if(m_runtime == nullptr) return;

    // Release stream and buffers
    cudaStreamDestroy(m_stream);
    CUDA_CHECK(cudaFree(m_device_buffers[0]));
    CUDA_CHECK(cudaFree(m_device_buffers[1]));
	CUDA_CHECK(cudaFree(m_decode_ptr_device));
	delete[] m_decode_ptr_host;
    delete[] m_output_buffer_host;
    cuda_preprocess_destroy();

    // Destroy the engine
    delete m_context;
    delete m_engine;
    delete m_runtime;
	// #endif
}
#endif

void ImgUDPGateway::_deinit_yolov7()
{
    if (m_stream) {
        cudaStreamDestroy(m_stream);
        m_stream = nullptr;
    }

    if (m_device_buffers[0]) {
        cudaFree(m_device_buffers[0]);
        m_device_buffers[0] = nullptr;
    }

    if (m_device_buffers[1]) {
        cudaFree(m_device_buffers[1]);
        m_device_buffers[1] = nullptr;
    }

    if (m_decode_ptr_device) {
        cudaFree(m_decode_ptr_device);
        m_decode_ptr_device = nullptr;
    }

    delete[] m_decode_ptr_host;
    m_decode_ptr_host = nullptr;

    delete[] m_output_buffer_host;
    m_output_buffer_host = nullptr;

    cuda_preprocess_destroy();

    if (m_context) {
        delete m_context;
        m_context = nullptr;
    }
    if (m_engine) {
        delete m_engine;
        m_engine = nullptr;
    }
    if (m_runtime) {
        delete m_runtime;
        m_runtime = nullptr;
    }
}
//------------------------------------------------------------------------------
//
//  send every NNN packet
//
//  note: sendmmsg is slower than sendto
//------------------------------------------------------------------------------
void ImgUDPGateway::_forward()
{
    uint8_t frmchk = 0x00;
    Packet ipkt;
    Packet3 opkt;
    uint16_t opkt_idx;
    int ret, send_size, send_idx;

    while(m_run)
    {
        m_outq.wait_dequeue(ipkt);

        memcpy(&opkt.pixels[(ipkt.idx%NNN)][0], &ipkt.pixels[0], 432);    // TODO: no memcpy
        // iov[1+(ipkt.idx%NNN)].iov_base = &packet.pixels[0];

        if((ipkt.idx%NNN) == (NNN-1))
        {
            // ex: collect ipkt.idx=0,1,2, then send
            // new header
            opkt_idx = (ipkt.idx - (NNN-1)) / NNN;
            // fmt::print("ipkt.idx = {:4}, opkt_idx = {:4}\n", ipkt.idx, opkt_idx);
            opkt.set_idx(frmchk, opkt_idx);

            if(ipkt.idx == 8693)
            {
                // last packet has id (4 bytes)
                opkt.id = m_frameID++;
            }

            // send all
            send_idx = 0;
            send_size = 2+432*3+(ipkt.idx == 8693?4:0);
            while(send_idx != send_size)
            {
                ret = sendto(m_sock_img, &opkt.data[send_idx], send_size - send_idx, 0, (struct sockaddr *)&m_dstaddr, sizeof(struct sockaddr_in));
                if(ret >= 0)
                {
                    send_idx += ret;
                }
            }

            // toggle frmchk
            if(opkt_idx == ((8694/NNN)-1))
            {
                frmchk = frmchk == 0x00 ? 0x80 : 0x00;
            }
        }
    }
}


//------------------------------------------------------------------------------
// recv nv_cmd from C_Client(m_iface_out)
//
//
//
//------------------------------------------------------------------------------
// int iiiw = 0;
void ImgUDPGateway::_nvcmd_receiver()
{
    int ret;
    NV_CMD rcmd;
    struct iovec   iov = {0};
    struct msghdr  msg = {0};

    iov.iov_base = &rcmd.payload;
    iov.iov_len  = sizeof(rcmd.payload);
    msg.msg_iov     = &iov;
    msg.msg_iovlen  = 1;

    // fmt::print("{} start\n", __func__);

    while(m_run)
    {
        ret = recvmsg(m_sock_cmd, &msg, 0); // block
        if(ret < 0) continue;
        switch(rcmd.payload.cmd)
        {
            case NVCMD_DETECT:
                // fmt::print("NVCMD_DETECT\n");
                m_nv_det = rcmd.payload.onoff;
                break;

            case NVCMD_TRACKING:
                // fmt::print("NVCMD_TRACKING\n")
                m_nv_trk = rcmd.payload.onoff;
                m_trk_bbox.x = rcmd.payload.x;
                m_trk_bbox.y = rcmd.payload.y;
                m_trk_bbox.width = rcmd.payload.w;
                m_trk_bbox.height = rcmd.payload.h;

                if(m_nv_trk)
                {
                    #if EN_TRACKING
                    m_nv_det_prev = m_nv_det;
                    m_tracker->init(*m_pCurFrame, m_trk_bbox);
                    #endif
                }
                else
                {
                    m_nv_det = m_nv_det_prev;
                }
                break;
        }
        // fmt::print("ret={}, cmd={}, onoff={}\n", ret, +rcmd.payload.cmd, +rcmd.payload.onoff);
    }
}

//------------------------------------------------------------------------------
//
//
//
//
//------------------------------------------------------------------------------
std::vector<Detection> ImgUDPGateway::_inference(cv::Mat img)
{
    std::vector<Detection> res;
	
	cuda_preprocess(img.ptr(), img.cols, img.rows, m_device_buffers[0], kInputW, kInputH, m_stream);
	CUDA_CHECK(cudaStreamSynchronize(m_stream));

    // infer(*m_context, m_stream, (void**)m_device_buffers, m_output_buffer_host, kBatchSize, m_decode_ptr_host, m_decode_ptr_device, m_model_bboxes, m_cuda_post_process);

	int count = static_cast<int>(*m_decode_ptr_host);
	process_decode_ptr_host(res, &m_decode_ptr_host[0], bbox_element, img, count);

    return res;
}

static void debug_print_gpumat_info(const cv::cuda::GpuMat& img, const char* tag)
{
    printf("[%s] empty=%d rows=%d cols=%d type=%d ch=%d step=%zu data=%p\n",
           tag,
           img.empty() ? 1 : 0,
           img.rows,
           img.cols,
           img.type(),
           img.channels(),
           img.step,
           (void*)img.ptr<unsigned char>());
}

static inline void map_raw_bbox_to_image_ltrb(
    float bbox[4],
    int img_w,
    int img_h,
    int input_w,
    int input_h)
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

    l = std::max(0.0f, std::min(l, (float)(img_w - 1)));
    r = std::max(0.0f, std::min(r, (float)(img_w - 1)));
    t = std::max(0.0f, std::min(t, (float)(img_h - 1)));
    b = std::max(0.0f, std::min(b, (float)(img_h - 1)));

    bbox[0] = l;
    bbox[1] = t;
    bbox[2] = r;
    bbox[3] = b;
}

static inline void convert_ltrb_to_xywh(float bbox[4])
{
    const float left   = bbox[0];
    const float top    = bbox[1];
    const float right  = bbox[2];
    const float bottom = bbox[3];

	bbox[0] = left;                 // x
	bbox[1] = top;                  // y
	bbox[2] = std::max(0.0f, right - left);   // w
	bbox[3] = std::max(0.0f, bottom - top);   // h
}
#if 0
std::vector<Detection> ImgUDPGateway::_inference_gpu(cv::cuda::GpuMat img)
{

    std::vector<Detection> res;

    return res;
}

namespace {

struct LatestPublishCtx {
    k180::osd::OsdShared* shared = nullptr;
    k180::osd::OsdDetSlot* slot = nullptr;
};


static bool point_in_det(const Detection& d, float x, float y)
{
    const float cx = d.bbox[0];
    const float cy = d.bbox[1];
    const float w  = d.bbox[2];
    const float h  = d.bbox[3];

    const float l = cx - 0.5f * w;
    const float t = cy - 0.5f * h;
    const float r = cx + 0.5f * w;
    const float b = cy + 0.5f * h;

    return (x >= l && x <= r && y >= t && y <= b);
}


static void CUDART_CB publish_latest_host_func(void* userData)
{
    std::unique_ptr<LatestPublishCtx> ctx(static_cast<LatestPublishCtx*>(userData));
    if (!ctx || !ctx->shared || !ctx->slot) return;

    auto* slot = ctx->slot;
    if (!slot->det_count_host) return;

    int count = *slot->det_count_host;

    k180::osd::osd_update_latest_result(*ctx->shared,
                                        slot->frame_seq,
                                        slot->det_host,
                                        count);
}

} // namespace
#endif

bool ImgUDPGateway::publish_track_results_to_slot(
    const std::vector<Detection>& res_track,
    std::uint64_t frame_seq,
    k180::osd::OsdShared& osd_shared)
{
    auto* slot = k180::osd::osd_acquire_free_track_slot(osd_shared);
    if (!slot) {
        osd_shared.track_drop_no_slot.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    if (!slot->det_host || !slot->det_count_host) {
        k180::osd::osd_release_slot(*slot);
        return false;
    }

    int count = static_cast<int>(res_track.size());
    if (count > kMaxNumOutputBbox) {
        count = kMaxNumOutputBbox;
    }

    slot->frame_seq = frame_seq;
    *slot->det_count_host = count;

    if (count > 0) {
        std::memcpy(slot->det_host,
                    res_track.data(),
                    static_cast<size_t>(count) * sizeof(Detection));
    }

    k180::osd::osd_update_latest_track_result(
        osd_shared,
        frame_seq,
        (count > 0) ? slot->det_host : nullptr,
        count);

    slot->state.store(k180::osd::SlotState::INFER_PENDING, std::memory_order_release);

    cudaError_t ce = cudaEventRecord(slot->ready_event, 0);
    if (ce != cudaSuccess) {
        fprintf(stderr,
                "[publish_track_results_to_slot] cudaEventRecord failed: %s\n",
                cudaGetErrorString(ce));
        k180::osd::osd_release_slot(*slot);
        return false;
    }

    return true;
}

bool ImgUDPGateway::publish_detections_to_slot(
    const std::vector<Detection>& res,
    std::uint64_t frame_seq,
    k180::osd::OsdShared& osd_shared)
{
    auto* slot = k180::osd::osd_acquire_free_slot(osd_shared);
    if (!slot) {
        osd_shared.infer_drop_no_slot.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    if (!slot->det_host || !slot->det_count_host) {
        k180::osd::osd_release_slot(*slot);
        return false;
    }

    int count = static_cast<int>(res.size());
    if (count > kMaxNumOutputBbox) {
        count = kMaxNumOutputBbox;
    }

    slot->frame_seq = frame_seq;
    *slot->det_count_host = count;

    if (count > 0) {
        std::memcpy(slot->det_host,
                    res.data(),
                    static_cast<size_t>(count) * sizeof(Detection));
    }

    k180::osd::osd_update_latest_result(
        osd_shared,
        frame_seq,
        (count > 0) ? slot->det_host : nullptr,
        count);

    // 這條新路徑沒有 GPU async work，因此資料此刻已 ready
    slot->state.store(k180::osd::SlotState::INFER_PENDING, std::memory_order_release);

    // 讓 mux_src_pad_probe 的 cudaEventQuery(exact_slot->ready_event) 直接成功
    cudaError_t ce = cudaEventRecord(slot->ready_event, 0);
    if (ce != cudaSuccess) {
        fprintf(stderr,
                "[publish_detections_to_slot] cudaEventRecord failed: %s\n",
                cudaGetErrorString(ce));
        k180::osd::osd_release_slot(*slot);
        return false;
    }

    return true;
}

std::vector<Detection> ImgUDPGateway::_inference_gpu(
    const cv::cuda::GpuMat& img,
    cudaEvent_t src_ready_event)
{
    std::vector<Detection> res;
    cv::Mat local_cmat;

    if (img.empty()) return res;
#if 0
{
    static time_t last_stat_t = 0;

    static uint64_t last_attach_ok = 0;
    static uint64_t last_not_ready = 0;
    static uint64_t last_not_found = 0;
    static uint64_t last_drop_no_slot = 0;

    static uint64_t last_exact_ready_ok = 0;
    static uint64_t last_fallback_used = 0;
    static uint64_t last_fallback_miss = 0;

    time_t now_t = time(nullptr);
    if (now_t != last_stat_t) {
        last_stat_t = now_t;

        uint64_t attach_ok = osd_shared.probe_attach_ok.load(std::memory_order_relaxed);
        uint64_t not_ready = osd_shared.probe_miss_not_ready.load(std::memory_order_relaxed);
        uint64_t not_found = osd_shared.probe_miss_not_found.load(std::memory_order_relaxed);
        uint64_t drop_no_slot = osd_shared.infer_drop_no_slot.load(std::memory_order_relaxed);

        uint64_t exact_ready_ok = osd_shared.probe_exact_ready_ok.load(std::memory_order_relaxed);
        uint64_t fallback_used = osd_shared.probe_fallback_used.load(std::memory_order_relaxed);
        uint64_t fallback_miss = osd_shared.probe_fallback_miss.load(std::memory_order_relaxed);

        fprintf(stderr,
                "[OSD][1s] attach=+%llu not_ready=+%llu not_found=+%llu drop_no_slot=+%llu | "
                "exact=+%llu fallback_used=+%llu fallback_miss=+%llu\n",
                (unsigned long long)(attach_ok - last_attach_ok),
                (unsigned long long)(not_ready - last_not_ready),
                (unsigned long long)(not_found - last_not_found),
                (unsigned long long)(drop_no_slot - last_drop_no_slot),
                (unsigned long long)(exact_ready_ok - last_exact_ready_ok),
                (unsigned long long)(fallback_used - last_fallback_used),
                (unsigned long long)(fallback_miss - last_fallback_miss));

        last_attach_ok = attach_ok;
        last_not_ready = not_ready;
        last_not_found = not_found;
        last_drop_no_slot = drop_no_slot;

        last_exact_ready_ok = exact_ready_ok;
        last_fallback_used = fallback_used;
        last_fallback_miss = fallback_miss;
    }
}
#endif

    if (src_ready_event) {
        cudaError_t ce = cudaStreamWaitEvent(m_stream, src_ready_event, 0);
        if (ce != cudaSuccess) {
            fprintf(stderr,
                    "[_inference_gpu] cudaStreamWaitEvent failed: %s\n",
                    cudaGetErrorString(ce));
            return {};
        }
    }

    cuda_preprocess_gpu(
        img.ptr<uint8_t>(),
        img.step,
        img.cols,
        img.rows,
        m_device_buffers[0],
        kInputW,
        kInputH,
        m_stream
    );

    infer_gpu(
        *m_context,
        m_stream,
        (void**)m_device_buffers,
        m_output_buffer_host,
        kBatchSize,
        m_decode_ptr_host,
        m_decode_ptr_device,
        m_model_bboxes,
        m_cuda_post_process
    );

    cuda_finalize_detections(
        m_decode_ptr_device,
        kDecodeBBoxElement,
        img.cols,
        img.rows,
        kInputW,
        kInputH,
        m_det_device,          // 你自己的 member buffer，不是 slot
        m_det_count_device,    // 你自己的 member buffer，不是 slot
        kMaxNumOutputBbox,
        m_stream
    );

    cudaError_t ce = cudaMemcpyAsync(
        m_det_count_host,
        m_det_count_device,
        sizeof(int),
        cudaMemcpyDeviceToHost,
        m_stream
    );
    if (ce != cudaSuccess) {
        fprintf(stderr,
                "[_inference_gpu] cudaMemcpyAsync(det_count) failed: %s\n",
                cudaGetErrorString(ce));
        return {};
    }

    ce = cudaMemcpyAsync(
        m_det_host,
        m_det_device,
        kMaxNumOutputBbox * sizeof(Detection),
        cudaMemcpyDeviceToHost,
        m_stream
    );
    if (ce != cudaSuccess) {
        fprintf(stderr,
                "[_inference_gpu] cudaMemcpyAsync(det_host) failed: %s\n",
                cudaGetErrorString(ce));
        return {};
    }

    ce = cudaStreamSynchronize(m_stream);
    if (ce != cudaSuccess) {
        fprintf(stderr,
                "[_inference_gpu] cudaStreamSynchronize failed: %s\n",
                cudaGetErrorString(ce));
        return {};
    }

    const int count = *m_det_count_host;
    if (count <= 0) {
        return res;
    }

    res.assign(m_det_host, m_det_host + count);
    return res;
}
//------------------------------------------------------------------------------
//
//
//
//
//------------------------------------------------------------------------------
int ImgUDPGateway::_send_detboxes(std::vector<Detection> &res)
{
    // 1 frame has pixels: 1296 x 966 = 1251936
    // minimal box: 25 x 25 = 625
    // max boxes in 1 frame: 1251936 / 625 = 2003.x
    // 1 Detection (24 bytes) x MAX_DETBOX (512) = 12288 = 12KB

    // pakcet:
    // [ID:4 BYTES][NUM OF DET:2 BYTES][DETBOXes: 12 KBYTES]

    int ret;
    uint16_t size_header = res.size(), i;
    struct mmsghdr mh = {0};
    struct iovec   iov[2+MAX_DETBOX] = {0};

    // init sendmmsg stuff
    iov[0].iov_base = &m_frameID;
    iov[0].iov_len  = 4;

    iov[1].iov_base = &size_header;
    iov[1].iov_len  = 2;

    // res.resize(MAX_DETBOX);

    i = 2;
    for(auto &det: res)
    {
        iov[i].iov_base = &det;
        iov[i].iov_len  = sizeof(Detection);    // 24 bytes
        i++;
    }

    mh.msg_hdr.msg_name = (caddr_t) &m_detaddr;
    mh.msg_hdr.msg_namelen = sizeof(struct sockaddr_in);
    mh.msg_hdr.msg_iov = iov;
    mh.msg_hdr.msg_iovlen = 2+MAX_DETBOX; // garbage after res.size()

    ret = sendmmsg(m_sock_det, &mh, 1, 0);

    // fmt::print("{}: sendmmsg ret = {}\n", __func__, ret);
    return ret;
}

//------------------------------------------------------------------------------
//
//
//
//
//------------------------------------------------------------------------------
void ImgUDPGateway::_on_detection(int state, void *userdata)
{
    ImgUDPGateway *THIS = (ImgUDPGateway*)userdata;

    THIS->m_nv_det = !THIS->m_nv_det;
}

//------------------------------------------------------------------------------
//
//
//
//
//------------------------------------------------------------------------------
void ImgUDPGateway::_on_tracking(int state, void *userdata)
{
    ImgUDPGateway *THIS = (ImgUDPGateway*)userdata;

    switch (THIS->m_mode)
    {
        case LOCAL:
            THIS->m_nv_trk = !THIS->m_nv_trk;
            break;

        case RELEASE1:
            if(THIS->m_trk_sel >= 0)
            {
                THIS->m_trk_quit = true;
            }
            THIS->mp_netconn->send1208(false);
            break;
    }
}

//------------------------------------------------------------------------------
//
//
//
//
//------------------------------------------------------------------------------
void ImgUDPGateway::_on_mouse(int event, int x, int y, int flags, void *userdata)
{
    ImgUDPGateway *THIS = (ImgUDPGateway*)userdata;

    switch (THIS->m_mode)
    {
        case LOCAL:
            switch(event)
            {
                case cv::EVENT_LBUTTONDOWN:
                #if EN_TRACKING
                if((THIS->m_nv_det) && (THIS->m_nv_trk) && (THIS->m_trk_target_pos.x == -1) && (THIS->m_trk_target_pos.y == -1))
                {
                    THIS->m_trk_target_pos.x = x;
                    THIS->m_trk_target_pos.y = y;
                }
                #endif
                break;

                case cv::EVENT_LBUTTONDBLCLK:
                if(THIS->m_trk_sel >= 0)
                {
                    THIS->m_trk_quit = true;
                }
                break;
            }
            break;

        case RELEASE1:
            switch(event)
            {
                case cv::EVENT_LBUTTONDOWN:
                #if EN_TRACKING
                if((THIS->m_trk_target_pos.x == -1) && (THIS->m_trk_target_pos.y == -1))
                {
                    THIS->m_trk_target_pos.x = x;
                    THIS->m_trk_target_pos.y = y;
                }
                #endif
                break;

                case cv::EVENT_LBUTTONDBLCLK:
                if(THIS->m_trk_sel >= 0)
                {
                    THIS->m_trk_quit = true;
                }
                THIS->mp_netconn->send1208(false);
                break;
            }
            break;
    }
}

//------------------------------------------------------------------------------
//
//
//
//
//------------------------------------------------------------------------------
void ImgUDPGateway::_on_frame_skip(int val, void *userdata)
{
    ImgUDPGateway *THIS = (ImgUDPGateway*)userdata;
    THIS->m_frame_skip = val;
}

//------------------------------------------------------------------------------
// from demo_tracking.cpp
//
//
//
//------------------------------------------------------------------------------
// void ConvertBox(vector<cv::Rect> *outs, vector<vector<tk::dnn::box>> inps)
// void ConvertBox(vector<cv::Rect> *outs, std::vector<Detection> inps)
// {
	// bbox_t box_t;
	// outs->clear();
	// for(int i=0; i<inps[0].size() ;i++)
	// {
	// 	tk::dnn::box b = inps[0][i];
	// 	box_t.x = b.x;
	// 	box_t.y = b.y;
	// 	box_t.w = b.w;
	// 	box_t.h = b.h;
	// 	box_t.obj_id = b.cl;
	// 	outs->push_back(box_t);
	// }
// }
//------------------------------------------------------------------------------
// from demo_tracking.cpp
//
//
//
//------------------------------------------------------------------------------
int ImgUDPGateway::_selectTarget(vector<cv::Rect> objPos, cv::Point mousePos)
{
	int idx = -1;
	vector<vector<cv::Point>> contours;
	for(size_t i = 0; i < objPos.size(); i++)
	{
		vector<cv::Point> obj;
		obj.push_back(cv::Point(objPos[i].x, objPos[i].y));
		obj.push_back(cv::Point(objPos[i].x + objPos[i].width, objPos[i].y));
		obj.push_back(cv::Point(objPos[i].x + objPos[i].width, objPos[i].y + objPos[i].height));
		obj.push_back(cv::Point(objPos[i].x, objPos[i].y + objPos[i].height));
		contours.push_back(obj);
	}
	for(size_t i = 0; i < contours.size(); i++)
	{
		if(pointPolygonTest(contours[i], mousePos, true)>= 0)
		{
			idx = i;
			break;
		}
	}
    #if EN_DBG_TRK
    if(idx!=-1)fmt::print("TRK0: size={}, mouse={},{}, idx={}\n", contours.size(), mousePos.x, mousePos.y, idx);
	#endif
    return idx;
}

//------------------------------------------------------------------------------
// from demo_tracking.cpp
//
//
//
//------------------------------------------------------------------------------
bool ImgUDPGateway::_lossCount(queue<timeval>* queue, timeval last_time, size_t max_num)
{
	bool ret = false;


	return ret;
}

//------------------------------------------------------------------------------
// from demo_tracking.cpp
//
//
//
//------------------------------------------------------------------------------
void ImgUDPGateway::_resetQueue(queue<timeval>* queue)
{
	while(!queue->empty())
	{
		queue->pop();
	}
}

//------------------------------------------------------------------------------
//
//
//
//
//------------------------------------------------------------------------------
void ImgUDPGateway::_mode_local_process(cv::Mat *pCurrentFrame, bool isEO)
{
 
}

//------------------------------------------------------------------------------
//
//
//
//
//------------------------------------------------------------------------------
void ImgUDPGateway::_mode_release1_process(cv::Mat *pCurrentFrame, bool isEO)
{

}

//------------------------------------------------------------------------------
//
//
//
//
//------------------------------------------------------------------------------
