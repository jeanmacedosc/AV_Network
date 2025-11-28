# Makefile for VisionFive 2 Gateway (RISC-V)

CXX         := riscv64-linux-gnu-g++
TARGET_NAME := gateway_riscv
SETUP_NAME  := can_setup

STAGING_DIR := staging
OS_DIR      := os
BUILD_DIR   := build_riscv

TARGET_BIN  := $(STAGING_DIR)/bin/$(TARGET_NAME)
SETUP_BIN   := $(STAGING_DIR)/bin/$(SETUP_NAME)
CPIO_IMG    := $(OS_DIR)/initramfs.cpio

ARCH_FLAGS  := -march=rv64gc_zba_zbb -mtune=sifive-u74
CXXFLAGS    := -std=c++17 -O3 -flto -static -pthread -Wall $(ARCH_FLAGS)

INCLUDES    := -Iinclude -Isrc -Isrc/hal -Isrc/observer

SRCS_GW     := src/main.cpp \
               src/gateway.cpp \
               $(wildcard src/hal/*.cpp)

SRCS_SETUP  := src/can_setup.cpp

OBJS_GW     := $(SRCS_GW:%.cpp=$(BUILD_DIR)/%.o)

.PHONY: all clean initramfs

all: $(TARGET_BIN) $(SETUP_BIN) initramfs

$(TARGET_BIN): $(OBJS_GW)
	@echo "[RISC-V] Linking Gateway to $@"
	@mkdir -p $(dir $@)
	@$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ $(OBJS_GW)

$(SETUP_BIN): $(SRCS_SETUP)
	@echo "[RISC-V] Compiling CAN Setup Tool..."
	@mkdir -p $(dir $@)
	@$(CXX) $(CXXFLAGS) $(INCLUDES) $(SRCS_SETUP) -o $@

$(BUILD_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	@echo "[RISC-V] Compiling $<"
	@$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

initramfs: $(TARGET_BIN) $(SETUP_BIN)
	@echo "[RISC-V] Packing initramfs.cpio..."
	@mkdir -p $(OS_DIR)
	cd $(STAGING_DIR) && find . | cpio -H newc -o > ../$(OS_DIR)/initramfs.cpio
	@echo "Success! Image generated at: $(CPIO_IMG)"

clean:
	@echo "[RISC-V] Cleaning..."
	@rm -rf $(BUILD_DIR)
	@rm -f $(TARGET_BIN) $(SETUP_BIN) $(CPIO_IMG)