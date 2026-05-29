#pragma once

// #include <opencv2/videoio/videoio_c.h>	// CV_CAP_PROP_EXPOSURE
#include <opencv2/opencv.hpp>

extern cv::VideoWriter cap_w1234, cap_w0, cap_w1, cap_w2, cap_w3;
extern bool rec_1234_wait, rec_0_wait, rec_1_wait, rec_2_wait, rec_3_wait;

namespace k180::rec {
    void rec_wait_clock();
    void rec_1234();
    void rec_0();
    void rec_1();
    void rec_2();
    void rec_3();
    void rec_center();
}