#ifndef __UTILS_HPP_INCLUDE__
#define __UTILS_HPP_INCLUDE__

#include <iostream>
#include <string>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <pthread.h>
#include <filesystem>
#include <algorithm>
#include <fmt/format.h>     // libfmt-dev
#include <png.h>            // libpng-dev

using namespace std::chrono;

class Utils
{
public:
    static int init_socket(std::string sockname, std::string bind_iface, int bind_port)
    {
        int ret, sock, yes=1;
        struct ifreq ifr;
        struct sockaddr_in broadcastAddr = {0};

        sock = socket(AF_INET, SOCK_DGRAM, 0);
        
        if (sock < 0)
        {
            fmt::print("[{}] socket error\n", sockname);
            return -1;
        }

        ret = setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof(yes));
        if (ret == -1) 
        {
            fmt::print("[{}] setsockopt SO_REUSEPORT error\n", sockname);
            return -1;
        }

        ret = setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
        if (ret == -1) 
        {
            fmt::print("[{}] setsockopt SO_REUSEADDR error\n", sockname);
            return -1;
        }

        ret = setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &yes, sizeof(yes));
        if (ret == -1) 
        {
            fmt::print("[{}] setsockopt SO_BROADCAST error\n", sockname);
            return -1;
        }

        if(!bind_iface.empty())
        {
            memset(&ifr, 0, sizeof(ifr));
            snprintf(ifr.ifr_name, sizeof(ifr.ifr_name), "%s", bind_iface.c_str());
            if (setsockopt(sock, SOL_SOCKET, SO_BINDTODEVICE, (void *)&ifr, sizeof(ifr)) < 0)
            {
                fmt::print("[{}] BINDTODEVICE error\n", sockname);
                return -1;
            }
        }

        if(bind_port > 0)
        {
            broadcastAddr.sin_family = AF_INET;
            // broadcastAddr.sin_addr.s_addr = htonl(INADDR_ANY);  /* Any incoming interface */
            broadcastAddr.sin_port = htons(bind_port);
            
            ret = bind(sock, (struct sockaddr *) &broadcastAddr, sizeof(broadcastAddr));
            if (ret == -1) 
            {
                perror("[m_sock_cmd] bind error");
                fmt::print("[{}] bind() error\n", sockname);
                return -1;
            }
        }
        
        return sock;
    }

    // copy from B_Gateway/yoloc_in/config.h
    static void draw_single_img_bbox(cv::Mat& img, std::vector<DetectionR>& res, std::vector<cv::Rect> *pRect)
    {
        if(pRect) pRect->clear();
        
        for (size_t j = 0; j < res.size(); j++)
        {
            std::string str_num = std::to_string(res[j].conf);
            cv::Rect r = Utils::get_rect(img, res[j].bbox);
            cv::rectangle(img, r, cv::Scalar(0x27, 0xC1, 0x36), 2);
            cv::putText(img, std::to_string((int)res[j].class_id)+":"+str_num.substr(0, str_num.find('.') + 3), cv::Point(r.x, r.y - 1), cv::FONT_HERSHEY_PLAIN, 1.2, cv::Scalar(0xFF, 0xFF, 0xFF), 2);

            // save rect 
            if(pRect)
            {
                pRect->push_back(r);
            }
        }
    }

    // copy from B_Gateway/yoloc_in/config.h
    static cv::Rect get_rect(cv::Mat& img, float bbox[4])
    {
        const static int kInputH = 640; // B_Gateway/yoloc_in/config.h
        const static int kInputW = 640; // B_Gateway/yoloc_in/config.h

        float l, r, t, b;
        float r_w = kInputW / (img.cols * 1.0);
        float r_h = kInputH / (img.rows * 1.0);
        if (r_h > r_w) {
            l = bbox[0] - bbox[2] / 2.f;
            r = bbox[0] + bbox[2] / 2.f;
            t = bbox[1] - bbox[3] / 2.f - (kInputH - r_w * img.rows) / 2;
            b = bbox[1] + bbox[3] / 2.f - (kInputH - r_w * img.rows) / 2;
            l = l / r_w;
            r = r / r_w;
            t = t / r_w;
            b = b / r_w;
        } else {
            l = bbox[0] - bbox[2] / 2.f - (kInputW - r_h * img.cols) / 2;
            r = bbox[0] + bbox[2] / 2.f - (kInputW - r_h * img.cols) / 2;
            t = bbox[1] - bbox[3] / 2.f;
            b = bbox[1] + bbox[3] / 2.f;
            l = l / r_h;
            r = r / r_h;
            t = t / r_h;
            b = b / r_h;
        }
        return cv::Rect(round(l), round(t), round(r - l), round(b - t));
    }

    static int set_priority(pthread_t tid, int policy, int priority)
    {
        sched_param sch_param;
        int ret = 0, dummy;

        pthread_getschedparam(tid, &dummy, &sch_param);
        
        sch_param.sched_priority = priority;
        ret = pthread_setschedparam(tid, policy, &sch_param);

        return ret;
    }

    static int writePNG(const char* filename, int width, int height, uint8_t *buffer, char* title = NULL)
    {
        // http://www.labbookpages.co.uk/software/imgProc/files/libPNG/makePNG.c
        int code = 0;
        FILE *fp = NULL;
        png_structp png_ptr = NULL;
        png_infop info_ptr = NULL;
        png_bytep row = NULL;

        // Open file for writing (binary mode)
        fp = fopen(filename, "wb");
        if (fp == NULL) {
            fprintf(stderr, "Could not open file %s for writing\n", filename);
            code = 1;
            goto finalise;
        }

        // Initialize write structure
        png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
        if (png_ptr == NULL) {
            fprintf(stderr, "Could not allocate write struct\n");
            code = 1;
            goto finalise;
        }
        png_set_compression_level(png_ptr, 0); // 0: no compression, 9: max
        // Initialize info structure
        info_ptr = png_create_info_struct(png_ptr);
        if (info_ptr == NULL) {
            fprintf(stderr, "Could not allocate info struct\n");
            code = 1;
            goto finalise;
        }

        // Setup Exception handling
        if (setjmp(png_jmpbuf(png_ptr))) {
            fprintf(stderr, "Error during png creation\n");
            code = 1;
            goto finalise;
        }

        png_init_io(png_ptr, fp);

        // Write header (8 bit colour depth)
        png_set_IHDR(png_ptr, info_ptr, width, height,
                8, PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE,
                PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);

        // Set title
        if (title != NULL) {
            png_text title_text;
            title_text.compression = PNG_TEXT_COMPRESSION_NONE;
            title_text.key = (char*)"Title";
            title_text.text = title;
            png_set_text(png_ptr, info_ptr, &title_text, 1);
        }

        png_write_info(png_ptr, info_ptr);

        // Allocate memory for one row (3 bytes per pixel - RGB)
        row = (png_bytep) malloc(3 * width * sizeof(png_byte));

        // Write image data
        int x, y;
        for (y=0 ; y<height ; y++) {
    #if 1   // BGR
            for (x=0 ; x<width ; x++) {
                // setRGB(&(row[x*3]), buffer[y*width + x]);
                uint8_t r = buffer[y*(width*3) + (x * 3) + 2];
                uint8_t g = buffer[y*(width*3) + (x * 3) + 1];
                uint8_t b = buffer[y*(width*3) + (x * 3) + 0];

                row[x*3 + 0] = r;
                row[x*3 + 1] = g;
                row[x*3 + 2] = b;
            }
            png_write_row(png_ptr, row);
    #else   // RGB
            memcpy(row, &buffer[y*(width*3)], 1296 * 3);
            png_write_row(png_ptr, row);
    #endif
        }

        // End write
        png_write_end(png_ptr, NULL);

        finalise:
        if (fp != NULL) fclose(fp);
        if (info_ptr != NULL) png_free_data(png_ptr, info_ptr, PNG_FREE_ALL, -1);
        if (png_ptr != NULL) png_destroy_write_struct(&png_ptr, (png_infopp)NULL);
        if (row != NULL) free(row);

        return code;
    }

    static std::string createPicName(std::string extension, std::string folder="./")
    {
        char ret_string[64] = {0};

        int64_t ms = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
        time_t now = ms / 1000;
        tm *ltm = localtime(&now);
        #if __GNUC__ >= 10
        snprintf(ret_string, sizeof(ret_string)-1 , "%s/%4d%02d%02d_%02d%02d%02d_%03llu.%s",
            folder.c_str(),
            1900 + ltm->tm_year, 1 + ltm->tm_mon, ltm->tm_mday, ltm->tm_hour, ltm->tm_min, ltm->tm_sec, ms % 1000,
            extension.c_str());
        #else
        snprintf(ret_string, sizeof(ret_string)-1 , "%s/%4d%02d%02d_%02d%02d%02d_%03lu.%s",
            folder.c_str(),
            1900 + ltm->tm_year, 1 + ltm->tm_mon, ltm->tm_mday, ltm->tm_hour, ltm->tm_min, ltm->tm_sec, ms % 1000,
            extension.c_str());
        #endif
        return std::string(ret_string);
    }

    static std::vector<std::string> getPicnames(std::string folder)
    {
        std::vector<std::string> ret;

        for (auto const& dir_entry: std::filesystem::directory_iterator(folder))
        {
            ret.push_back(dir_entry.path());
        }

        std::sort(ret.begin(), ret.end());

        return ret;
    }

    static bool isValidIpAddress(const std::string &ipAddress)
    {
        struct sockaddr_in sa;
        int result = inet_pton(AF_INET, ipAddress.c_str(), &(sa.sin_addr));
        return result != 0;
    }
};

#endif  // __UTILS_HPP_INCLUDE__