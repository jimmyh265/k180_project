#ifndef __INC_IMGUDPGATEWAY_HPP__
#define __INC_IMGUDPGATEWAY_HPP__

// STD
#include <cstdint>
#include <cstring>
#include <cstdarg>
#include <string>
#include <sys/socket.h>
// 3RD-PARTY
#include <png.h>
#include <cuda_runtime.h>
#include <opencv2/opencv.hpp>
#include <opencv2/tracking.hpp>
#include <opencv2/tracking/tracking_legacy.hpp>
// LOCAL
// #include "FrameSource.hpp"
#include "../common/common.hpp"
#include "../common/nv_cmd.hpp"
#include "../common/detbox.hpp"
#include "../common/blockingconcurrentqueue.h"
#include "netcmd.hpp"
#include "cuda_utils.h"
#include "logging.h"
#include "config.h"
#include "k180_osd_shared.h"
// #include <opencv2/core/cuda.hpp>
using namespace std;
using namespace nvinfer1;

enum Mode   { NO_SAVE = 0, DO_SAVE = 1, FORWARD = 2, LOCAL = 3, RELEASE1 = 4, };
enum TrackerType {
    BOOSTING = 0,   MIL = 1,    KCF = 2,   TLD = 3,
    MEDIANFLOW = 4, GOTURN = 5, MOSSE = 6, CSRT = 7,
};

class Detection;

class ImgUDPGateway
{
    static const int MAX_TRK_AREA = (200*200);  // pixel x pixel
    static const size_t MISS_TARGET_FRAME_NUM = 5;
    static const int IFR_EO_SKIP = 5;
    static const int IFR_IR_SKIP = 12;
    static const int TRK_EO_SKIP = 5;
    static const int TRK_IR_SKIP = 12;
    static const string WINDOW_NAME;
    static const string MODE[5];
    static const std::vector<int> PNG_COMPRESSION_PARAMS;
    static const std::vector<int> JPG_COMPRESSION_PARAMS;
    static void _on_detection(int state, void *userdata);
    static void _on_tracking(int state, void *userdata);
    static void _on_mouse(int event, int x, int y, int flags, void *userdata);
    static void _on_frame_skip(int val, void *userdata);
public:
    ImgUDPGateway(string iface_in, int port, Mode mode, string iface_out, string engine="", int tracker_type=-1);
    ~ImgUDPGateway();
    void start();
	std::vector<Detection> _inference(cv::Mat pic);
	// std::vector<Detection> _inference_gpu(cv::cuda::GpuMat pic);
	std::vector<Detection> _inference_gpu(const cv::cuda::GpuMat& pic, cudaEvent_t src_ready_event);
bool _inference_gpu_to_slot(const cv::cuda::GpuMat& img,
                            std::uint64_t frame_seq,
                            k180::osd::OsdShared& osd_shared,
                            cudaEvent_t src_ready_event);

bool publish_track_results_to_slot(
    const std::vector<Detection>& res_track,
    std::uint64_t frame_seq,
    k180::osd::OsdShared& osd_shared);
	
bool publish_detections_to_slot(const std::vector<Detection>& res,
                                                std::uint64_t frame_seq,
                                                k180::osd::OsdShared& osd_shared);
    static const string TRACKER_TYPE[8];

private:
    void _init_yolov7();
    void _init_tracker();
    void _deinit_yolov7();
    void _forward();
    void _nvcmd_receiver();
    // std::vector<Detection> _inference(cv::Mat pic);
    int _send_detboxes(std::vector<Detection> &res);
    int _selectTarget(vector<cv::Rect> objPos, cv::Point mousePos);
    bool _lossCount(queue<timeval>* queue, timeval last_time, size_t max_num);
    void _resetQueue(queue<timeval>* queue);
    void _mode_local_process(cv::Mat *pCurrentFrame, bool isEO);
    void _mode_release1_process(cv::Mat *pCurrentFrame, bool isEO);

    string m_iface_in;
    int m_port;
    int m_mode;
    string m_iface_out; // it's IP addr when m_mode == RELEASE1
    Netcmd *mp_netconn;
    // FrameSource *m_frame_src;

    cv::VideoCapture m_cap;

    cv::Mat *m_pCurFrame;

    int m_sock_img; // broadcast img to client
    int m_sock_cmd; // recieve cmd from client
    int m_sock_det; // broadcast detbox to client
    struct sockaddr_in m_dstaddr;   // img dstaddr
    struct sockaddr_in m_detaddr;   // detbox addr

    moodycamel::BlockingConcurrentQueue<Packet> m_outq;
    uint32_t m_frameID = 0;

    bool m_nv_det = false;       // call_detection
    bool m_nv_det_prev = false;
    bool m_nv_trk = false;
    bool m_trk_quit = false;
    int m_trk_sel = -1;
    bool m_trk_got21AA;
    cv::Point m_trk_target_pos = cv::Point(-1, -1);
    bool m_run = true;
    int m_frame_skip = 1;
    string m_engine_fname;
    int m_tracker_type;
    string m_save_folder = "./";
    cv::Ptr<cv::legacy::Tracker> m_tracker = nullptr;
    cv::Rect m_trk_bbox;

    // yolo / cuda stuff
    // nvinfer1::IRuntime* m_runtime = nullptr;
    // nvinfer1::ICudaEngine* m_engine = nullptr;
    // nvinfer1::IExecutionContext* m_context = nullptr;
    IRuntime* m_runtime = nullptr;
    ICudaEngine* m_engine = nullptr;
    IExecutionContext* m_context = nullptr;
    cudaStream_t m_stream;
    float* m_device_buffers[2];
    float* m_output_buffer_host = nullptr;
    float* m_decode_ptr_host = nullptr;
    float* m_decode_ptr_device = nullptr;
	// auto m_out_dims = nullptr;
	int m_model_bboxes;
	std::string m_cuda_post_process = "g";
	
Detection* m_final_det_device = nullptr;
int* m_final_det_count_device = nullptr;

Detection* m_final_det_host = nullptr;
int m_final_det_count_host = 0;

Detection* m_det_device = nullptr;
int*       m_det_count_device = nullptr;

Detection* m_det_host = nullptr;
int*       m_det_count_host = nullptr;
};

#endif
