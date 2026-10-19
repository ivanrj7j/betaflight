# Makefile for ARIES platform (THEJAS32 / VEGA ET1031 RISC-V)

PLATFORM_SDK := none

DEFAULT_OUTPUT := elf

# Set toolchain prefix to the installed ESP RISC-V toolchain
ARM_SDK_PREFIX ?= /home/ivan/.espressif/tools/riscv32-esp-elf/esp-13.2.0_20250707/riscv32-esp-elf/bin/riscv32-esp-elf-

INCLUDE_DIRS := \
    $(INCLUDE_DIRS) \
    $(TARGET_PLATFORM_DIR) \
    $(TARGET_PLATFORM_DIR)/include

ARCH_FLAGS   = -march=rv32im_zicsr -mabi=ilp32 -mcmodel=medany
DEVICE_FLAGS = -DARIES -DTHEJAS32 -Wno-type-limits -Wno-sign-compare -Wno-unused-parameter -Wno-incompatible-pointer-types
LD_SCRIPT    = $(LINKER_DIR)/aries_v3.ld

MCU_COMMON_SRC := \
    ARIES/startup.S \
    ARIES/system.c \
    ARIES/serial_uart_aries.c \
    ARIES/stub_drivers.c \
    drivers/serial_pinconfig.c

MCU_EXCLUDES = \
    drivers/rx/rx_xn297.c \
    drivers/display_ug2864hsweg01.c \
    io/displayport_oled.c

TARGET_MAP   = $(OBJECT_DIR)/$(FORKNAME)_$(TARGET).map

LIBS         = -lm -lc -lgcc

LD_FLAGS    := \
    $(LIBS) -nostartfiles \
    $(ARCH_FLAGS) \
    $(LTO_FLAGS) \
    $(DEBUG_FLAGS) \
    -Wl,-gc-sections,-Map,$(TARGET_MAP) \
    -Wl,-L$(LINKER_DIR) \
    -Wl,--cref \
    -T$(LD_SCRIPT)
