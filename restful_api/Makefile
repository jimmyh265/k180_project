# 編譯器與參數
CC      = gcc
CFLAGS  = -Wall -O2
LDLIBS_BIN = -luuid -lpthread -lmicrohttpd -ljson-c -lcrypt

# 來源與目標資料夾
SRC_DIR   = src
BUILD_DIR = build
PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
INSTALL ?= install

# 可執行檔與來源檔定義
BIN = $(BUILD_DIR)/restful_api_server

SRCS = $(SRC_DIR)/restful_api_server.c

OBJS = $(SRCS:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)

# 編譯規則
all: $(BIN)

.PHONY: all install clean

$(BIN): $(OBJS)
	@echo Linking $@
	@$(CC) -o $@ $^ $(LDLIBS_BIN)

# 通用編譯規則
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# 清除
clean:
	rm -rf $(BUILD_DIR)

install: all
	$(INSTALL) -d -m 0755 $(BINDIR)
	$(INSTALL) -m 0755 $(BIN) $(BINDIR)/restful_api_server
