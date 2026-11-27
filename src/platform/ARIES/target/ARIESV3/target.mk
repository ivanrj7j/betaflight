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
# Exact source paths from mk/source.mk for subsystems not present on ARIES v3.0.
MCU_EXCLUDES += \
    blackbox/blackbox.c \
    blackbox/blackbox_encoding.c \
    blackbox/blackbox_io.c \
    BoschSensortec/BMI270-Sensor-API/bmi270_maximum_fifo.c \
    cms/cms.c \
    cms/cms_menu_blackbox.c \
    cms/cms_menu_failsafe.c \
    cms/cms_menu_firmware.c \
    cms/cms_menu_gps_lap_timer.c \
    cms/cms_menu_gps_rescue_multirotor.c \
    cms/cms_menu_gps_rescue_wing.c \
    cms/cms_menu_imu.c \
    cms/cms_menu_ledstrip.c \
    common/colorconversion.c \
    common/gps_conversion.c \
    common/huffman.c \
    common/huffman_table.c \
    common/sdft.c \
    common/uvarint.c \
    drivers/accgyro/accgyro_mpu6500.c \
    drivers/accgyro/accgyro_spi_mpu6500.c \
    drivers/bus_octospi.c \
    drivers/bus_quadspi.c \
    drivers/dshot_command.c \
    drivers/light_ws2811strip.c \
    drivers/pinio.c \
    drivers/rx/expresslrs_driver.c \
    drivers/rx/rx_pwm.c \
    drivers/rx/rx_spi.c \
    drivers/serial_lcd_console.c \
    drivers/sound_beeper.c \
    fc/gps_lap_timer.c \
    flight/alt_hold_multirotor.c \
    flight/alt_hold_wing.c \
    flight/autopilot_multirotor.c \
    flight/autopilot_wing.c \
    flight/flight_plan_capture.c \
    flight/flight_plan_nav.c \
    flight/flight_plan_nav_wing.c \
    flight/gps_rescue_multirotor.c \
    flight/gps_rescue_wing.c \
    flight/landing_wing.c \
    flight/launch_wing.c \
    flight/mixer_tricopter.c \
    flight/nav_trail.c \
    flight/pos_hold_multirotor.c \
    flight/pos_hold_wing.c \
    flight/position.c \
    flight/position_estimator.c \
    flight/position_filter.c \
    flight/position_nav.c \
    flight/rpm_filter.c \
    flight/servos.c \
    flight/servos_tricopter.c \
    io/dashboard.c \
    io/displayport_crsf.c \
    io/displayport_fb_osd.c \
    io/displayport_frsky_osd.c \
    io/displayport_hott.c \
    io/displayport_max7456.c \
    io/displayport_msp.c \
    io/displayport_srxl.c \
    io/frsky_osd.c \
    io/gimbal_control.c \
    io/gps.c \
    io/ledstrip.c \
    io/pidaudio.c \
    io/piniobox.c \
    io/smartaudio_protocol.c \
    io/spektrum_rssi.c \
    io/tramp_protocol.c \
    io/transponder_ir.c \
    io/usb_cdc_hid.c \
    io/usb_msc.c \
    rx/expresslrs.c \
    rx/expresslrs_common.c \
    rx/expresslrs_telemetry.c \
    rx/fport.c \
    rx/frsky_crc.c \
    rx/ghst.c \
    rx/ibus.c \
    rx/jetiexbus.c \
    rx/mavlink.c \
    rx/msp.c \
    rx/msp_override.c \
    rx/pwm.c \
    rx/rx_spi.c \
    rx/rx_spi_common.c \
    rx/sbus.c \
    rx/sbus_channels.c \
    rx/spektrum.c \
    rx/srxl2.c \
    rx/sumd.c \
    rx/sumh.c \
    rx/xbus.c \
    sensors/adcinternal.c \
    sensors/barometer.c \
    sensors/compass.c \
    sensors/esc_sensor.c \
    sensors/opticalflow.c \
    sensors/pitot.c \
    sensors/rangefinder.c \
    telemetry/crsf.c \
    telemetry/frsky_hub.c \
    telemetry/ghst.c \
    telemetry/hott.c \
    telemetry/ibus.c \
    telemetry/ibus_shared.c \
    telemetry/jetiexbus.c \
    telemetry/ltm.c \
    telemetry/mavlink.c \
    telemetry/mavlink_mission.c \
    telemetry/msp_shared.c \
    telemetry/smartport.c \
    telemetry/srxl.c \
    telemetry/telemetry.c
