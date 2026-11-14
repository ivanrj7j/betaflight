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
DEVICE_FLAGS = -DARIES -DTHEJAS32 -DUSE_CONFIG -Wno-type-limits -Wno-sign-compare -Wno-unused-parameter -Wno-incompatible-pointer-types -ffast-math -fno-math-errno
LD_SCRIPT    = $(LINKER_DIR)/aries_v3.ld

# Size optimization for in-SRAM execution (< 248 KB)
OPTIMISE_DEFAULT := -Os
OPTIMISE_SPEED   := -Os
LTO_FLAGS        := $(OPTIMISATION_BASE) -Os

MCU_COMMON_SRC := \
    ARIES/startup.S \
    ARIES/system.c \
    ARIES/serial_uart_aries.c \
    ARIES/io_aries.c \
    ARIES/exti_aries.c \
    ARIES/bus_spi_aries.c \
    ARIES/config_flash_aries.c \
    ARIES/pwm_motor_aries.c \
    ARIES/stub_drivers.c \
    drivers/serial_pinconfig.c

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
    drivers/compass/compass_virtual.c

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
