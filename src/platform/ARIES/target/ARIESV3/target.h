/*
 * This file is part of Betaflight.
 * Target configuration for C-DAC ARIES v3.0 (THEJAS32 / VEGA ET1031 RISC-V RV32IM @ 100 MHz).
 *
 * Citations:
 *   - knowledgebase/components/09_BUILD_LINKER_AND_MEMORY.md
 *   - knowledgebase/codebase_evaluations/14_EVAL_TARGET_H_AND_CONFIG.md
 *   - PLAN/02_BUILD_SYSTEM_AND_MEMORY_OPTIMIZATION.md (Microcomponent 02_C)
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TARGET_BOARD_IDENTIFIER "ARV3"
#define USBD_PRODUCT_STRING     "ARIES v3.0"

#define SYSTEM_HSE_MHZ          100
#define DEFAULT_CPU_OVERCLOCK   0

// DMA attributes for architecture without hardware DMA
#define DMA_RAM
#define DMA_RW_AXI
#define DMA_RAM_R
#define DMA_RAM_W
#define DMA_RAM_RW
#define DMA_DATA_ZERO_INIT
#define DMA_DATA
#define STATIC_DMA_DATA_AUTO    static

// Prune memory-heavy and unsupported subsystems to guarantee <= 248 KB SRAM binary footprint
#undef USE_VCP
#undef USE_USB_MSC
#undef USE_USB_CDC_HID
#undef USE_SDCARD
#undef USE_MAX7456
#undef USE_CAMERA_CONTROL
#undef USE_PINIO
#undef USE_PINIOBOX
#undef USE_TRANSPONDER
#undef USE_DSHOT
#undef USE_DSHOT_BITBANG
#undef USE_DSHOT_DMAR
#undef USE_DSHOT_TELEMETRY
#undef USE_DSHOT_TELEMETRY_STATS
#undef USE_RPM_FILTER
#undef USE_RPM_LIMIT
#undef USE_ESC_SENSOR
#undef USE_ESC_SENSOR_INFO
#undef USE_ESC_SENSOR_TELEMETRY
#define USE_EXTI
#undef USE_OCTOSPI
#undef USE_QUADSPI
#undef USE_FLASH
#undef USE_FLASH_CHIP
#undef USE_FLASH_TOOLS
#undef USE_FLASHFS
#undef USE_RX_SPI
#undef USE_RX_PWM
#undef USE_RX_PPM
#undef USE_RX_MSP
#undef USE_RX_MSP_OVERRIDE
#undef USE_LED_STRIP
#undef USE_CANVAS
#undef USE_OSD
#undef USE_OSD_SD
#undef USE_OSD_HD
#undef USE_OSD_QUICK_MENU
#undef USE_OSD_ADJUSTMENTS
#undef USE_OSD_PROFILES
#undef USE_OSD_STICK_OVERLAY
#undef USE_OSD_CUSTOM_TEXT
#undef ENABLE_OSD_CUSTOM_TEXT
#define ENABLE_OSD_CUSTOM_TEXT 0
#undef USE_RC_STATS
#undef USE_SPEC_PREARM_SCREEN
#undef USE_BLACKBOX
#undef USE_CMS
#undef USE_CMS_FAILSAFE_MENU
#undef USE_EXTENDED_CMS_MENUS
#undef USE_MSP_DISPLAYPORT
#undef USE_OSD_OVER_MSP_DISPLAYPORT
#undef USE_VTX
#undef USE_VTX_COMMON
#undef USE_VTX_CONTROL
#undef USE_VTX_SMARTAUDIO
#undef USE_VTX_TRAMP
#undef USE_VTX_MSP
#undef USE_VTX_TABLE
#undef USE_VTX_RTC6705
#undef USE_VTX_RTC6705_SOFTSPI
#undef USE_SERVOS
#undef USE_GPS
#undef USE_GPS_PLUS_CODES
#undef USE_GPS_RESCUE
#undef USE_ALTITUDE_HOLD
#undef USE_POSITION_HOLD
#undef USE_TELEMETRY
#undef USE_TELEMETRY_CRSF
#undef USE_TELEMETRY_FRSKY_HUB
#undef USE_TELEMETRY_SMARTPORT
#undef USE_TELEMETRY_GHST
#undef USE_TELEMETRY_SRXL
#undef USE_MSP_OVER_TELEMETRY
#undef USE_CRSF_V3
#undef USE_CRSF_ACCGYRO_TELEMETRY
#undef USE_CRSF_CMS_TELEMETRY
#undef USE_SERIALRX_GHST
#undef USE_SERIALRX_IBUS
#undef USE_SERIALRX_SBUS
#undef USE_SERIALRX_SPEKTRUM
#undef USE_SERIALRX_FPORT
#undef USE_SERIALRX_XBUS
#undef USE_SERIALRX_SRXL2
#undef USE_SERIALRX_JETIEXBUS
#undef USE_SERIALRX_SUMD
#undef USE_SERIALRX_SUMH
#undef USE_SERIALRX_MAVLINK
#undef USE_SPEKTRUM_BIND
#undef USE_SPEKTRUM_BIND_PLUG
#undef USE_SPEKTRUM_REAL_RSSI
#undef USE_SPEKTRUM_VIRTUAL_RSSI
#undef USE_SPEKTRUM_RSSI_PERCENT_CONVERSION
#undef USE_SPEKTRUM_VTX_CONTROL
#undef USE_SPEKTRUM_VTX_TELEMETRY
#undef USE_SPEKTRUM_CMS_TELEMETRY
#undef USE_HOTT_TEXTMODE
#undef USE_RCDEVICE
#undef USE_DASHBOARD
#undef USE_LAUNCH_CONTROL
#undef USE_RUNAWAY_TAKEOFF
#undef USE_YAW_SPIN_RECOVERY
#undef USE_GYRO_OVERFLOW_CHECK
#undef USE_VIRTUAL_CURRENT_METER
#undef USE_BATTERY_CONTINUE
#undef USE_BATTERY_VOLTAGE_SAG_COMPENSATION
#undef USE_PERSISTENT_STATS
#undef USE_PROFILE_NAMES
#undef USE_CUSTOM_BOX_NAMES
#undef USE_SENSOR_NAMES
#undef USE_UNCOMMON_MIXERS
#undef USE_RTC_TIME
#undef USE_SIGNATURE
#undef USE_SERIAL_4WAY_BLHELI_BOOTLOADER
#undef USE_SERIAL_4WAY_SK_BOOTLOADER
#undef USE_SERIAL_4WAY_BLHELI_INTERFACE
#undef USE_SERIAL_PASSTHROUGH
#undef USE_GYRO_REGISTER_DUMP
#undef USE_BEEPER
// shortcut: USE_ADC disabled until external I2C2 U9 driver is implemented (prevents 0V lockout)
#undef USE_ADC
#undef USE_ADC_INTERNAL

#undef USE_MAG
#undef USE_BARO

// Core flight features
#define TARGET_PREINIT
#define USE_SPI
#define USE_SPI_DEVICE_1
#define USE_GYRO
#define USE_GYRO_SPI_ICM42688P
#define USE_ACC
#define USE_ACC_SPI_ICM42688P
#define USE_CLI
#define USE_SERIAL_UART
#define FAST_MATH

/*
 * Flight loop rate clamping for 100 MHz RV32IM soft-float core (Ground Truth 4):
 * Clamps the PID loop to exactly 1.0 kHz (1000 us), maintaining > 65% idle CPU headroom.
 */
#define DEFAULT_PID_PROCESS_DENOM 8

/*
 * In-SRAM Parameter Group Configuration (Ground Truth 3):
 * Strictly 8192 bytes allocated in SRAM (.bss); flash pointer writes strictly prohibited.
 */
#define CONFIG_IN_RAM
#define EEPROM_SIZE                     8192
#undef CONFIG_IN_FLASH
#undef ENABLE_CONFIG_FLASH_INIT

/*
 * -----------------------------------------------------------------------------
 * Zero-Conflict 12-Pin Allocation Matrix (Headers J1 & J2)
 * Reference: knowledgebase/codebase_evaluations/14_EVAL_TARGET_H_AND_CONFIG.md
 * -----------------------------------------------------------------------------
 * Header J1:
 *   - PA0: Gyro EXTI DRDY (ICM-42688P Data Ready Interrupt)
 *   - PA4: Motor 4 PWM Output
 *   - PA5: SPI0 SCK  (12.5 MHz Clamped)
 *   - PA6: SPI0 SDI  (MISO)
 *   - PA7: SPI0 SDO  (MOSI)
 *   - PA8: SPI0 CS   (ICM-42688P Chip Select)
 *
 * Header J2:
 *   - PA1: Motor 1 PWM Output
 *   - PA2: Motor 2 PWM Output
 *   - PA3: Motor 3 PWM Output
 *
 * Onboard Dedicated:
 *   - PA16: UART1 TX (CRSF / ELRS Receiver)
 *   - PA17: UART1 RX (CRSF / ELRS Receiver)
 *   - PA22: LED0 (Status Blue LED)
 *   - PA30: UART0 TX (CP2102N USB Bridge)
 *   - PA31: UART0 RX (CP2102N USB Bridge)
 * -----------------------------------------------------------------------------
 */

// Status Indicator & Beeper
#define LED0_PIN                PA22
#define BEEPER_PIN              NONE

// High-Usability Immediate CLI Activation & Generic Polled UART Drain Hook
#define USE_IMMEDIATE_CLI
#define USE_UART_POLLED_DRAIN

// Onboard USB-UART Bridge (CP2102N)
#define USE_UART0
#define UART0_TX_PIN            PA30
#define UART0_RX_PIN            PA31
#define MSP_UART                SERIAL_PORT_UART0

// RC Receiver Interface (CRSF / ExpressLRS @ 420k Baud)
#define USE_UART1
#define UART1_RX_PIN            PA17
#define UART1_TX_PIN            PA16
#define SERIALRX_UART           SERIAL_PORT_USART1

// Motor PWM Actuator Outputs (Timer 1 Non-Blocking Deadline Scheduler)
#define USE_PWM_OUTPUT
#define USE_MOTOR
#define DEFAULT_MOTOR_PROTOCOL  MOTOR_PROTOCOL_PWM
#define DEFAULT_MOTOR_PWM_RATE  400
#define MOTOR1_PIN              PA1
#define MOTOR2_PIN              PA2
#define MOTOR3_PIN              PA3
#define MOTOR4_PIN              PA4

// SPI0 & IMU Sensor Bus Mappings (Header J1)
#define SPI1_SCK_PIN            PA5
#define SPI1_SDI_PIN            PA6     // MISO
#define SPI1_SDO_PIN            PA7     // MOSI
#define GYRO_1_CS_PIN           PA8
#define GYRO_1_EXTI_PIN         PA0
#define GYRO_1_SPI_INSTANCE     SPI1
#define GYRO_1_ALIGN            CW0_DEG
#define ACC_1_ALIGN             CW0_DEG

// Serial RX Protocol Selection
#define USE_SERIALRX
#define USE_SERIALRX_CRSF
#define SERIALRX_PROVIDER       SERIALRX_CRSF

// Enforce target configuration static assertion guards
#include "config.h"
