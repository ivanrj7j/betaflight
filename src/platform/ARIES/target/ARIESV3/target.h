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
#undef USE_FLASHFS
#undef USE_RX_PWM
#undef USE_RX_PPM
#undef USE_LED_STRIP

// Enable core flight features
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

// Flash Config & EEPROM Emulation (32 KB reserved sector 0x201F8000 - 0x201FFFFF)
#define CONFIG_IN_FLASH
#define FLASH_SECTOR_SIZE               4096
#define FLASH_PAGE_SIZE                 4096
#define FLASH_CONFIG_STREAMER_BUFFER_SIZE 32
#define FLASH_CONFIG_BUFFER_TYPE        uint32_t
#define EEPROM_SIZE                     8192
#define ENABLE_CONFIG_FLASH_INIT

// Stubs for pin assignments
// Status LED LD4 on GPIO 22 (Header pin Bank 1 pin 6)
#define LED0_PIN                PA22
#define BEEPER_PIN              NONE

#define USE_UART0
#define UART0_RX_PIN            NONE
#define UART0_TX_PIN            NONE

#define USE_UART1
#define UART1_RX_PIN            NONE
#define UART1_TX_PIN            NONE
