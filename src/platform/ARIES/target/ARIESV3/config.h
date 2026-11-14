/*
 * Betaflight board configuration for C-DAC ARIES v3.0 (THEJAS32 / VEGA ET1031).
 * Defines hardware sensors and features before common_pre.h to prune unused drivers.
 */

#pragma once

#define FC_TARGET_MCU           THEJAS32
#define BOARD_NAME              ARIESV3
#define MANUFACTURER_ID         ARV3

#define USE_GYRO
#define USE_GYRO_SPI_ICM42688P
#define USE_GYRO_SPI_BMI270

#define USE_ACC
#define USE_ACC_SPI_ICM42688P
#define USE_ACC_SPI_BMI270
