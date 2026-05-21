#===============================================================================
# Description: C++ Project Makefile (fps x short/long profiles)
#===============================================================================

#===============================================================================
# TOOLCHAIN
#===============================================================================
CXX = g++ -std=c++17
CC  = gcc
LD  = $(CXX)
RM  = rm -f
INCLUDES = -I./include \
           -I./ByteTrack/deploy/TensorRT/cpp/include \
           -I/usr/include/eigen3 \
		   -I/opt/nvidia/deepstream/deepstream-7.1/sources/includes

#===============================================================================
# PROJECT VARS
#===============================================================================
BUILD_DIR = ./build
BUILD_COMMON = $(BUILD_DIR)/common
BUILD_FPS60 = $(BUILD_DIR)/fps60
BUILD_FPS30 = $(BUILD_DIR)/fps30
BUILD_FPS60_SHORT = $(BUILD_DIR)/fps60_short
BUILD_FPS60_LONG  = $(BUILD_DIR)/fps60_long
BUILD_FPS30_SHORT = $(BUILD_DIR)/fps30_short
BUILD_FPS30_LONG  = $(BUILD_DIR)/fps30_long
YOLO_DIR = ./yolo_src_new
YOLO_INC = ./yolo_inc_new

SRC_DIR   = ./src
BYTE_DIR  = ./ByteTrack/deploy/TensorRT/cpp/src

#===============================================================================
# CUDA
#===============================================================================
NXX = nvcc
CUDA_DEFINES  = -DAPI_EXPORTS -Dmyplugins_EXPORTS
CUDA_INCLUDES = -I./include -I./$(YOLO_INC) `pkg-config --cflags opencv4`
CUDA_FLAGS    = -g -Xcompiler=-fPIC -std=c++11

# Profile binaries
BIN_FPS60_SHORT = $(BUILD_DIR)/grand_yeah_fps60_short
BIN_FPS60_LONG  = $(BUILD_DIR)/grand_yeah_fps60_long
BIN_FPS30_SHORT = $(BUILD_DIR)/grand_yeah_fps30_short
BIN_FPS30_LONG  = $(BUILD_DIR)/grand_yeah_fps30_long

MYPLUGIN  = $(BUILD_DIR)/libmyplugins.so
INST_DIR  = ../../bin

#===============================================================================
# SOURCES
#===============================================================================
SRCS_CPP  = \
	$(SRC_DIR)/gy_two_cam.cpp \
	$(SRC_DIR)/gy_logging.cpp \
	$(SRC_DIR)/parser_user_json.cpp \
	$(SRC_DIR)/my_seamfinder.cpp \
	$(SRC_DIR)/user_cfg_validate.cpp \
	$(SRC_DIR)/user_cfg_postprocess.cpp \
	$(SRC_DIR)/k180_runtime.cpp \
	$(SRC_DIR)/k180_tracking.cpp \
	$(SRC_DIR)/k180_bright_tuner.cpp \
	$(SRC_DIR)/k180_user_cfg_dump.cpp \
	$(SRC_DIR)/k180_stitch_api.cpp \
	$(SRC_DIR)/k180_pipeline_sync.cpp \
	$(SRC_DIR)/k180_h264_hub.cpp \
	$(SRC_DIR)/k180_h265_hub.cpp \
	$(SRC_DIR)/k180_record_cleanup.cpp \
	$(SRC_DIR)/k180_rtsp_attach.cpp \
	$(SRC_DIR)/k180_stream_builder.cpp \
	$(SRC_DIR)/k180_osd_meta.cpp \
	$(SRC_DIR)/k180_osd_slots.cpp \
	$(SRC_DIR)/k180_frame_tag_meta.cpp \
	$(SRC_DIR)/k180_osd_probe.cpp \
	$(SRC_DIR)/k180_ai_runtime.cpp \
	$(SRC_DIR)/k180_dbg_timing.cpp \
	$(SRC_DIR)/ImgUDPGateway.cpp \
	$(YOLO_DIR)/block.cpp \
	$(YOLO_DIR)/calibrator.cpp \
	$(YOLO_DIR)/model.cpp \
	$(YOLO_DIR)/postprocess.cpp \
	$(YOLO_DIR)/yolo_main.cpp \
	$(BYTE_DIR)/BYTETracker.cpp \
	$(BYTE_DIR)/STrack.cpp \
	$(BYTE_DIR)/kalmanFilter.cpp \
	$(BYTE_DIR)/lapjv.cpp \
	$(BYTE_DIR)/utils.cpp

# These sources include k180_constants.h, so they must be compiled once per fps
# profile. They do not depend on HW_SHORT_VER.
SRCS_CPP_FPS = \
	$(SRC_DIR)/user_cfg_validate.cpp \
	$(SRC_DIR)/k180_runtime.cpp \
	$(SRC_DIR)/k180_bright_tuner.cpp \
	$(SRC_DIR)/k180_stitch_api.cpp \
	$(SRC_DIR)/k180_stream_builder.cpp \
	$(SRC_DIR)/k180_ai_runtime.cpp

# gy_two_cam.cpp depends on both the fps profile and HW_SHORT_VER.
SRCS_CPP_VARIANT = \
	$(SRC_DIR)/gy_two_cam.cpp

SRCS_CPP_COMMON = $(filter-out $(SRCS_CPP_FPS) $(SRCS_CPP_VARIANT),$(SRCS_CPP))

SRCS__CU  = \
	src/noblender_kernel_stream.cu \
	src/remap_rgba_kernel.cu \
	yolo_plugin/yololayer.cu \
	$(YOLO_DIR)/preprocess.cu \
	$(YOLO_DIR)/postprocess.cu

#===============================================================================
# FLAGS
#===============================================================================
INCS = -I./ -I./$(YOLO_INC) -I./yolo_plugin \
	-I/usr/local/cuda/targets/aarch64-linux/include \
	-I/usr/src/jetson_multimedia_api/include \
	`pkg-config --cflags glib-2.0 gstreamer-1.0 opencv4 libsoup-2.4 json-glib-1.0` \
	-I/usr/include/glib-2.0

OPT       = -O2
CFLAGS    = -Wall $(INCS) $(OPT) $(INCLUDES)
CFLAGS += -Wno-class-memaccess
CFLAGS += -g -fno-omit-frame-pointer
CFLAGS += -DNVMM_COPY_USE_EVENT_FENCE=1
# CFLAGS += -g3 -O0 -fno-omit-frame-pointer -D_GLIBCXX_ASSERTIONS
# NVMM_COPY_USE_EVENT_FENCE 0/1
CFLAGS_FPS60 = -DK180_PROFILE_NAME=\"fps60\" -DK180_TRIGGER_INTERVAL_US=16667 -DK180_MAX_STREAM_FPS=60
CFLAGS_FPS30 = -DK180_PROFILE_NAME=\"fps30\" -DK180_TRIGGER_INTERVAL_US=33334 -DK180_MAX_STREAM_FPS=30
CFLAGS_SHORT = -DHW_SHORT_VER
CFLAGS_LONG  =
CFLAGS_FPS60_SHORT = $(CFLAGS_FPS60) $(CFLAGS_SHORT)
CFLAGS_FPS60_LONG  = $(CFLAGS_FPS60) $(CFLAGS_LONG)
CFLAGS_FPS30_SHORT = $(CFLAGS_FPS30) $(CFLAGS_SHORT)
CFLAGS_FPS30_LONG  = $(CFLAGS_FPS30) $(CFLAGS_LONG)
# CFLAGS += -DGST_DBG_MSG=1
#===============================================================================
# LINK
#===============================================================================
LDFLAGS   = -L/usr/local/cuda/targets/aarch64-linux/lib \
            -L/usr/local/cuda/targets/aarch64-linux/lib/stubs \
            -L/usr/lib/aarch64-linux-gnu/nvidia \
            -L/opt/nvidia/deepstream/deepstream-7.1/lib
LDFLAGS  += -rdynamic
LDFLAGS  += -Wl,-rpath,/usr/local/cuda/targets/aarch64-linux/lib \
            -Wl,-rpath,/usr/lib/aarch64-linux-gnu/nvidia \
            -Wl,-rpath,/opt/nvidia/deepstream/deepstream-7.1/lib

LDLIBS    = -lpthread -lpcap -lfmt \
            -luuid -lmicrohttpd -ljansson \
            -lnvinfer -lcudart -lEGL -lnvbufsurface -lcuda \
            -lnvds_meta -lnvdsgst_meta -lnvdsbufferpool \
            $(MYPLUGIN) \
            `pkg-config --libs gstreamer-rtsp-server-1.0 gstreamer-app-1.0 gstreamer-allocators-1.0 gstreamer-video-1.0 opencv4 libsoup-2.4 json-glib-1.0` \
            -lcudadevrt -lcudart_static -lrt -ljson-c -ldl \
            -lopencv_stitching -lopencv_cudaimgproc -lopencv_cudaarithm \
            -lopencv_core -lopencv_imgproc -lopencv_highgui
#===============================================================================
# OBJECTS
#===============================================================================
OBJS_CU_COMMON        = $(SRCS__CU:%.cu=$(BUILD_DIR)/%.cu.o)

OBJS_CPP_COMMON      = $(SRCS_CPP_COMMON:%.cpp=$(BUILD_COMMON)/%.o)
OBJS_CPP_FPS60       = $(SRCS_CPP_FPS:%.cpp=$(BUILD_FPS60)/%.o)
OBJS_CPP_FPS30       = $(SRCS_CPP_FPS:%.cpp=$(BUILD_FPS30)/%.o)
OBJS_CPP_FPS60_SHORT = $(SRCS_CPP_VARIANT:%.cpp=$(BUILD_FPS60_SHORT)/%.o)
OBJS_CPP_FPS60_LONG  = $(SRCS_CPP_VARIANT:%.cpp=$(BUILD_FPS60_LONG)/%.o)
OBJS_CPP_FPS30_SHORT = $(SRCS_CPP_VARIANT:%.cpp=$(BUILD_FPS30_SHORT)/%.o)
OBJS_CPP_FPS30_LONG  = $(SRCS_CPP_VARIANT:%.cpp=$(BUILD_FPS30_LONG)/%.o)

#===============================================================================
# PHONY
#===============================================================================
.PHONY: all all-profiles fps60 fps30 short long \
        fps60-short fps60-long fps30-short fps30-long clean distclean

#===============================================================================
# ALL
#===============================================================================
all: all-profiles

all-profiles: fps60 fps30

fps60: fps60-short fps60-long

fps30: fps30-short fps30-long

short: fps60-short fps30-short

long: fps60-long fps30-long

fps60-short: $(BIN_FPS60_SHORT)

fps60-long: $(BIN_FPS60_LONG)

fps30-short: $(BIN_FPS30_SHORT)

fps30-long: $(BIN_FPS30_LONG)
#===============================================================================
# MYPLUGIN
#===============================================================================
$(MYPLUGIN): $(OBJS_CU_COMMON)
	@$(CXX) -fPIC -shared -Wl,-soname,libmyplugins.so -o $@ $^ \
	-lnvinfer -lcudart -L"/usr/local/cuda/targets/aarch64-linux/lib/stubs" \
	-L"/usr/local/cuda/targets/aarch64-linux/lib" -lcudadevrt -lcudart_static -lrt -lpthread -ldl \
	-L/usr/local/lib -lopencv_core -lopencv_imgproc -lopencv_highgui

#===============================================================================
# LINK BINARIES
#===============================================================================
$(BIN_FPS60_SHORT): $(OBJS_CPP_COMMON) $(OBJS_CPP_FPS60) $(OBJS_CPP_FPS60_SHORT) $(OBJS_CU_COMMON) | $(MYPLUGIN)
	@$(LD) -o $@ $^ $(LDFLAGS) $(LDLIBS)

$(BIN_FPS60_LONG): $(OBJS_CPP_COMMON) $(OBJS_CPP_FPS60) $(OBJS_CPP_FPS60_LONG) $(OBJS_CU_COMMON) | $(MYPLUGIN)
	@$(LD) -o $@ $^ $(LDFLAGS) $(LDLIBS)

$(BIN_FPS30_SHORT): $(OBJS_CPP_COMMON) $(OBJS_CPP_FPS30) $(OBJS_CPP_FPS30_SHORT) $(OBJS_CU_COMMON) | $(MYPLUGIN)
	@$(LD) -o $@ $^ $(LDFLAGS) $(LDLIBS)

$(BIN_FPS30_LONG): $(OBJS_CPP_COMMON) $(OBJS_CPP_FPS30) $(OBJS_CPP_FPS30_LONG) $(OBJS_CU_COMMON) | $(MYPLUGIN)
	@$(LD) -o $@ $^ $(LDFLAGS) $(LDLIBS)

#===============================================================================
# COMPILE RULES
#===============================================================================
$(BUILD_COMMON)/%.o: %.cpp
	@mkdir -p $(dir $@)
	@$(CXX) $(CFLAGS) -MMD -c -o $@ $<

$(BUILD_FPS60)/%.o: %.cpp
	@mkdir -p $(dir $@)
	@$(CXX) $(CFLAGS) $(CFLAGS_FPS60) -MMD -c -o $@ $<

$(BUILD_FPS30)/%.o: %.cpp
	@mkdir -p $(dir $@)
	@$(CXX) $(CFLAGS) $(CFLAGS_FPS30) -MMD -c -o $@ $<

$(BUILD_FPS60_SHORT)/%.o: %.cpp
	@mkdir -p $(dir $@)
	@$(CXX) $(CFLAGS) $(CFLAGS_FPS60_SHORT) -MMD -c -o $@ $<

$(BUILD_FPS60_LONG)/%.o: %.cpp
	@mkdir -p $(dir $@)
	@$(CXX) $(CFLAGS) $(CFLAGS_FPS60_LONG) -MMD -c -o $@ $<

$(BUILD_FPS30_SHORT)/%.o: %.cpp
	@mkdir -p $(dir $@)
	@$(CXX) $(CFLAGS) $(CFLAGS_FPS30_SHORT) -MMD -c -o $@ $<

$(BUILD_FPS30_LONG)/%.o: %.cpp
	@mkdir -p $(dir $@)
	@$(CXX) $(CFLAGS) $(CFLAGS_FPS30_LONG) -MMD -c -o $@ $<

$(BUILD_DIR)/%.cu.o: %.cu
	@mkdir -p $(dir $@)
	@$(NXX) $(CUDA_DEFINES) $(CUDA_INCLUDES) $(CUDA_FLAGS) -MMD -c -o $@ $<

#===============================================================================
# CLEAN
#===============================================================================
clean:
	@$(RM) -rf $(BUILD_DIR)

distclean: clean

#===============================================================================
# AUTO DEPENDENCIES (.d)
#===============================================================================
-include $(OBJS_CPP_COMMON:.o=.d) $(OBJS_CPP_FPS60:.o=.d) $(OBJS_CPP_FPS30:.o=.d) \
         $(OBJS_CPP_FPS60_SHORT:.o=.d) $(OBJS_CPP_FPS60_LONG:.o=.d) \
         $(OBJS_CPP_FPS30_SHORT:.o=.d) $(OBJS_CPP_FPS30_LONG:.o=.d) \
         $(OBJS_CU_COMMON:.o=.d)
