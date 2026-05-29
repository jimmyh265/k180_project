# K180 system helper binaries used by PHP and REST API.
CC      = gcc
CXX     = g++
CFLAGS  = -Wall -O2
CXXFLAGS = -Wall -O2

SRC_DIR   = src
BUILD_DIR = build
PREFIX ?= /usr/local
LIBEXECDIR ?= $(PREFIX)/libexec/k180
INSTALL ?= install
INSTALL_OWNER ?= root
INSTALL_GROUP ?= k180
INSTALL_MODE ?= 0750

BIN_RESTART = $(BUILD_DIR)/restart_program_camera
BIN_REBOOT  = $(BUILD_DIR)/reboot_system
BIN_NETPLAN = $(BUILD_DIR)/modify_sysip_netplan

SRCS_RESTART = $(SRC_DIR)/restart_program_camera.cpp
SRCS_REBOOT  = $(SRC_DIR)/reboot_now.c
SRCS_NETPLAN = $(SRC_DIR)/modify_sysip_netplan.c

OBJS_RESTART = $(SRCS_RESTART:$(SRC_DIR)/%.cpp=$(BUILD_DIR)/%.o)
OBJS_REBOOT  = $(SRCS_REBOOT:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)
OBJS_NETPLAN = $(SRCS_NETPLAN:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)

all: $(BIN_RESTART) $(BIN_REBOOT) $(BIN_NETPLAN)

.PHONY: all install clean

$(BIN_RESTART): $(OBJS_RESTART)
	@echo Linking $@
	@$(CXX) -o $@ $^

$(BIN_REBOOT): $(OBJS_REBOOT)
	@echo Linking $@
	@$(CC) -o $@ $^

$(BIN_NETPLAN): $(OBJS_NETPLAN)
	@echo Linking $@
	@$(CC) -o $@ $^ -ljson-c

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR)

install: all
	$(INSTALL) -d -o $(INSTALL_OWNER) -g $(INSTALL_GROUP) -m $(INSTALL_MODE) $(LIBEXECDIR)
	$(INSTALL) -o $(INSTALL_OWNER) -g $(INSTALL_GROUP) -m $(INSTALL_MODE) $(BIN_RESTART) $(LIBEXECDIR)/restart_program_camera
	$(INSTALL) -o $(INSTALL_OWNER) -g $(INSTALL_GROUP) -m $(INSTALL_MODE) $(BIN_REBOOT) $(LIBEXECDIR)/reboot_system
	$(INSTALL) -o $(INSTALL_OWNER) -g $(INSTALL_GROUP) -m $(INSTALL_MODE) $(BIN_NETPLAN) $(LIBEXECDIR)/modify_sysip_netplan
