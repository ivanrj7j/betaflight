/*
 * This file is part of Betaflight.
 * Target configuration for C-DAC ARIES v3.0 (THEJAS32 / VEGA ET1031 RISC-V RV32IM @ 100 MHz).
 *
 * Citations:
 *   - knowledgebase/components/09_BUILD_LINKER_AND_MEMORY.md
 *   - knowledgebase/codebase_evaluations/14_EVAL_TARGET_H_AND_CONFIG.md
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

// Prune memory-heavy subsystems to guarantee <= 248 KB SRAM binary footprint
#undef USE_VCP
#undef USE_USB_MSC
#undef USE_SDCARD
#undef USE_MAX7456
#undef USE_CAMERA_CONTROL
#undef USE_PINIO
#undef USE_PINIOBOX
#undef USE_TRANSPONDER
#undef USE_DSHOT_BITBANG
#undef USE_DSHOT_TELEMETRY
#undef USE_RPM_FILTER
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
#undef USE_RX_PWM
#undef USE_RX_PPM
#undef USE_LED_STRIP
#undef USE_OSD
#undef USE_BLACKBOX
#undef USE_CMS
#undef USE_VTX
#undef USE_VTX_COMMON
#undef USE_VTX_CONTROL
#undef USE_SERVOS
#undef USE_GPS
#undef USE_GPS_PLUS_CODES
#undef USE_GPS_RESCUE
#undef USE_ALTITUDE_HOLD
#undef USE_POSITION_HOLD
#undef USE_TELEMETRY
#undef USE_SERIAL_PASSTHROUGH
#undef USE_BEEPER
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

// Actuator & Serial Pin Mappings
#define LED0_PIN                PA22
#define BEEPER_PIN              NONE

#define USE_UART0
#define UART0_RX_PIN            PA30
#define UART0_TX_PIN            PA31
#define MSP_UART                SERIAL_PORT_UART0

#define USE_UART1
#define UART1_RX_PIN            PA17
#define UART1_TX_PIN            PA16
#define SERIALRX_UART           SERIAL_PORT_USART1

#define USE_PWM_OUTPUT
#define USE_MOTOR
#define DEFAULT_MOTOR_PROTOCOL  MOTOR_PROTOCOL_PWM
#define DEFAULT_MOTOR_PWM_RATE  400
#define MOTOR1_PIN              PA1
#define MOTOR2_PIN              PA2
#define MOTOR3_PIN              PA3
#define MOTOR4_PIN              PA4

#define USE_SERIALRX
#define USE_SERIALRX_CRSF
#define SERIALRX_PROVIDER       SERIALRX_CRSF
