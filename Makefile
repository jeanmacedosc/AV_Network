CXX         := riscv64-linux-gnu-g++
TARGET_NAME := gateway_riscv
INJECTOR_NAME := injector

STAGING_DIR := staging
OS_DIR      := os
BUILD_DIR   := build_riscv

TARGET      := $(STAGING_DIR)/bin/$(TARGET_NAME)
INJECTOR_BIN := $(STAGING_DIR)/bin/$(INJECTOR_NAME)
CPIO_IMG    := $(OS_DIR)/initramfs.cpio

ARCH_FLAGS := -march=rv64gc_zba_zbb -mtune=sifive-u74
CXXFLAGS   := -std=c++17 -O3 -flto -static -pthread -Wall $(ARCH_FLAGS)
INCLUDES   := -I. -Isrc

CAN_SETUP_NAME := can_setup
CAN_SETUP_BIN  := $(STAGING_DIR)/bin/$(CAN_SETUP_NAME)
CAN_SETUP_SRC  := src/can_setup.cpp

SRCS_ROOT := main.cpp
SRCS_SRC  := $(filter-out src/injector.cpp src/can_setup.cpp, $(wildcard src/*.cpp))
SRCS_GW   := $(SRCS_ROOT) $(SRCS_SRC)

SRCS_INJ  := src/injector.cpp

OBJS_GW   := $(SRCS_GW:%.cpp=$(BUILD_DIR)/%.o)
OBJS_INJ  := $(SRCS_INJ:%.cpp=$(BUILD_DIR)/%.o)

.PHONY: all clean initramfs

all: $(TARGET) $(INJECTOR_BIN) $(CAN_SETUP_BIN) initramfs

# Gateway
$(TARGET): $(OBJS_GW)
	@echo "[RISC-V] Linking Gateway to $@"
	@mkdir -p $(dir $@)
	@$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ $(OBJS_GW)

# Injector
$(INJECTOR_BIN): $(OBJS_INJ)
	@echo "[RISC-V] Linking Injector to $@"
	@mkdir -p $(dir $@)
	@$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ $(OBJS_INJ)

$(CAN_SETUP_BIN): $(CAN_SETUP_SRC)
	@echo "[RISC-V] Compiling CAN Setup Tool..."
	@mkdir -p $(dir $@)
	@$(CXX) $(CXXFLAGS) $(CAN_SETUP_SRC) -o $@

$(BUILD_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	@echo "[RISC-V] Compiling $<"
	@$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

initramfs: $(TARGET) $(INJECTOR_BIN)
	@echo "[RISC-V] Packing initramfs.cpio..."
	@mkdir -p $(OS_DIR)
	cd $(STAGING_DIR) && find . | cpio -H newc -o > ../$(OS_DIR)/initramfs.cpio
	@echo "Success! Image generated at: $(CPIO_IMG)"

clean:
	@echo "[RISC-V] Cleaning..."
	@rm -rf $(BUILD_DIR)
	@rm -f $(TARGET) $(INJECTOR_BIN) $(CPIO_IMG)
