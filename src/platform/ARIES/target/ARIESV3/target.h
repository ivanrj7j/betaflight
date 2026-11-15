/*
 * This file is part of Betaflight.
 * Target configuration for C-DAC ARIES v3.0 (THEJAS32 / VEGA ET1031).
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "common/utils.h"

#define TARGET_BOARD_IDENTIFIER "ARV3"
#define USBD_PRODUCT_STRING     "ARIES v3.0"

#define SYSTEM_HSE_MHZ          100
#define DEFAULT_CPU_OVERCLOCK   0

// DMA attributes
#define DMA_RAM
#define DMA_RW_AXI
#define DMA_RAM_R
#define DMA_RAM_W
#define DMA_RAM_RW
#define DMA_DATA_ZERO_INIT
#define DMA_DATA
#define STATIC_DMA_DATA_AUTO    static

// Disable hardware features not present in dummy build
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
#undef USE_FLASH_M25P16
#undef USE_FLASH_FM25V02A
#undef USE_FLASH_W25M
#undef USE_FLASH_W25N
#undef USE_FLASH_W25Q128FV
#undef USE_FLASH_PY25Q128HA
#undef USE_FLASH_W25N01G
#undef USE_FLASH_W25N02K
#undef USE_FLASH_W25M512
#undef USE_FLASH_W25M02G
#undef USE_FLASH_MX66UW1G45G
#undef USE_FLASH_TOOLS
#undef USE_FLASHFS
#undef USE_RX_PWM
#undef USE_RX_PPM
#undef USE_LED_STRIP

// Prune memory-heavy subsystems to fit inside 248 KB internal SRAM
#undef USE_OSD
#undef USE_OSD_SD
#undef USE_OSD_HD
#undef USE_OSD_CUSTOM_TEXT
#undef ENABLE_OSD_CUSTOM_TEXT
#define ENABLE_OSD_CUSTOM_TEXT 0
#undef USE_BLACKBOX
#undef USE_CMS
#undef USE_VTX
#undef USE_VTX_COMMON
#undef USE_VTX_CONTROL
#undef USE_VTX_SMARTAUDIO
#undef USE_VTX_TRAMP
#undef USE_VTX_MSP
#undef USE_VTX_TABLE
#undef USE_SERVOS
#undef USE_GPS
#undef USE_GPS_PLUS_CODES
#undef USE_GPS_RESCUE
#undef USE_ALTITUDE_HOLD
#undef USE_POSITION_HOLD
#undef USE_TELEMETRY_FRSKY_HUB
#undef USE_TELEMETRY_SMARTPORT
#undef USE_TELEMETRY_GHST
#undef USE_TELEMETRY_SRXL
#undef USE_SERIALRX_GHST
#undef USE_SERIALRX_IBUS
#undef USE_SERIALRX_SBUS
#undef USE_SERIALRX_SPEKTRUM
#undef USE_SERIALRX_FPORT
#undef USE_SERIALRX_XBUS
#undef USE_SERIALRX_SRXL2
#undef USE_RX_SPI
#undef USE_RX_CC2500
#undef USE_RX_EXPRESSLRS
#undef USE_RX_SX1280
#undef USE_RX_SX127X

// Disable Mag & Baro (not needed for acro quad flight)
#undef USE_MAG
#undef USE_BARO
#undef USE_BARO_MS5611
#undef USE_BARO_SPI_MS5611
#undef USE_BARO_BMP280
#undef USE_BARO_SPI_BMP280
#undef USE_BARO_BMP388
#undef USE_BARO_SPI_LPS
#undef USE_BARO_QMP6988
#undef USE_BARO_DPS310
#undef USE_BARO_SPI_DPS310
#undef USE_BARO_2SMBP_02B
#undef USE_BARO_LPS22DF

// Disable extra gyros/accels not present on board
#undef USE_ACC_MPU6500
#undef USE_GYRO_MPU6500
#undef USE_ACC_SPI_MPU6000
#undef USE_GYRO_SPI_MPU6000
#undef USE_ACC_SPI_MPU6500
#undef USE_GYRO_SPI_MPU6500
#undef USE_ACC_SPI_ICM20689
#undef USE_GYRO_SPI_ICM20689
#undef USE_ACCGYRO_LSM6DSO
#undef USE_ACCGYRO_LSM6DSV16X
#undef USE_ACCGYRO_LSM6DSK320X
#undef USE_GYRO_SPI_ICM42605
#undef USE_ACCGYRO_ICM42622P
#undef USE_ACCGYRO_ICM42686P
#undef USE_ACCGYRO_ICM45686
#undef USE_ACCGYRO_ICM45605
#undef USE_ACCGYRO_ICM56686
#undef USE_ACCGYRO_IIM42652
#undef USE_ACCGYRO_IIM42653
#undef USE_ACC_SPI_ICM42605
#undef USE_ACCGYRO_ICM40609D

#undef USE_DASHBOARD
#undef USE_RC_STATS
#undef USE_PERSISTENT_STATS
#undef USE_RCDEVICE
#undef USE_ESCSERIAL
#undef USE_ESCSERIAL_SIMONK
#undef USE_MSP_DISPLAYPORT
#undef USE_SERIAL_4WAY_BLHELI_INTERFACE
#undef USE_SERIAL_4WAY_BLHELI_BOOTLOADER
#undef USE_SERIAL_4WAY_SK_BOOTLOADER

// Enable core flight features
#define TARGET_PREINIT
#define USE_SPI
#define USE_SPI_DEVICE_1
#define SPI1_SCK_PIN            PA5
#define SPI1_SDI_PIN            PA6
#define SPI1_SDO_PIN            PA7
#define SPI1_MISO_PIN           PA6
#define SPI1_MOSI_PIN           PA7
#define SPI1_NSS_PIN            PA8

#define USE_GYRO
#define USE_GYRO_SPI_ICM42688P
#define USE_GYRO_SPI_BMI270

#define USE_ACC
#define USE_ACC_SPI_ICM42688P
#define USE_ACC_SPI_BMI270

#define GYRO_1_CS_PIN           PA8
#define GYRO_1_SPI_INSTANCE     SPI1
#define GYRO_1_EXTI_PIN         PA0
#define GYRO_1_ALIGN            CW0_DEG
#define USE_CLI
#define USE_SERIAL_UART
#define FAST_MATH

// Flight loop rate clamping for 100 MHz RV32IM soft-float core:
// With an 8 kHz gyro (ICM42688P), a denominator of 8 clamps the PID loop to exactly 1.0 kHz (1000 µs),
// aligning with TASK_GYROPID_DESIRED_PERIOD (1000 µs) and preventing CPU task overruns.
#define DEFAULT_PID_PROCESS_DENOM 8

// Parameter Group / Configuration persistence in RAM for in-SRAM execution
#define CONFIG_IN_RAM
#define EEPROM_SIZE                     8192
#undef CONFIG_IN_FLASH
#undef ENABLE_CONFIG_FLASH_INIT

// Stubs for pin assignments
// Status LED LD4 on GPIO 22 (Header pin Bank 1 pin 6)
#define LED0_PIN                PA22
#define BEEPER_PIN              NONE

#define USE_UART0
#define UART0_RX_PIN            PA30
#define UART0_TX_PIN            PA31

#define MSP_UART                SERIAL_PORT_UART0

#define USE_UART1
#define UART1_RX_PIN            PA17 // Header J2_15 (Serial RX)
#define UART1_TX_PIN            PA16 // Header J2_13 (Telemetry TX)
#define SERIALRX_UART           SERIAL_PORT_USART1

// Actuator Outputs (Motor 1..4 on Header J2 & J1)
#define USE_PWM_OUTPUT
#define USE_MOTOR
#define DEFAULT_MOTOR_PROTOCOL  MOTOR_PROTOCOL_PWM
#define DEFAULT_MOTOR_PWM_RATE  400
#define MOTOR1_PIN              PA1  // Header J2_5 (Motor 1 - Rear Right)
#define MOTOR2_PIN              PA2  // Header J2_3 (Motor 2 - Front Right)
#define MOTOR3_PIN              PA3  // Header J2_1 (Motor 3 - Rear Left)
#define MOTOR4_PIN              PA4  // Header J1_10 (Motor 4 - Front Left)

// Serial Receiver (CRSF / ELRS on UART1)
#define USE_SERIALRX
#define USE_SERIALRX_CRSF
#define SERIALRX_PROVIDER       SERIALRX_CRSF
