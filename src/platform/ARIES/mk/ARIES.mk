# ==============================================================================
# Makefile for C-DAC ARIES v3.0 Platform (THEJAS32 SoC / VEGA ET1031 RISC-V)
# Inviolable Silicon Invariant: RV32IM only, strictly 0 compressed instructions
# Reference: PLAN/02_BUILD_SYSTEM_AND_MEMORY_OPTIMIZATION.md (02_A, 02_B, 02_E)
# ==============================================================================

PLATFORM_SDK := none

DEFAULT_OUTPUT := bin

# ------------------------------------------------------------------------------
# 1. Dynamic Toolchain Discovery Engine (Zero Hardcoded Workstation Paths)
# ------------------------------------------------------------------------------
# Priority 1: User-specified RISCV_TOOLCHAIN_PATH environment variable
# Priority 2: Standard cross-compiler candidates on host PATH
# Priority 3: Common toolchain installation locations (e.g. ~/.espressif/tools/...)

TOOLCHAIN_CANDIDATES := riscv32-esp-elf-gcc riscv-none-elf-gcc riscv64-unknown-elf-gcc

ifeq ($(ARM_SDK_PREFIX),)
  # Check if RISCV_TOOLCHAIN_PATH was provided by environment
  ifneq ($(RISCV_TOOLCHAIN_PATH),)
    DISCOVERED_CC := $(firstword $(wildcard $(foreach c,$(TOOLCHAIN_CANDIDATES),$(RISCV_TOOLCHAIN_PATH)/$(c))))
  endif

  # If not found yet, query PATH using 'which'
  ifeq ($(DISCOVERED_CC),)
    DISCOVERED_CC := $(firstword $(foreach c,$(TOOLCHAIN_CANDIDATES),$(shell which $(c) 2>/dev/null)))
  endif

  # If still not found, search known default directories
  ifeq ($(DISCOVERED_CC),)
    KNOWN_TOOLCHAIN_DIRS := \
        $(firstword $(wildcard $(HOME)/.espressif/tools/riscv32-esp-elf/*/riscv32-esp-elf/bin)) \
        /usr/local/bin \
        /usr/bin
    DISCOVERED_CC := $(firstword $(wildcard $(foreach d,$(KNOWN_TOOLCHAIN_DIRS),$(foreach c,$(TOOLCHAIN_CANDIDATES),$(d)/$(c)))))
  endif

  ifneq ($(DISCOVERED_CC),)
    # Extract the prefix by stripping the compiler candidate suffix ('gcc')
    ARM_SDK_PREFIX := $(patsubst %gcc,%,$(DISCOVERED_CC))
  else
    # Default fallback to PATH invocation
    ARM_SDK_PREFIX := riscv32-esp-elf-
  endif
endif

INCLUDE_DIRS := \
    $(INCLUDE_DIRS) \
    $(TARGET_PLATFORM_DIR) \
    $(TARGET_PLATFORM_DIR)/include

# ------------------------------------------------------------------------------
# 2. Architecture & Silicon Boundary Flags
# ------------------------------------------------------------------------------
# The core does NOT implement compressed ('C') or hardware float ('F'/'D') extensions.
ARCH_FLAGS   = -march=rv32im_zicsr -mabi=ilp32 -mcmodel=medany

# Memory builtins disabled to prevent compiler from emitting calls to compressed libc helpers
DEVICE_FLAGS = -DARIES -DTHEJAS32 -DUSE_CONFIG \
               -Wno-type-limits -Wno-sign-compare -Wno-unused-parameter \
               -Wno-incompatible-pointer-types -ffast-math -fno-math-errno \
               -fno-builtin-memset -fno-builtin-memcpy -fno-builtin-memmove \
               -DARIES_DEV_LOGGING=1

LD_SCRIPT    = $(LINKER_DIR)/aries_v3.ld

# ------------------------------------------------------------------------------
# 3. Dynamic Multilib RVC Isolation Engine
# ------------------------------------------------------------------------------
# Espressif and standard RISC-V GCC toolchains default to multilib variants containing
# compressed opcodes (rv32imafc). We query the cross-compiler with pure RV32I flags to
# guarantee linking strictly against pure uncompressed archives (rv32i_zicsr_zifencei/ilp32).

TOOLCHAIN_RV32I_LIB := $(shell $(ARM_SDK_PREFIX)gcc -march=rv32i_zicsr_zifencei -mabi=ilp32 -print-file-name=libc.a 2>/dev/null | xargs dirname)
TOOLCHAIN_RV32I_LIBGCC := $(shell $(ARM_SDK_PREFIX)gcc -march=rv32i_zicsr_zifencei -mabi=ilp32 -print-file-name=libgcc.a 2>/dev/null | xargs dirname)

ifeq ($(wildcard $(TOOLCHAIN_RV32I_LIB)/libc.a),)
  # Fallback for toolchains without the zifencei multilib name
  TOOLCHAIN_RV32I_LIB    := $(shell $(ARM_SDK_PREFIX)gcc -march=rv32i_zicsr -mabi=ilp32 -print-file-name=libc.a 2>/dev/null | xargs dirname)
  TOOLCHAIN_RV32I_LIBGCC := $(shell $(ARM_SDK_PREFIX)gcc -march=rv32i_zicsr -mabi=ilp32 -print-file-name=libgcc.a 2>/dev/null | xargs dirname)
endif

# ------------------------------------------------------------------------------
# 4. Aggressive Optimization for 248 KB SRAM Execution
# ------------------------------------------------------------------------------
OPTIMISE_DEFAULT := -Os
OPTIMISE_SPEED   := -Os
LTO_FLAGS        := $(OPTIMISATION_BASE) -Os

MCU_COMMON_SRC := \
    ARIES/startup.S \
    ARIES/system.c \
    ARIES/serial_uart_aries.c \
    ARIES/io_aries.c \
    ARIES/exti_aries.c \
    ARIES/pwm_motor_aries.c \
    ARIES/stub_drivers.c \
    drivers/serial_pinconfig.c \
    drivers/bus_spi_config.c \
    ARIES/bus_spi_aries.c
    # Future Phase Drivers (uncomment as phases are implemented):
    # ARIES/config_flash_aries.c (Phase 18)

# Surgical exclusion of unused upstream peripherals to fit within 248 KB SRAM budget
MCU_EXCLUDES = \
    drivers/rx/rx_xn297.c \
    drivers/display_ug2864hsweg01.c \
    io/displayport_oled.c \
    drivers/flash/flash.c \
    drivers/flash/flash_fm25v02a.c \
    drivers/flash/flash_m25p16.c \
    drivers/flash/flash_mt29f.c \
    drivers/flash/flash_mx66uw1g45g.c \
    drivers/flash/flash_w25m.c \
    drivers/flash/flash_w25n.c \
    drivers/flash/flash_w25q128fv.c \
    io/flashfs.c \
    drivers/sdcard.c \
    drivers/sdcard_spi.c \
    drivers/sdcard_sdio_baremetal.c \
    drivers/sdcard_standard.c \
    io/asyncfatfs/asyncfatfs.c \
    io/asyncfatfs/fat_standard.c \
    google/olc/olc.c \
    io/rcdevice.c \
    io/rcdevice_cam.c \
    drivers/serial_escserial.c \
    io/serial_4way.c \
    io/serial_4way_stk500v2.c \
    io/serial_4way_avrootloader.c \
    cms/cms_menu_main.c \
    cms/cms_menu_misc.c \
    cms/cms_menu_osd.c \
    cms/cms_menu_power.c \
    cms/cms_menu_saveexit.c \
    cms/cms_menu_vtx_common.c \
    cms/cms_menu_vtx_rtc6705.c \
    cms/cms_menu_vtx_smartaudio.c \
    cms/cms_menu_vtx_tramp.c \
    cms/cms_menu_persistent_stats.c \
    cms/cms_menu_rpm_limit.c \
    cms/cms_menu_quick.c \
    cms/cms_menu_vtx_msp.c \
    io/vtx.c \
    io/vtx_rtc6705.c \
    io/vtx_smartaudio.c \
    io/vtx_tramp.c \
    io/vtx_control.c \
    io/spektrum_vtx_control.c \
    io/vtx_msp.c \
    osd/osd.c \
    osd/osd_custom_text.c \
    osd/osd_elements.c \
    osd/osd_nav_map.c \
    osd/osd_warnings.c \
    drivers/rx/rx_a7105.c \
    drivers/rx/rx_cc2500.c \
    drivers/rx/rx_cyrf6936.c \
    drivers/rx/rx_nrf24l01.c \
    drivers/rx/rx_sx127x.c \
    drivers/rx/rx_sx1280.c \
    rx/cc2500_common.c \
    rx/cc2500_frsky_shared.c \
    rx/cc2500_frsky_d.c \
    rx/cc2500_frsky_x.c \
    rx/cc2500_sfhss.c \
    rx/cc2500_redpine.c \
    rx/a7105_flysky.c \
    rx/cyrf6936_spektrum.c \
    drivers/barometer/barometer_2smpb_02b.c \
    drivers/barometer/barometer_bmp085.c \
    drivers/barometer/barometer_bmp280.c \
    drivers/barometer/barometer_bmp388.c \
    drivers/barometer/barometer_bmp5xx.c \
    drivers/barometer/barometer_dps310.c \
    drivers/barometer/barometer_lps22df.c \
    drivers/barometer/barometer_lps.c \
    drivers/barometer/barometer_ms5611.c \
    drivers/barometer/barometer_qmp6988.c \
    drivers/barometer/barometer_virtual.c \
    drivers/compass/compass_ak8963.c \
    drivers/compass/compass_ak8975.c \
    drivers/compass/compass_ak9916.c \
    drivers/compass/compass_bmm350.c \
    drivers/compass/compass_hmc5883l.c \
    drivers/compass/compass_ist8310.c \
    drivers/compass/compass_lis2mdl.c \
    drivers/compass/compass_lis3mdl.c \
    drivers/compass/compass_mmc560x.c \
    drivers/compass/compass_mpu925x_ak8963.c \
    drivers/compass/compass_qmc5883.c \
    drivers/compass/compass_virtual.c \
    drivers/accgyro/accgyro_mpu6050.c \
    drivers/accgyro/accgyro_spi_bmi160.c \
    drivers/accgyro/accgyro_spi_bmi423.c \
    drivers/accgyro/accgyro_spi_icm20649.c \
    drivers/accgyro/accgyro_spi_icm20689.c \
    drivers/accgyro/accgyro_spi_icm40609.c \
    drivers/accgyro/accgyro_spi_icm456xx.c \
    drivers/accgyro/accgyro_spi_icm56686.c \
    drivers/accgyro/accgyro_spi_l3gd20.c \
    drivers/accgyro/accgyro_spi_lsm6dso.c \
    drivers/accgyro/accgyro_spi_lsm6dso_init.c \
    drivers/accgyro/accgyro_spi_lsm6dsv16x.c \
    drivers/accgyro/accgyro_spi_mpu6000.c \
    drivers/accgyro/accgyro_spi_mpu9250.c \
    drivers/accgyro/accgyro_virtual.c \
    drivers/max7456.c \
    drivers/vtx_rtc6705.c \
    drivers/vtx_rtc6705_soft_spi.c \
    drivers/transponder_ir_arcitimer.c \
    drivers/transponder_ir_ilap.c \
    drivers/transponder_ir_erlt.c \
    drivers/lcd_console.c \
    drivers/lcd_panel/lcd_panel_font_5x7.c \
    drivers/lcd_panel/lcd_panel_stub.c \
    drivers/lcd_panel/ssd1306_i2c.c \
    drivers/display_canvas.c \
    drivers/display.c \
    drivers/buttons.c \
    drivers/camera_control.c \
    drivers/pitot/pitot_ms4525.c \
    drivers/rangefinder/rangefinder_hcsr04.c \
    drivers/rangefinder/rangefinder_lidarmt.c \
    drivers/rangefinder/rangefinder_lidartf.c \
    drivers/rangefinder/rangefinder_nooploop.c \
    drivers/rangefinder/rangefinder_upt1.c

TARGET_MAP   = $(OBJECT_DIR)/$(FORKNAME)_$(TARGET).map

LIBS         = -lm -lc -lgcc

LD_FLAGS    := \
    -L$(TOOLCHAIN_RV32I_LIB) \
    -L$(TOOLCHAIN_RV32I_LIBGCC) \
    $(LIBS) -nostartfiles \
    $(ARCH_FLAGS) \
    $(LTO_FLAGS) \
    $(DEBUG_FLAGS) \
    -Wl,-gc-sections,-Map,$(TARGET_MAP) \
    -Wl,-L$(LINKER_DIR) \
    -Wl,--cref \
    -T$(LD_SCRIPT)
