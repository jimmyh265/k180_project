CC = gcc
CFLAGS = -Wall -O2 -lssl -lcrypto
INCLUDES = -Iinclude
HOST_SRC_DIR = host_tools
DEVICE_SRC_DIR = device_tools
BUILD_DIR = build
USERS = fourd kinghold JongShyn
PREFIX ?= /usr/local
LIBEXECDIR ?= $(PREFIX)/libexec/k180
INSTALL ?= install
INSTALL_OWNER ?= root
INSTALL_GROUP ?= k180
INSTALL_MODE ?= 0750

ENC_BINS = $(addprefix build/gen_fw_enc_, $(USERS))
DEC_BINS = $(addprefix build/gen_fw_dec_, $(USERS))

all: $(ENC_BINS) $(DEC_BINS)

.PHONY: all clean install-dec-fourd

# 動態 include 對應的 user config 檔
build/gen_fw_enc_%: $(HOST_SRC_DIR)/gen_fw_enc.c include/config_user_%.h | build
	$(CC) $< -o $@ $(CFLAGS) $(INCLUDES) -include include/config_user_$*.h -DUSER_NAME=$*

build/gen_fw_dec_%: $(DEVICE_SRC_DIR)/gen_fw_dec.c include/config_user_%.h | build
	$(CC) $< -o $@ $(CFLAGS) $(INCLUDES) -include include/config_user_$*.h -DUSER_NAME=$*

build:
	mkdir -p build

clean:
	rm -rf build

install-dec-fourd: build/gen_fw_dec_fourd
	$(INSTALL) -d -o $(INSTALL_OWNER) -g $(INSTALL_GROUP) -m $(INSTALL_MODE) $(LIBEXECDIR)
	$(INSTALL) -o $(INSTALL_OWNER) -g $(INSTALL_GROUP) -m $(INSTALL_MODE) $< $(LIBEXECDIR)/gen_fw_dec
