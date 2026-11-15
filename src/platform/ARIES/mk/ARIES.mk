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
DEVICE_FLAGS = -DARIES -DTHEJAS32 -DUSE_CONFIG -Wno-type-limits -Wno-sign-compare -Wno-unused-parameter -Wno-incompatible-pointer-types -ffast-math -fno-math-errno -fno-builtin-memset -fno-builtin-memcpy -fno-builtin-memmove
LD_SCRIPT    = $(LINKER_DIR)/aries_v3.ld

# Multilib path override: the toolchain lacks rv32im multilib and defaults to rv32imafc with RVC.
# We explicitly link against the rv32i_zicsr_zifencei/ilp32 multilib to guarantee 0 compressed/FPU instructions.
TOOLCHAIN_RV32I_LIB    := /home/ivan/.espressif/tools/riscv32-esp-elf/esp-13.2.0_20250707/riscv32-esp-elf/riscv32-esp-elf/lib/rv32i_zicsr_zifencei/ilp32
TOOLCHAIN_RV32I_LIBGCC := /home/ivan/.espressif/tools/riscv32-esp-elf/esp-13.2.0_20250707/riscv32-esp-elf/lib/gcc/riscv32-esp-elf/13.2.0/rv32i_zicsr_zifencei/ilp32

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
