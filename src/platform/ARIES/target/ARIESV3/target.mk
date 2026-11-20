# Target makefile for C-DAC ARIES v3.0 (THEJAS32 SoC / VEGA ET1031 RISC-V)
# Citations:
#   - PLAN/02_BUILD_SYSTEM_AND_MEMORY_OPTIMIZATION.md (Microcomponent 02_B)
#   - knowledgebase/components/11_TARGET_OPTIMIZATION_BUDGET.md
#   - knowledgebase/TARGET_OPTIMIZATION_128KB.md

TARGET_MCU        := THEJAS32
TARGET_MCU_FAMILY := ARIES
MCU_FLASH_SIZE    := 256
TARGET_FLASH_SIZE := 256

# Memory-bounded linker script binding (Ground Truth 2: 248 KB SRAM budget)
LD_SCRIPT         := $(PLATFORM_DIR)/link/aries_v3.ld

# Modular Subsystem Pruning for ARIES v3.0 (Ground Truth 2: <= 248 KB SRAM ceiling)
# Excludes drivers and subsystems for hardware not physically present on ARIES v3.0.
# Eliminates > 500 KB of bloat from the compilation tree.
MCU_EXCLUDES += \
    drivers/display \
    io/displayport \
    io/osd \
    io/cms \
    io/vtx \
    drivers/vtx \
    drivers/barometer \
    drivers/compass \
    drivers/sdcard \
    io/flashfs \
    io/asyncfatfs \
    flight/failsafe_gps \
    navigation
