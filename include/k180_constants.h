#pragma once
#include <cstdint>
#include <string_view>

namespace k180::constants {
	
inline constexpr char META_PATH_0[] = "/home/fourd/projects/rtsp_server/cfg/meta1234_rotate_0";
inline constexpr char META_PATH_180[] = "/home/fourd/projects/rtsp_server/cfg/meta1234_rotate_180";
inline constexpr char FW_INFO_FILE[] = "/home/fourd/projects/rtsp_server/cfg/firmware_ver_info.json";
inline constexpr char RF_REG_FILE[] = "/home/fourd/projects/rtsp_server/cfg/user_def_setting.json";

inline constexpr int WORKAROUNF_FPS = 30;
inline constexpr int CAM_NUMBER = 4;
inline constexpr int CAPTURE_IMG_WIDTH = 1920;
inline constexpr int CAPTURE_IMG_HEIGHT = 1080;

inline constexpr int stream_out_w_1234_1080 = 5800, stream_out_h_1234_1080 = 1000;
inline constexpr int stream_out_w_1234_720 = 4176, stream_out_h_1234_720 = 720; 	// same scale as 1080p
inline constexpr int stream_out_w_one_1080 = 1920, stream_out_h_one_1080 = 1080;
inline constexpr int stream_out_w_one_720 = 1280, stream_out_h_one_720 = 720; 

inline constexpr int stream_out_w_half_1080 = stream_out_w_1234_1080/2;
inline constexpr int stream_out_w_half_720 = stream_out_w_1234_720/2;

inline constexpr uint8_t shutter_interval = 17;	//33;	// 迴圈裡的動作 ex set_control 需要 2.xms
/*
shutter_interval = 17; 需要搭配 inline int exposure_tun_val = 8000;，輸出 fps 可達 58, 59, 若高於 8000, 則FPS不達58 59
20 來不及 blender
25 可以 blender
*/

enum CamId {
    cam0 = 0,
    cam1,
    cam2,
    cam3,
    cam0123
};

inline constexpr int CTRL_WDR_MODE			= 0x00981991;
inline constexpr int CTRL_TRIGGER_MODE		= 0x009819c0;
inline constexpr int CTRL_TRIGGER_SHUTTER   = 0x009819c1;
inline constexpr int CTRL_TRIGGER_GAIN  	 = 0x009819c2;
inline constexpr int CTRL_TRIGGER_WB_MODE  	 = 0x009819c3;
inline constexpr int CTRL_TRIGGER_WB_RGAIN   = 0x009819c4;
inline constexpr int CTRL_TRIGGER_WB_BGAIN   = 0x009819c5;
inline constexpr int CTRL_TRIGGER_DELAY      = 0x009819c9;
inline constexpr int CTRL_SW_TRIGGER		= 0x009819cb;

inline constexpr int BLENDER_POOL_SIZE = 3;
inline constexpr int THREAD_APPLY_COUNT = 1;
inline constexpr int THREAD_PREP_COUNT = 1;
// inline constexpr float BLEND_STRENGTH_DEFAULT = 0.04f;

// inline constexpr std::int64_t REC_DISK_SIZE_DEFAULT = 50'000'000'000LL;

// inline constexpr std::string_view DEFAULT_DEV_NAME = "fd-k180-01";
// inline constexpr std::string_view DEFAULT_CFG_PATH = "./user_def_setting.json";


#if 0
/* Original image after undistorted then cut black area. */
// int AFTER_UNDIST_AND_CUT_W = 1860;		// 1920 - 30 - 30 = 1860
int AFTER_UNDIST_AND_CUT_W = 1520;		// 1920 - 200 - 200 = 1520
int AFTER_UNDIST_AND_CUT_H = 980;		// 1080 - 50 - 50 = 980
int UNDIST_CUT_W = 200, UNDIST_CUT_H = 50;

size_t AFTER_UNDIST_FOUR_IMG_SIZE_720P_4  = 14745600;		// 1280 * 720 * 4 * 4 =  RGBA * 四張
size_t AFTER_UNDIST_FOUR_IMG_SIZE_720P_2  = 7372800;		// 1280 * 720 * 4 * 2 =  RGBA * 二張
// int AFTER_UNDIST_FOUR_IMG_SIZE_1080P  = 29164800;		// 1860 * 980 * 4 * 4 =  RGBA * 四張
int AFTER_UNDIST_FOUR_IMG_SIZE_1080P  = 23833600;		// 1520 * 980 * 4 * 4 =  RGBA * 四張
int AFTER_UNDIST_FOUR_IMG_SIZE_SPEC1 = 27030240;			// 6080 + 177 = 6257 , 6257 * 1080 * 4 = 27030240

size_t stream_out_4ch_size = (size_t )stream_out_w_1234_1080 * (size_t )stream_out_h_1234_1080 * 4;
// int64_t REC_DICK_SIZE = 50000000000ULL;		// 50G
#endif
	
} // namespace k180