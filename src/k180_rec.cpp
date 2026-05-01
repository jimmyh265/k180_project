
#include "k180_rec.h"
// --- C / POSIX ---
#include <unistd.h>     // usleep(), sleep()
#include <pthread.h>    // pthread_exit()
#include <cstdio>       // sprintf()
#include <cstdlib>      // system()
#include <cstdint>      // int64_t
#include <ctime>        // time_t, time(), localtime()

#include <sys/stat.h>   // struct stat, stat()
#include <dirent.h>     // DIR, opendir(), readdir(), closedir()

// --- C++ std ---
#include <thread>       // std::this_thread::sleep_for
#include <chrono>       // std::chrono::seconds
#include <mutex>        // std::unique_lock (若你用 unique_lock<shared_mutex> 也OK)
#include <shared_mutex> // std::shared_mutex, std::shared_lock

// --- OpenCV ---
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>        // resize(), cvtColor(), COLOR_BGRA2BGR
#include <opencv2/core/cuda.hpp>      // cv::cuda::GpuMat
#include <opencv2/cudaimgproc.hpp>    // (若你未來用 cuda::resize/cvtColor)

#include "gy_logging.h"
#include "user_def_json.h"
#include "k180_runtime.h"
#include "k180_constants.h"

using namespace std;
using namespace cv;
using namespace k180::constants;
using namespace k180::runtime;

cv::VideoWriter cap_w1234, cap_w0, cap_w1, cap_w2, cap_w3;
bool rec_1234_wait = 0, rec_0_wait = 0, rec_1_wait = 0, rec_2_wait = 0, rec_3_wait = 0;

extern std::shared_mutex mat_mutex[7];
extern bool rec_img_ready;
extern bool bingo_c1234_rec;
extern std::atomic<bool> keep_running;
// extern bool keep_running;
extern vector<Mat> cam_BGRA_cMat;
extern vector<cv::cuda::GpuMat> cam_BGR_gMat;
// extern UserConfig cfggg;



namespace k180::rec {

int64_t get_directory_size(const char *dir_path) { 
    struct dirent *entry; 
    struct stat statbuf; 
    int64_t total_size = 0; 
    DIR *dp = opendir(dir_path); 
 
    if (dp == NULL) { 
        log_error_errno_fmt("opendir"); 
        return -1;  // Return an error code 
    } 
 
    // Iterate over the entries in the directory 
    while ((entry = readdir(dp)) != NULL) { 
        // Skip the "." and ".." entries 
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) { 
            continue; 
        } 
 
        // Construct the full path of the entry 
        char full_path[1024]; 
        snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name); 
 
        // Get file status 
        if (stat(full_path, &statbuf) == -1) { 
			log_error_errno_fmt("stat"); 
            continue;  // Skip this entry on error 
        } 
 
        // If it's a directory, recurse into it 
        if (S_ISDIR(statbuf.st_mode)) { 
            int64_t dir_size = get_directory_size(full_path); 
            if (dir_size != -1) { 
                total_size += dir_size;  // Add the size of the subdirectory 
            } 
        } else { 
            // Add the size of the file 
            total_size += statbuf.st_size; 
        } 
    } 
 
    closedir(dp); 
    return total_size; 
}

void rec_wait_clock(){

	int idle = 1*1000000/cfggg.rec_fps_adj;	//必須先乘，再除

	while( !rec_img_ready )
		sleep(1);
		
	while (keep_running.load(std::memory_order_relaxed)) {
		usleep(idle);
		rec_1234_wait = 1;
		rec_0_wait = 1;
		rec_1_wait = 1;
		rec_2_wait = 1;
		rec_3_wait = 1;
	}
	log_info_fmt("rec_wait_clock exit");
	return;
}
	
void rec_1234(){
	cv::Mat tmp_cMAT, local_cmat_1;
	cv::cuda::GpuMat tmp_gMAT;

	while( !rec_img_ready )
		sleep(1);

	while (keep_running.load(std::memory_order_relaxed)) {
		while( !rec_1234_wait )
			usleep(1000);
		rec_1234_wait = 0;

		while( !bingo_c1234_rec )
			usleep(1000);
		bingo_c1234_rec = 0;
		
#if 0
		if(cap_w1234.isOpened()){
			std::shared_lock lock(mat_mutex[cam0123]);
			if( cfggg.s1.resolution == 720 ){
				cuda::resize(cam_BGR_gMat[cam0123], tmp_gMAT, cv::Size(stream_out_w_1234_720,stream_out_h_1234_720), 0, 0, cv::INTER_LINEAR);
				tmp_gMAT.download(tmp_cMAT);
				cap_w1234.write(tmp_cMAT);
			} else {
				cam_BGR_gMat[cam0123].download(tmp_cMAT);
				cap_w1234.write(tmp_cMAT);
			}
			lock.unlock();
		}
#endif
		if(cap_w1234.isOpened()){
			std::shared_lock lock(mat_mutex[cam0123]);
			if( cfggg.s1.resolution == 720 ){
				resize(cam_BGRA_cMat[cam0123], local_cmat_1, cv::Size(stream_out_w_1234_720,stream_out_h_1234_720), 0, 0, cv::INTER_LINEAR);
				cvtColor(local_cmat_1, tmp_cMAT, COLOR_BGRA2BGR);
				cap_w1234.write(tmp_cMAT);
			} else {
				cvtColor(cam_BGRA_cMat[cam0123], tmp_cMAT, COLOR_BGRA2BGR);
				cap_w1234.write(tmp_cMAT);
			}
			lock.unlock();
		}
	}
	log_info_fmt("rec_1234 exit");
	return;
	
}

void rec_0(){
	cv::Mat tmp_cMAT;
	cv::cuda::GpuMat tmp_gMAT;

	while (keep_running.load(std::memory_order_relaxed)) {
		while( !rec_0_wait )
			usleep(1000);
		rec_0_wait = 0;

		std::shared_lock lock(mat_mutex[cam0]);
		if(cap_w0.isOpened()){
			if( cfggg.s1.resolution == 720 ){
				cv::resize(cam_BGR_gMat[cam0], tmp_gMAT, cv::Size(1280,720), 0, 0, cv::INTER_LINEAR);
				tmp_gMAT.download(tmp_cMAT);
				cap_w0.write(tmp_cMAT);
			} else {
				cam_BGR_gMat[cam0].download(tmp_cMAT);
				cap_w0.write(tmp_cMAT);
			}
		}
		lock.unlock();
	}
	log_info_fmt("rec_0 exit");
	return;
}

void rec_1(){
	cv::Mat tmp_cMAT;
	cv::cuda::GpuMat tmp_gMAT;

	while (keep_running.load(std::memory_order_relaxed)) {
		while( !rec_1_wait )
			usleep(1000);
		rec_1_wait = 0;
		
		std::shared_lock lock(mat_mutex[cam1]);
		if(cap_w1.isOpened()){
			if( cfggg.s1.resolution == 720 ){
				cv::resize(cam_BGR_gMat[cam1], tmp_gMAT, cv::Size(1280,720), 0, 0, cv::INTER_LINEAR);
				tmp_gMAT.download(tmp_cMAT);
				cap_w1.write(tmp_cMAT);
			} else {
				cam_BGR_gMat[cam1].download(tmp_cMAT);
				cap_w1.write(tmp_cMAT);
			}
		}
		lock.unlock();
	}
	log_info_fmt("rec_1 exit");
	return;
}

void rec_2(){
	cv::Mat tmp_cMAT;
	cv::cuda::GpuMat tmp_gMAT;

	while (keep_running.load(std::memory_order_relaxed)) {
		while( !rec_2_wait )
			usleep(1000);
		rec_2_wait = 0;
		
		std::shared_lock lock(mat_mutex[cam2]);
		if(cap_w2.isOpened()){
			if( cfggg.s1.resolution == 720 ){
				cv::resize(cam_BGR_gMat[cam2], tmp_gMAT, cv::Size(1280,720), 0, 0, cv::INTER_LINEAR);
				tmp_gMAT.download(tmp_cMAT);
				cap_w2.write(tmp_cMAT);
			} else {
				cam_BGR_gMat[cam2].download(tmp_cMAT);
				cap_w2.write(tmp_cMAT);
			}
		}
		lock.unlock();
	}
	log_info_fmt("rec_2 exit");
	return;
}

void rec_3(){
	cv::Mat tmp_cMAT;
	cv::cuda::GpuMat tmp_gMAT;

	while (keep_running.load(std::memory_order_relaxed)) {
		while( !rec_3_wait )
			usleep(1000);
		rec_3_wait = 0;
		
		std::shared_lock lock(mat_mutex[cam3]);
		if(cap_w3.isOpened()){
			if( cfggg.s1.resolution == 720 ){
				cv::resize(cam_BGR_gMat[cam3], tmp_gMAT, cv::Size(1280,720), 0, 0, cv::INTER_LINEAR);
				tmp_gMAT.download(tmp_cMAT);
				cap_w3.write(tmp_cMAT);
			} else {
				cam_BGR_gMat[cam3].download(tmp_cMAT);
				cap_w3.write(tmp_cMAT);
			}
		}
		lock.unlock();
	}
	log_info_fmt("rec_3 exit");
	return;
}

void rec_center(){

	int ret;
	time_t now;
	struct tm *t;
	char write_cmd_1234[250], write_cmd_0[220], write_cmd_1[220], write_cmd_2[220], write_cmd_3[220];
	int tmp_w = 1920, tmp_h = 1080;
	// int tmp_w_1234 = 5750, tmp_h_1234 = 1088;
	int tmp_w_1234 = stream_out_w_1234_1080, tmp_h_1234 = stream_out_h_1234_1080;		// 6700
	// int tmp_w_1234 = 7680, tmp_h_1234 = 1080;

	if( cfggg.s1.resolution == 720 ){
		tmp_w = 1280;
		tmp_h = 720;
		tmp_w_1234 = 5120;
		tmp_h_1234 = 720;
	}

	while( !rec_img_ready )
		sleep(1);

	while (keep_running.load(std::memory_order_relaxed)) {

		int64_t dir_size = get_directory_size("/data"); 
		if (dir_size != -1) { 
			log_info_fmt("Total size of directory : %ld bytes\n" ,dir_size); 
		} else { 
			log_error_errno_fmt("Error calculating size for directory"); 
		} 
		while ( dir_size > cfggg.rec_dick_size ){
			ret = system("ls /data/* -t | tail -n 4 | xargs -d '\n' rm");
			if( ret == -1 ){
				log_info_fmt("ERROR : del file cmd");
				// cleanup_and_exit(0);
			}
			dir_size = get_directory_size("/data"); 
			if (dir_size != -1) { 
				log_info_fmt("Total size of directory : %ld bytes\n" ,dir_size); 
			} else { 
				log_error_errno_fmt("Error calculating size for directory"); 
			} 
		}

		switch ( cfggg.recorded ){
			case 1:{
				std::unique_lock lock4(mat_mutex[cam0123]);
				if(cap_w1234.isOpened()){
					cap_w1234.release();
				}
				lock4.unlock();
				break;
			}
			case 2:{
				std::unique_lock lock0(mat_mutex[cam0]);
				if(cap_w0.isOpened()){
					cap_w0.release();
				}
				lock0.unlock();

				std::unique_lock lock1(mat_mutex[cam1]);
				if(cap_w1.isOpened()){
					cap_w1.release();
				}
				lock1.unlock();

				std::unique_lock lock2(mat_mutex[cam2]);
				if(cap_w2.isOpened()){
					cap_w2.release();
				}
				lock2.unlock();
				
				std::unique_lock lock3(mat_mutex[cam3]);
				if(cap_w3.isOpened()){
					cap_w3.release();
				}
				lock3.unlock();
				break;
			}
			default:{
				break;
			}
		}
 // maxperf-enable=1 iframeinterval=60 vbv-size=32000
 // maxperf-enable=1 vbv-size=32000
		time(&now);
		t = localtime(&now);
		// sprintf(write_cmd_1234, "appsrc ! videoconvert ! video/x-raw,format=BGRx ! nvvidconv ! nvv4l2h265enc ! h265parse ! mpegtsmux ! filesink location=/data/S1_ch1234_%d%02d%02d_%02d%02d.mp4", t->tm_year+1900, t->tm_mon+1, t->tm_mday, t->tm_hour, t->tm_min);
		sprintf(write_cmd_1234, "appsrc ! videoconvert ! video/x-raw,format=BGRx ! nvvidconv ! nvv4l2h265enc bitrate=%d ! h265parse ! mpegtsmux ! filesink location=/data/S1_ch1234_%d%02d%02d_%02d%02d.mp4", cfggg.s1.datarate_bps*2, t->tm_year+1900, t->tm_mon+1, t->tm_mday, t->tm_hour, t->tm_min);
		sprintf(write_cmd_0, "appsrc ! videoconvert ! video/x-raw,format=BGRx ! nvvidconv ! nvv4l2h265enc bitrate=%d ! h265parse ! mpegtsmux ! filesink location=/data/S1_ch1_%d%02d%02d_%02d%02d.mp4", cfggg.s1.datarate_bps, t->tm_year+1900, t->tm_mon+1, t->tm_mday, t->tm_hour, t->tm_min);
		sprintf(write_cmd_1, "appsrc ! videoconvert ! video/x-raw,format=BGRx ! nvvidconv ! nvv4l2h265enc bitrate=%d ! h265parse ! mpegtsmux ! filesink location=/data/S1_ch2_%d%02d%02d_%02d%02d.mp4", cfggg.s1.datarate_bps, t->tm_year+1900, t->tm_mon+1, t->tm_mday, t->tm_hour, t->tm_min);
		sprintf(write_cmd_2, "appsrc ! videoconvert ! video/x-raw,format=BGRx ! nvvidconv ! nvv4l2h265enc bitrate=%d ! h265parse ! mpegtsmux ! filesink location=/data/S1_ch3_%d%02d%02d_%02d%02d.mp4", cfggg.s1.datarate_bps, t->tm_year+1900, t->tm_mon+1, t->tm_mday, t->tm_hour, t->tm_min);
		sprintf(write_cmd_3, "appsrc ! videoconvert ! video/x-raw,format=BGRx ! nvvidconv ! nvv4l2h265enc bitrate=%d ! h265parse ! mpegtsmux ! filesink location=/data/S1_ch4_%d%02d%02d_%02d%02d.mp4", cfggg.s1.datarate_bps, t->tm_year+1900, t->tm_mon+1, t->tm_mday, t->tm_hour, t->tm_min);
		
		switch ( cfggg.recorded ){
			case 1:
				cap_w1234.open( write_cmd_1234, cv::CAP_GSTREAMER, 0, float(cfggg.rec_fps_adj), cv::Size(tmp_w_1234, tmp_h_1234));
				if ( !cap_w1234.isOpened() ){
					log_info_fmt("cap_w1234 reopen fail \n");
				}
				break;
			case 2:
				cap_w0.open( write_cmd_0, cv::CAP_GSTREAMER, 0, float(cfggg.s1.fps), cv::Size(tmp_w, tmp_h));
				cap_w1.open( write_cmd_1, cv::CAP_GSTREAMER, 0, float(cfggg.s1.fps), cv::Size(tmp_w, tmp_h));
				cap_w2.open( write_cmd_2, cv::CAP_GSTREAMER, 0, float(cfggg.s1.fps), cv::Size(tmp_w, tmp_h));
				cap_w3.open( write_cmd_3, cv::CAP_GSTREAMER, 0, float(cfggg.s1.fps), cv::Size(tmp_w, tmp_h));
				if ( !cap_w0.isOpened() || !cap_w1.isOpened() || !cap_w2.isOpened() || !cap_w3.isOpened() ){
					log_info_fmt("cap_w0 or 2 or 3 or 4 reopen fail \n");
				}
				break;
			default:
				break;
		}
        for (int i = 0; i < 180 && keep_running; i++) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
	}
	log_info_fmt("rec_center exit");
	return;
}

}
