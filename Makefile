#===============================================================================
# Description: C++ Project Makefile (dual logging: syslog & printf)
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
BUILD_SHORT = $(BUILD_DIR)/short
BUILD_LONG  = $(BUILD_DIR)/long
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

# 原本的 binary（完全保留）
BIN_SERVICE = $(BUILD_DIR)/grand_yeah_service
BIN_CONSOLE = $(BUILD_DIR)/grand_yeah_console

# 新增 short / long binaries
BIN_SERVICE_SHORT  = $(BUILD_DIR)/grand_yeah_service_short
BIN_CONSOLE_SHORT  = $(BUILD_DIR)/grand_yeah_console_short
BIN_SERVICE_LONG   = $(BUILD_DIR)/grand_yeah_service_long
BIN_CONSOLE_LONG   = $(BUILD_DIR)/grand_yeah_console_long

MYPLUGIN  = $(BUILD_DIR)/libmyplugins.so
INST_DIR  = ../../bin

#===============================================================================
# SOURCES
#===============================================================================
SRCS_CPP  = \
	$(SRC_DIR)/gy_two_cam.cpp \
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

LOG_CPP = $(SRC_DIR)/gy_logging.cpp

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
CFLAGS_SHORT = -DHW_SHORT_VER
CFLAGS_LONG  =
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
OBJS_CPP_COMMON       = $(SRCS_CPP:%.cpp=$(BUILD_DIR)/%.o)
OBJS_CU_COMMON        = $(SRCS__CU:%.cu=$(BUILD_DIR)/%.cu.o)

OBJS_CPP_SHORT        = $(SRCS_CPP:%.cpp=$(BUILD_SHORT)/%.o)
OBJS_CU_SHORT         = $(SRCS__CU:%.cu=$(BUILD_SHORT)/%.cu.o)

OBJS_CPP_LONG         = $(SRCS_CPP:%.cpp=$(BUILD_LONG)/%.o)
OBJS_CU_LONG          = $(SRCS__CU:%.cu=$(BUILD_LONG)/%.cu.o)

OBJS_LOG_SERVICE      = $(BUILD_DIR)/gy_logging_syslog.o
OBJS_LOG_CONSOLE      = $(BUILD_DIR)/gy_logging_console.o

OBJS_LOG_SERVICE_SHORT  = $(BUILD_SHORT)/gy_logging_syslog.o
OBJS_LOG_CONSOLE_SHORT  = $(BUILD_SHORT)/gy_logging_console.o
OBJS_LOG_SERVICE_LONG   = $(BUILD_LONG)/gy_logging_syslog.o
OBJS_LOG_CONSOLE_LONG   = $(BUILD_LONG)/gy_logging_console.o

#===============================================================================
# PHONY
#===============================================================================
.PHONY: all clean distclean

#===============================================================================
# ALL
#===============================================================================
all: \
	$(MYPLUGIN) \
	$(BIN_CONSOLE_SHORT) \
	$(BIN_SERVICE_SHORT) \
	$(BIN_SERVICE_LONG) \
	$(BIN_CONSOLE_LONG)
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
$(BIN_SERVICE): $(OBJS_CPP_COMMON) $(OBJS_CU_COMMON) $(OBJS_LOG_SERVICE)
	@$(LD) -o $@ $^ $(LDFLAGS) $(LDLIBS)

$(BIN_CONSOLE): $(OBJS_CPP_COMMON) $(OBJS_CU_COMMON) $(OBJS_LOG_CONSOLE)
	@$(LD) -o $@ $^ $(LDFLAGS) $(LDLIBS)

$(BIN_SERVICE_SHORT): $(OBJS_CPP_SHORT) $(OBJS_CU_SHORT) $(OBJS_LOG_SERVICE_SHORT)
	@$(LD) -o $@ $^ $(LDFLAGS) $(LDLIBS)

$(BIN_CONSOLE_SHORT): $(OBJS_CPP_SHORT) $(OBJS_CU_SHORT) $(OBJS_LOG_CONSOLE_SHORT)
	@$(LD) -o $@ $^ $(LDFLAGS) $(LDLIBS)

$(BIN_SERVICE_LONG): $(OBJS_CPP_LONG) $(OBJS_CU_LONG) $(OBJS_LOG_SERVICE_LONG)
	@$(LD) -o $@ $^ $(LDFLAGS) $(LDLIBS)

$(BIN_CONSOLE_LONG): $(OBJS_CPP_LONG) $(OBJS_CU_LONG) $(OBJS_LOG_CONSOLE_LONG)
	@$(LD) -o $@ $^ $(LDFLAGS) $(LDLIBS)

#===============================================================================
# COMPILE RULES
#===============================================================================
$(BUILD_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	@$(CXX) $(CFLAGS) -MMD -c -o $@ $<

$(BUILD_SHORT)/%.o: %.cpp
	@mkdir -p $(dir $@)
	@$(CXX) $(CFLAGS) $(CFLAGS_SHORT) -MMD -c -o $@ $<

$(BUILD_LONG)/%.o: %.cpp
	@mkdir -p $(dir $@)
	@$(CXX) $(CFLAGS) $(CFLAGS_LONG) -MMD -c -o $@ $<

$(BUILD_DIR)/%.cu.o: %.cu
	@mkdir -p $(dir $@)
	@$(NXX) $(CUDA_DEFINES) $(CUDA_INCLUDES) $(CUDA_FLAGS) -MMD -c -o $@ $<

$(BUILD_SHORT)/%.cu.o: %.cu
	@mkdir -p $(dir $@)
	@$(NXX) $(CUDA_DEFINES) $(CUDA_INCLUDES) $(CUDA_FLAGS) -MMD -c -o $@ $<

$(BUILD_LONG)/%.cu.o: %.cu
	@mkdir -p $(dir $@)
	@$(NXX) $(CUDA_DEFINES) $(CUDA_INCLUDES) $(CUDA_FLAGS) -MMD -c -o $@ $<

$(BUILD_DIR)/gy_logging_syslog.o: $(LOG_CPP)
	@mkdir -p $(dir $@)
	@$(CXX) $(CFLAGS) -DUSE_SYSLOG -MMD -c -o $@ $<

$(BUILD_DIR)/gy_logging_console.o: $(LOG_CPP)
	@mkdir -p $(dir $@)
	@$(CXX) $(CFLAGS) -MMD -c -o $@ $<

$(BUILD_SHORT)/gy_logging_syslog.o: $(LOG_CPP)
	@mkdir -p $(dir $@)
	@$(CXX) $(CFLAGS) $(CFLAGS_SHORT) -DUSE_SYSLOG -MMD -c -o $@ $<

$(BUILD_SHORT)/gy_logging_console.o: $(LOG_CPP)
	@mkdir -p $(dir $@)
	@$(CXX) $(CFLAGS) $(CFLAGS_SHORT) -MMD -c -o $@ $<

$(BUILD_LONG)/gy_logging_syslog.o: $(LOG_CPP)
	@mkdir -p $(dir $@)
	@$(CXX) $(CFLAGS) $(CFLAGS_LONG) -DUSE_SYSLOG -MMD -c -o $@ $<

$(BUILD_LONG)/gy_logging_console.o: $(LOG_CPP)
	@mkdir -p $(dir $@)
	@$(CXX) $(CFLAGS) $(CFLAGS_LONG) -MMD -c -o $@ $<

#===============================================================================
# CLEAN
#===============================================================================
clean:
	@$(RM) -rf $(BUILD_DIR)

distclean: clean

#===============================================================================
# AUTO DEPENDENCIES (.d)
#===============================================================================
-include $(OBJS_CPP_COMMON:.o=.d) $(OBJS_CPP_SHORT:.o=.d) $(OBJS_CPP_LONG:.o=.d) \
         $(OBJS_CU_COMMON:.o=.d)  $(OBJS_CU_SHORT:.o=.d)  $(OBJS_CU_LONG:.o=.d)

