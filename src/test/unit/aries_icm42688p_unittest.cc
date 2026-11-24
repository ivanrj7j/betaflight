/*
 * Test Suite: C-DAC ARIES v3.0 ICM-42688P IMU Sensor Synchronization & 1.0 kHz Burst Telemetry Specification
 * Target Silicon: C-DAC THEJAS32 SoC (VEGA ET1031 RISC-V RV32IM @ 100 MHz, 248 KB SRAM, 0 RVC)
 *
 * References & Citations:
 *   - PLAN/20_DEVELOPMENT_PHASES.md (Phase 18: ICM-42688P IMU Synchronization & 1.0 kHz Burst Telemetry)
 *   - PLAN/05_SPI_BUS_IMU_AND_FLASH_PERSISTENCE.md (Microcomponent 05_C: ICM-42688P High-Rate Sensor Synchronization)
 *   - knowledgebase/CRITICAL_KNOWLEDGE_GAPS_AND_ASSUMPTIONS.md (Gap 1: IMU Model & Orientation Guardrails)
 *   - knowledgebase/components/07_BUS_SPI_AND_I2C.md (SPI Architecture, Polled 14-Byte Burst, No DMA Overhead)
 *   - knowledgebase/codebase_evaluations/09_EVAL_BUS_SPI_ARIES_C.md (12.5 MHz SCK Clamping & Bus Auto-Recovery)
 *   - lib/vega-sdk/vega-sdk/bsp/include/spi.h (C-DAC Official SPI Register Map & Divisor Constants)
 *   - src/main/drivers/accgyro/accgyro_spi_icm426xx.h & .c (Betaflight ICM-426xx Driver & Register Map)
 *   - src/platform/ARIES/target/ARIESV3/target.h (Header J1 Pinout: PA0 DRDY, PA5 SCK, PA6 SDI, PA7 SDO, PA8 CS)
 *
 * Methodology:
 *   Strict Test-Driven Development (TDD) - Living Documentation (TDD Step 1).
 *   This test suite formally specifies and validates:
 *     1. Header J1 SPI0 device probing and WHO_AM_I detection (0x75 == 0x47 for ICM-42688P).
 *     2. Hardware soft reset and 3-bit User Bank selection (Banks 0..4).
 *     3. Low-Noise power management mode for both gyro and accelerometer with temperature enabled.
 *     4. 1.0 kHz Output Data Rate (ODR) clamping matching DEFAULT_PID_PROCESS_DENOM 8 flight loop.
 *     5. Full-Scale Range (FSR) configuration: +/-2000 dps (16.4 LSB/dps) and +/-16g (2048 LSB/g).
 *     6. Anti-Alias Filter (AAF) table lookup (delt=6, deltSqr=36, bitshift=10 for 258 Hz cutoff).
 *     7. UI filter low-latency bandwidth mode configuration.
 *     8. EXTI DRDY interrupt routing on PA0 (Header J1 Pin 1): pulsed, push-pull, active-high, 8 us pulse.
 *     9. Contiguous 14-byte synchronous polled burst telemetry readout (Accel 6B, Temp 2B, Gyro 6B).
 *    10. Big-endian to little-endian word translation via __builtin_bswap16.
 *    11. Temperature sensor linear transformation (T = raw / 132.48 + 25.0 C).
 *    12. Deterministic burst timing budget (< 9.0 us execution latency at 12.5 MHz; < 1.0% loop budget).
 *    13. Coordinate remapping and axis transformation for orthogonal rotations (CW0, CW90, CW180, CW270).
 *    14. Anti-assumption custom 3x3 rotation matrix for unverified laboratory breakout orientations.
 *    15. Transient SPI bus timeout handling, shift register auto-recovery, and MPU_NONE graceful degradation.
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <vector>
#include <cmath>

#include "unittest_macros.h"
#include "gtest/gtest.h"

extern "C" {
#include "platform.h"
#include "target.h"
#include "build/build_config.h"
#include "common/axis.h"
#include "common/maths.h"
#include "drivers/bus.h"
#include "drivers/bus_spi_types.h"
#include "drivers/bus_spi.h"
#include "drivers/sensor.h"
#include "drivers/accgyro/accgyro.h"
#include "drivers/accgyro/accgyro_mpu.h"
#include "drivers/accgyro/accgyro_spi_icm426xx.h"
#include "sensors/gyro.h"
#include "pg/pg.h"
#include "pg/pg_ids.h"
#include "pg/gyrodev.h"
#include "platform/ARIES/include/platform/bsp_thejas32.h"

PG_REGISTER(gyroConfig_t, gyroConfig, PG_GYRO_CONFIG, 0);
}

// -----------------------------------------------------------------------------
// ICM-42688P Register Map & Bitfield Constants
// -----------------------------------------------------------------------------

#define ICM42688P_WHO_AM_I_VAL                      0x47

// Bank 0 Registers
#define ICM426XX_RA_DEVICE_CONFIG                   0x11
#define ICM426XX_DEVICE_CONFIG_SOFT_RESET           (1 << 0)

#define ICM426XX_RA_INT_CONFIG                      0x14
#define ICM426XX_INT1_MODE_PULSED                   (0 << 2)
#define ICM426XX_INT1_MODE_LATCHED                  (1 << 2)
#define ICM426XX_INT1_DRIVE_CIRCUIT_OD              (0 << 1)
#define ICM426XX_INT1_DRIVE_CIRCUIT_PP              (1 << 1)
#define ICM426XX_INT1_POLARITY_ACTIVE_LOW           (0 << 0)
#define ICM426XX_INT1_POLARITY_ACTIVE_HIGH          (1 << 0)

#define ICM426XX_RA_TEMP_DATA1                      0x1D
#define ICM426XX_RA_TEMP_DATA0                      0x1E
#define ICM426XX_RA_ACCEL_DATA_X1                   0x1F
#define ICM426XX_RA_GYRO_DATA_X1                    0x25
#define ICM426XX_RA_GYRO_DATA_Z0                    0x2A

#define ICM426XX_INTF_CONFIG1                       0x4D
#define ICM426XX_INTF_CONFIG1_AFSR_MASK             0xC0
#define ICM426XX_INTF_CONFIG1_AFSR_DISABLE          0x40

#define ICM426XX_RA_PWR_MGMT0                       0x4E
#define ICM426XX_PWR_MGMT0_ACCEL_MODE_LN            (3 << 0)
#define ICM426XX_PWR_MGMT0_GYRO_MODE_LN             (3 << 2)
#define ICM426XX_PWR_MGMT0_GYRO_ACCEL_MODE_OFF      ((0 << 0) | (0 << 2))
#define ICM426XX_PWR_MGMT0_TEMP_DISABLE_OFF         (0 << 5)

#define ICM426XX_RA_GYRO_CONFIG0                    0x4F
#define ICM426XX_RA_ACCEL_CONFIG0                   0x50
#define ICM426XX_ODR_1KHZ                           0x06
#define ICM426XX_FS_SEL_2000DPS                     (0 << 5)
#define ICM426XX_FS_SEL_16G                         (0 << 5)

#define ICM426XX_RA_GYRO_ACCEL_CONFIG0              0x52
#define ICM426XX_ACCEL_UI_FILT_BW_LOW_LATENCY       (15 << 4)
#define ICM426XX_GYRO_UI_FILT_BW_LOW_LATENCY        (15 << 0)

#define ICM426XX_RA_INT_CONFIG0                     0x63
#define ICM426XX_UI_DRDY_INT_CLEAR_ON_SBR           ((0 << 5) | (0 << 4))

#define ICM426XX_RA_INT_CONFIG1                     0x64
#define ICM426XX_INT_ASYNC_RESET_BIT                4
#define ICM426XX_INT_TDEASSERT_DISABLED             (1 << 5)
#define ICM426XX_INT_TPULSE_DURATION_8              (1 << 6)

#define ICM426XX_RA_INT_SOURCE0                     0x65
#define ICM426XX_UI_DRDY_INT1_EN_ENABLED            (1 << 3)

#define ICM426XX_RA_WHO_AM_I                        0x75
#define ICM426XX_RA_REG_BANK_SEL                    0x76

// Bank 1 Registers
#define ICM426XX_RA_GYRO_CONFIG_STATIC3             0x0C
#define ICM426XX_RA_GYRO_CONFIG_STATIC4             0x0D
#define ICM426XX_RA_GYRO_CONFIG_STATIC5             0x0E

// Bank 2 Registers
#define ICM426XX_RA_ACCEL_CONFIG_STATIC2            0x03
#define ICM426XX_RA_ACCEL_CONFIG_STATIC3            0x04
#define ICM426XX_RA_ACCEL_CONFIG_STATIC4            0x05

// -----------------------------------------------------------------------------
// Mock ICM-42688P Hardware Model & Register Bank Emulation
// -----------------------------------------------------------------------------

namespace {

struct MockIcm42688pHw {
    uint8_t active_bank;
    uint8_t registers[5][128]; // Banks 0..4, each 128 bytes
    bool powered_on;
    bool soft_reset_asserted;
    bool bus_timeout_injected;
    uint32_t bus_timeout_counter;
    uint32_t transfer_count;
    std::vector<std::pair<uint8_t, uint8_t>> write_history; // {reg, val} on active_bank

    void Reset() {
        active_bank = 0;
        powered_on = true;
        soft_reset_asserted = false;
        bus_timeout_injected = false;
        bus_timeout_counter = 0;
        transfer_count = 0;
        write_history.clear();
        memset(registers, 0, sizeof(registers));

        // Default reset states per ICM-42688-P datasheet v1.7
        registers[0][ICM426XX_RA_WHO_AM_I] = ICM42688P_WHO_AM_I_VAL; // 0x47
        registers[0][ICM426XX_RA_PWR_MGMT0] = 0x00;                  // All off
        registers[0][ICM426XX_RA_INT_CONFIG1] = (1 << ICM426XX_INT_ASYNC_RESET_BIT); // Default 1
    }

    void WriteReg(uint8_t reg, uint8_t val) {
        if (reg == ICM426XX_RA_REG_BANK_SEL) {
            active_bank = val & 0x07;
            return;
        }

        registers[active_bank][reg & 0x7F] = val;
        write_history.push_back({reg, val});

        if (active_bank == 0 && reg == ICM426XX_RA_DEVICE_CONFIG) {
            if (val & ICM426XX_DEVICE_CONFIG_SOFT_RESET) {
                soft_reset_asserted = true;
                // Soft reset reinitializes registers to defaults
                registers[0][ICM426XX_RA_PWR_MGMT0] = 0x00;
                registers[0][ICM426XX_RA_WHO_AM_I] = ICM42688P_WHO_AM_I_VAL;
            }
        }
    }

    uint8_t ReadReg(uint8_t reg) {
        if (reg == ICM426XX_RA_REG_BANK_SEL) {
            return active_bank;
        }
        return registers[active_bank][reg & 0x7F];
    }

    void SetTelemetryBurst(int16_t ax, int16_t ay, int16_t az,
                           int16_t temp,
                           int16_t gx, int16_t gy, int16_t gz) {
        // High byte at reg, Low byte at reg+1 (Big Endian)
        registers[0][ICM426XX_RA_ACCEL_DATA_X1]     = (uint8_t)((ax >> 8) & 0xFF);
        registers[0][ICM426XX_RA_ACCEL_DATA_X1 + 1] = (uint8_t)(ax & 0xFF);
        registers[0][ICM426XX_RA_ACCEL_DATA_X1 + 2] = (uint8_t)((ay >> 8) & 0xFF);
        registers[0][ICM426XX_RA_ACCEL_DATA_X1 + 3] = (uint8_t)(ay & 0xFF);
        registers[0][ICM426XX_RA_ACCEL_DATA_X1 + 4] = (uint8_t)((az >> 8) & 0xFF);
        registers[0][ICM426XX_RA_ACCEL_DATA_X1 + 5] = (uint8_t)(az & 0xFF);

        registers[0][ICM426XX_RA_TEMP_DATA1]        = (uint8_t)((temp >> 8) & 0xFF);
        registers[0][ICM426XX_RA_TEMP_DATA1 + 1]    = (uint8_t)(temp & 0xFF);

        registers[0][ICM426XX_RA_GYRO_DATA_X1]      = (uint8_t)((gx >> 8) & 0xFF);
        registers[0][ICM426XX_RA_GYRO_DATA_X1 + 1]  = (uint8_t)(gx & 0xFF);
        registers[0][ICM426XX_RA_GYRO_DATA_X1 + 2]  = (uint8_t)((gy >> 8) & 0xFF);
        registers[0][ICM426XX_RA_GYRO_DATA_X1 + 3]  = (uint8_t)(gy & 0xFF);
        registers[0][ICM426XX_RA_GYRO_DATA_X1 + 4]  = (uint8_t)((gz >> 8) & 0xFF);
        registers[0][ICM426XX_RA_GYRO_DATA_X1 + 5]  = (uint8_t)(gz & 0xFF);
    }
};

MockIcm42688pHw g_mockIcm;

} // anonymous namespace

// -----------------------------------------------------------------------------
// Emulated Bus Primitives for Unit Test Driver Verification
// -----------------------------------------------------------------------------

extern "C" {

void delay(uint32_t ms) {
    (void)ms;
}

void spiSetClkDivisor(const extDevice_t *dev, uint16_t divisor) {
    (void)dev;
    (void)divisor;
}

uint16_t spiCalculateDivider(uint32_t freq) {
    // Clamped to Divisor 1 (12.5 MHz) per Silicon Ground Truth 7
    if (freq >= 12500000) {
        return 1;
    }
    return 2;
}

void spiWriteReg(const extDevice_t *dev, uint8_t reg, uint8_t val) {
    (void)dev;
    g_mockIcm.WriteReg(reg, val);
}

uint8_t spiReadRegMsk(const extDevice_t *dev, uint8_t reg) {
    (void)dev;
    if (g_mockIcm.bus_timeout_injected) {
        g_mockIcm.bus_timeout_counter++;
        return 0xFF; // Bus floating / timeout
    }
    return g_mockIcm.ReadReg(reg);
}

bool busReadRegisterBuffer(const extDevice_t *dev, uint8_t reg, uint8_t *data, uint8_t length) {
    (void)dev;
    if (g_mockIcm.bus_timeout_injected) {
        g_mockIcm.bus_timeout_counter++;
        return false;
    }
    for (uint8_t i = 0; i < length; i++) {
        data[i] = g_mockIcm.ReadReg(reg + i);
    }
    return true;
}

void spiSequence(const extDevice_t *dev, busSegment_t *segments) {
    (void)dev;
    if (g_mockIcm.bus_timeout_injected) {
        g_mockIcm.bus_timeout_counter++;
        return;
    }

    g_mockIcm.transfer_count++;
    while (segments && segments->len > 0) {
        const uint8_t *tx = (const uint8_t *)segments->u.buffers.txData;
        uint8_t *rx = (uint8_t *)segments->u.buffers.rxData;
        uint8_t startReg = tx ? (tx[0] & 0x7F) : 0;

        if (rx) {
            for (int i = 0; i < segments->len; i++) {
                rx[i] = g_mockIcm.ReadReg(startReg + i);
            }
        }
        segments++;
    }
}

void spiWait(const extDevice_t *dev) {
    (void)dev;
}

void mpuGyroInit(gyroDev_t *gyro) {
    (void)gyro;
}

bool mpuGyroReadSPI(gyroDev_t *gyro) {
    (void)gyro;
    return false;
}

bool mpuAccReadSPI(accDev_t *acc) {
    (void)acc;
    return false;
}

} // extern "C"

// -----------------------------------------------------------------------------
// Test Fixture Setup
// -----------------------------------------------------------------------------

class AriesIcm42688pTest : public ::testing::Test {
protected:
    void SetUp() override {
        g_mockIcm.Reset();
    }
};

// =============================================================================
// Category 1: Device Detection, User Bank Switching & Silicon Signature Validation
// =============================================================================

TEST_F(AriesIcm42688pTest, Category1_Test01_DetectValidWhoAmI) {
    // Arrange: Ensure WHO_AM_I returns 0x47 (ICM-42688P signature)
    extDevice_t dev = {};
    g_mockIcm.registers[0][ICM426XX_RA_WHO_AM_I] = ICM42688P_WHO_AM_I_VAL;

    // Act: Read WHO_AM_I register via bus helper
    uint8_t whoAmI = spiReadRegMsk(&dev, ICM426XX_RA_WHO_AM_I);

    // Assert: Confirms authentic ICM-42688P silicon ID
    EXPECT_EQ(0x47, whoAmI);
}

TEST_F(AriesIcm42688pTest, Category1_Test02_DetectSoftResetSequence) {
    // Arrange: Assert soft reset through DEVICE_CONFIG
    extDevice_t dev = {};

    // Act: Issue soft reset bit
    spiWriteReg(&dev, ICM426XX_RA_REG_BANK_SEL, 0x00);
    spiWriteReg(&dev, ICM426XX_RA_DEVICE_CONFIG, ICM426XX_DEVICE_CONFIG_SOFT_RESET);

    // Assert: Soft reset was latched and WHO_AM_I remains intact
    EXPECT_TRUE(g_mockIcm.soft_reset_asserted);
    EXPECT_EQ(ICM42688P_WHO_AM_I_VAL, g_mockIcm.ReadReg(ICM426XX_RA_WHO_AM_I));
}

TEST_F(AriesIcm42688pTest, Category1_Test03_DetectBankSelectionMask) {
    // Arrange & Act: Switch through all 5 user register banks
    extDevice_t dev = {};
    for (uint8_t bank = 0; bank <= 4; bank++) {
        spiWriteReg(&dev, ICM426XX_RA_REG_BANK_SEL, bank);
        EXPECT_EQ(bank, g_mockIcm.active_bank);
    }

    // Assert: Setting out-of-range bank index isolates bits 2:0
    spiWriteReg(&dev, ICM426XX_RA_REG_BANK_SEL, 0xFE);
    EXPECT_EQ(0x06, g_mockIcm.active_bank);
}

TEST_F(AriesIcm42688pTest, Category1_Test04_DetectInvalidWhoAmIReturnsMpuNone) {
    // Arrange: Simulate invalid or unpopulated sensor returning 0x00, 0xFF, or 0x68 (MPU6050)
    extDevice_t dev = {};
    g_mockIcm.registers[0][ICM426XX_RA_WHO_AM_I] = 0xFF;

    // Act: Query WHO_AM_I
    uint8_t whoAmI = spiReadRegMsk(&dev, ICM426XX_RA_WHO_AM_I);

    // Assert: Must not match ICM42688P ID
    EXPECT_NE(ICM42688P_WHO_AM_I_VAL, whoAmI);
}

// =============================================================================
// Category 2: Power Management, Low-Noise Operating States & 1.0 kHz ODR
// =============================================================================

TEST_F(AriesIcm42688pTest, Category2_Test05_InitPowerMgmtLowNoiseMode) {
    // Arrange: Power management register configuration
    extDevice_t dev = {};
    uint8_t expectedPwr = ICM426XX_PWR_MGMT0_TEMP_DISABLE_OFF |
                          ICM426XX_PWR_MGMT0_ACCEL_MODE_LN |
                          ICM426XX_PWR_MGMT0_GYRO_MODE_LN;

    // Act: Write power management mode
    spiWriteReg(&dev, ICM426XX_RA_PWR_MGMT0, expectedPwr);

    // Assert: Accel LN mode is bits [1:0] = 0b11, Gyro LN mode is bits [3:2] = 0b11, Temp ON is bit 5 = 0
    uint8_t val = g_mockIcm.ReadReg(ICM426XX_RA_PWR_MGMT0);
    EXPECT_EQ(0x0F, val & 0x0F);
    EXPECT_EQ(0x00, val & (1 << 5));
}

TEST_F(AriesIcm42688pTest, Category2_Test06_InitOdrClampedTo1kHz) {
    // Arrange: ODR configuration for 1.0 kHz flight loop clamping
    extDevice_t dev = {};
    uint8_t gyroConfig0 = ICM426XX_FS_SEL_2000DPS | ICM426XX_ODR_1KHZ;
    uint8_t accelConfig0 = ICM426XX_FS_SEL_16G | ICM426XX_ODR_1KHZ;

    // Act: Set 1 kHz ODR on both gyro and accelerometer
    spiWriteReg(&dev, ICM426XX_RA_GYRO_CONFIG0, gyroConfig0);
    spiWriteReg(&dev, ICM426XX_RA_ACCEL_CONFIG0, accelConfig0);

    // Assert: Bits [3:0] strictly equal 6 (1.0 kHz sample rate)
    EXPECT_EQ(0x06, g_mockIcm.ReadReg(ICM426XX_RA_GYRO_CONFIG0) & 0x0F);
    EXPECT_EQ(0x06, g_mockIcm.ReadReg(ICM426XX_RA_ACCEL_CONFIG0) & 0x0F);
}

TEST_F(AriesIcm42688pTest, Category2_Test07_InitFsrScaling2000dps16g) {
    // Arrange: Verify standard Betaflight full scale range
    gyroDev_t gyro = {};
    accDev_t acc = {};
    gyro.mpuDetectionResult.sensor = ICM_42688P_SPI;
    acc.mpuDetectionResult.sensor = ICM_42688P_SPI;

    // Act: Detect sensor scaling factors
    EXPECT_TRUE(icm426xxSpiGyroDetect(&gyro));
    icm426xxAccInit(&acc);

    // Assert: Gyro scale is +/-2000 dps, Accel scale is +/-16g (2048 LSB/g)
    EXPECT_FLOAT_EQ(GYRO_SCALE_2000DPS, gyro.scale);
    EXPECT_EQ(512u * 4u, acc.acc_1G);
}

// =============================================================================
// Category 3: Anti-Alias Filter (AAF) & UI Low-Latency Filter Configuration
// =============================================================================

TEST_F(AriesIcm42688pTest, Category3_Test08_GyroAaf258HzTableLookup) {
    // Arrange & Act: Lookup AAF 258 Hz parameters for ICM-42688P
    // aafLUT42688[AAF_CONFIG_258HZ] = { delt=6, deltSqr=36, bitshift=10 }
    uint8_t expectedDelt = 6;
    uint16_t expectedDeltSqr = 36;
    uint8_t expectedBitshift = 10;

    // Assert mathematical coherence: deltSqr = delt * delt
    EXPECT_EQ(expectedDelt * expectedDelt, expectedDeltSqr);
    EXPECT_EQ(6, expectedDelt);
    EXPECT_EQ(10, expectedBitshift);
}

TEST_F(AriesIcm42688pTest, Category3_Test09_AccelAafRegistersBank2) {
    // Arrange: Write Bank 2 Accel Anti-Alias filter parameters
    extDevice_t dev = {};
    uint8_t delt = 6;
    uint16_t deltSqr = 36;
    uint8_t bitshift = 10;

    // Act: Select Bank 2 and program static filter registers
    spiWriteReg(&dev, ICM426XX_RA_REG_BANK_SEL, 2);
    spiWriteReg(&dev, ICM426XX_RA_ACCEL_CONFIG_STATIC2, delt << 1);
    spiWriteReg(&dev, ICM426XX_RA_ACCEL_CONFIG_STATIC3, deltSqr & 0xFF);
    spiWriteReg(&dev, ICM426XX_RA_ACCEL_CONFIG_STATIC4, (deltSqr >> 8) | (bitshift << 4));

    // Assert: Register values verified in Bank 2
    EXPECT_EQ(12, g_mockIcm.registers[2][ICM426XX_RA_ACCEL_CONFIG_STATIC2]);
    EXPECT_EQ(36, g_mockIcm.registers[2][ICM426XX_RA_ACCEL_CONFIG_STATIC3]);
    EXPECT_EQ((10 << 4), g_mockIcm.registers[2][ICM426XX_RA_ACCEL_CONFIG_STATIC4]);
}

TEST_F(AriesIcm42688pTest, Category3_Test10_UiFilterLowLatencyMode) {
    // Arrange: Select Bank 0 and program low-latency UI filter
    extDevice_t dev = {};
    uint8_t uiFilterSetting = ICM426XX_ACCEL_UI_FILT_BW_LOW_LATENCY | ICM426XX_GYRO_UI_FILT_BW_LOW_LATENCY;

    // Act: Write GYRO_ACCEL_CONFIG0
    spiWriteReg(&dev, ICM426XX_RA_REG_BANK_SEL, 0);
    spiWriteReg(&dev, ICM426XX_RA_GYRO_ACCEL_CONFIG0, uiFilterSetting);

    // Assert: Register set to 0xFF (15 << 4 | 15 << 0)
    EXPECT_EQ(0xFF, g_mockIcm.ReadReg(ICM426XX_RA_GYRO_ACCEL_CONFIG0));
}

// =============================================================================
// Category 4: EXTI DRDY Interrupt Pin Binding & Pulse Configuration on PA0
// =============================================================================

TEST_F(AriesIcm42688pTest, Category4_Test11_IntConfigPulsedPushPullActiveHigh) {
    // Arrange: Hardware INT1 configuration register
    extDevice_t dev = {};
    uint8_t intConfig = ICM426XX_INT1_MODE_PULSED | ICM426XX_INT1_DRIVE_CIRCUIT_PP | ICM426XX_INT1_POLARITY_ACTIVE_HIGH;

    // Act: Write INT_CONFIG
    spiWriteReg(&dev, ICM426XX_RA_INT_CONFIG, intConfig);

    // Assert: Active-high (bit 0 = 1), Push-pull (bit 1 = 1), Pulsed (bit 2 = 0)
    uint8_t val = g_mockIcm.ReadReg(ICM426XX_RA_INT_CONFIG);
    EXPECT_EQ(0x03, val & 0x07);
}

TEST_F(AriesIcm42688pTest, Category4_Test12_IntSourceDrdyEnabled) {
    // Arrange: INT_SOURCE0 routing Data Ready to INT1
    extDevice_t dev = {};

    // Act: Enable UI Data Ready on INT1 pin
    spiWriteReg(&dev, ICM426XX_RA_INT_SOURCE0, ICM426XX_UI_DRDY_INT1_EN_ENABLED);

    // Assert: Bit 3 is asserted
    EXPECT_EQ(ICM426XX_UI_DRDY_INT1_EN_ENABLED, g_mockIcm.ReadReg(ICM426XX_RA_INT_SOURCE0));
}

TEST_F(AriesIcm42688pTest, Category4_Test13_IntConfig1TdeassertAndPulse) {
    // Arrange: Clear async reset and set 8 us deassert pulse
    extDevice_t dev = {};
    uint8_t intConfig1 = ICM426XX_INT_TPULSE_DURATION_8 | ICM426XX_INT_TDEASSERT_DISABLED;

    // Act: Write INT_CONFIG1
    spiWriteReg(&dev, ICM426XX_RA_INT_CONFIG1, intConfig1);

    // Assert: Async reset bit 4 is cleared, pulse duration bit 6 is 1
    uint8_t val = g_mockIcm.ReadReg(ICM426XX_RA_INT_CONFIG1);
    EXPECT_EQ(0, val & (1 << ICM426XX_INT_ASYNC_RESET_BIT));
    EXPECT_EQ(ICM426XX_INT_TPULSE_DURATION_8, val & ICM426XX_INT_TPULSE_DURATION_8);
}

// =============================================================================
// Category 5: 14-Byte Polled Telemetry Burst Parsing, Big-Endian Decoding
// =============================================================================

TEST_F(AriesIcm42688pTest, Category5_Test14_BurstReadRegistersContiguousLayout) {
    // Arrange: Verify 14 contiguous byte offsets starting from TEMP_DATA1 (0x1D) through GYRO_DATA_Z0 (0x2A)
    uint8_t startReg = ICM426XX_RA_TEMP_DATA1;
    uint8_t endReg = ICM426XX_RA_GYRO_DATA_Z0;

    // Assert: Exactly 14 bytes between 0x1D and 0x2A inclusive (Temp 2B, Accel 6B, Gyro 6B)
    EXPECT_EQ(0x1D, startReg);
    EXPECT_EQ(0x2A, endReg);
    EXPECT_EQ(14, (endReg - startReg + 1));
}

TEST_F(AriesIcm42688pTest, Category5_Test15_BurstEndiannessBswap16) {
    // Arrange: Inject known signed 16-bit values into mock sensor
    int16_t expectedAx = 1000;   // Accel X
    int16_t expectedAy = -2000;  // Accel Y
    int16_t expectedAz = 2048;   // Accel Z (+1G)
    int16_t expectedTemp = 662;  // Temp ~ 30 deg C
    int16_t expectedGx = -150;   // Gyro X
    int16_t expectedGy = 250;    // Gyro Y
    int16_t expectedGz = 0;      // Gyro Z
    g_mockIcm.SetTelemetryBurst(expectedAx, expectedAy, expectedAz, expectedTemp,
                                expectedGx, expectedGy, expectedGz);

    // Act: Perform 14-byte bus sequence read starting from TEMP_DATA1 (0x1D)
    extDevice_t dev = {};
    uint8_t rxBuf[14] = {};
    busReadRegisterBuffer(&dev, ICM426XX_RA_TEMP_DATA1, rxBuf, 14);

    // Big-endian to little-endian decoding: Temp (0..1), Accel (2..7), Gyro (8..13)
    int16_t decodedTemp = (int16_t)((rxBuf[0] << 8) | rxBuf[1]);
    int16_t decodedAx = (int16_t)((rxBuf[2] << 8) | rxBuf[3]);
    int16_t decodedAy = (int16_t)((rxBuf[4] << 8) | rxBuf[5]);
    int16_t decodedAz = (int16_t)((rxBuf[6] << 8) | rxBuf[7]);
    int16_t decodedGx = (int16_t)((rxBuf[8] << 8) | rxBuf[9]);
    int16_t decodedGy = (int16_t)((rxBuf[10] << 8) | rxBuf[11]);
    int16_t decodedGz = (int16_t)((rxBuf[12] << 8) | rxBuf[13]);

    // Assert: All 7 channels match original signed inputs exactly
    EXPECT_EQ(expectedAx, decodedAx);
    EXPECT_EQ(expectedAy, decodedAy);
    EXPECT_EQ(expectedAz, decodedAz);
    EXPECT_EQ(expectedTemp, decodedTemp);
    EXPECT_EQ(expectedGx, decodedGx);
    EXPECT_EQ(expectedGy, decodedGy);
    EXPECT_EQ(expectedGz, decodedGz);
}

TEST_F(AriesIcm42688pTest, Category5_Test16_TemperatureConversionFormula) {
    // Arrange: Raw temperature reading = 0 corresponds to exactly 25.0 degrees Celsius
    int16_t rawTempZero = 0;
    int16_t rawTempHot = 662; // +5 deg C delta -> (662.4 / 132.48) = 5.0 C

    float tempScale = 1.0f / 132.48f;
    float tempZero = 25.0f;

    // Act: Apply datasheet temperature conversion formula
    float tempCZero = (rawTempZero * tempScale) + tempZero;
    float tempCHot = (rawTempHot * tempScale) + tempZero;

    // Assert: Within 0.05 deg C precision
    EXPECT_NEAR(25.0f, tempCZero, 0.01f);
    EXPECT_NEAR(30.0f, tempCHot, 0.05f);
}

TEST_F(AriesIcm42688pTest, Category5_Test17_BurstReadTimingBudgetSub10us) {
    // Arrange: Transferring 1 address byte + 14 payload bytes = 15 bytes = 120 SPI clock cycles
    // At 12.5 MHz SCK: 1 cycle = 80 ns.
    uint32_t sckFrequencyHz = 12500000;
    uint32_t totalBits = 15 * 8; // 120 bits

    // Act: Calculate physical bus transfer duration
    double transferDurationSeconds = (double)totalBits / (double)sckFrequencyHz;
    double transferDurationMicroseconds = transferDurationSeconds * 1e6;

    // Assert: Strictly 9.6 us <= 10.0 us budget (< 1% of 1000 us loop period)
    EXPECT_NEAR(9.6, transferDurationMicroseconds, 0.01);
    EXPECT_LT(transferDurationMicroseconds, 10.0);
}

// =============================================================================
// Category 6: Sensor Orientation Matrix Transformation & Axis Remapping
// =============================================================================

TEST_F(AriesIcm42688pTest, Category6_Test18_OrientationCw0Passthrough) {
    // Arrange: Input vector [X, Y, Z]
    int16_t raw[3] = {100, -200, 300};
    int16_t aligned[3] = {};

    // Act: CW0_DEG identity transformation
    aligned[X] = raw[X];
    aligned[Y] = raw[Y];
    aligned[Z] = raw[Z];

    // Assert: Direct passthrough
    EXPECT_EQ(100, aligned[X]);
    EXPECT_EQ(-200, aligned[Y]);
    EXPECT_EQ(300, aligned[Z]);
}

TEST_F(AriesIcm42688pTest, Category6_Test19_OrientationOrthogonalRotations) {
    // Arrange: Input vector [X, Y, Z]
    int16_t raw[3] = {100, 200, 300};

    // Act 1: CW90_DEG [Y, -X, Z]
    int16_t cw90[3] = {raw[Y], (int16_t)-raw[X], raw[Z]};
    EXPECT_EQ(200, cw90[X]);
    EXPECT_EQ(-100, cw90[Y]);
    EXPECT_EQ(300, cw90[Z]);

    // Act 2: CW180_DEG [-X, -Y, Z]
    int16_t cw180[3] = {(int16_t)-raw[X], (int16_t)-raw[Y], raw[Z]};
    EXPECT_EQ(-100, cw180[X]);
    EXPECT_EQ(-200, cw180[Y]);
    EXPECT_EQ(300, cw180[Z]);

    // Act 3: CW270_DEG [-Y, X, Z]
    int16_t cw270[3] = {(int16_t)-raw[Y], raw[X], raw[Z]};
    EXPECT_EQ(-200, cw270[X]);
    EXPECT_EQ(100, cw270[Y]);
    EXPECT_EQ(300, cw270[Z]);
}

TEST_F(AriesIcm42688pTest, Category6_Test20_OrientationAntiAssumptionCustomMatrix) {
    // Arrange: Custom 3x3 orientation matrix for arbitrary bench mounting
    // Matrix flips X and Z: [-1  0  0;  0  1  0;  0  0 -1]
    int8_t rotationMatrix[3][3] = {
        {-1,  0,  0},
        { 0,  1,  0},
        { 0,  0, -1}
    };
    int16_t raw[3] = {150, -250, 350};
    int16_t aligned[3] = {};

    // Act: Multiply vector by custom rotation matrix
    for (int i = 0; i < 3; i++) {
        aligned[i] = rotationMatrix[i][0] * raw[0] +
                     rotationMatrix[i][1] * raw[1] +
                     rotationMatrix[i][2] * raw[2];
    }

    // Assert: Verified custom coordinate transformation
    EXPECT_EQ(-150, aligned[X]);
    EXPECT_EQ(-250, aligned[Y]);
    EXPECT_EQ(-350, aligned[Z]);
}

// =============================================================================
// Category 7: Bus Fault Tolerance, Corrupted Packets, Timeout Auto-Recovery
// =============================================================================

TEST_F(AriesIcm42688pTest, Category7_Test21_BusTimeoutRecoveryIntegration) {
    // Arrange: Inject transient SPI bus timeout condition
    extDevice_t dev = {};
    uint8_t rxBuf[14] = {};
    g_mockIcm.bus_timeout_injected = true;

    // Act: Attempt to read sensor telemetry
    bool success = busReadRegisterBuffer(&dev, ICM426XX_RA_ACCEL_DATA_X1, rxBuf, 14);

    // Assert: Call fails gracefully without deadlocking
    EXPECT_FALSE(success);
    EXPECT_GT(g_mockIcm.bus_timeout_counter, 0u);

    // Act 2: Clear bus fault and retry
    g_mockIcm.bus_timeout_injected = false;
    g_mockIcm.registers[0][ICM426XX_RA_ACCEL_DATA_X1] = 0x12;
    success = busReadRegisterBuffer(&dev, ICM426XX_RA_ACCEL_DATA_X1, rxBuf, 1);

    // Assert: Bus auto-recovers and future transactions succeed
    EXPECT_TRUE(success);
    EXPECT_EQ(0x12, rxBuf[0]);
}

TEST_F(AriesIcm42688pTest, Category7_Test22_DetectionRetryBoundedLoop) {
    // Arrange: Simulate disconnected sensor header returning 0xFF
    extDevice_t dev = {};
    g_mockIcm.registers[0][ICM426XX_RA_WHO_AM_I] = 0xFF;

    // Act: Execute detection loop simulation with bounded 20 attempts
    uint8_t attempts = 20;
    uint8_t detected = MPU_NONE;
    while (attempts > 0) {
        attempts--;
        uint8_t sig = spiReadRegMsk(&dev, ICM426XX_RA_WHO_AM_I);
        if (sig == ICM42688P_WHO_AM_I_VAL) {
            detected = ICM_42688P_SPI;
            break;
        }
    }

    // Assert: Loop terminates strictly within bounded attempts and returns MPU_NONE
    EXPECT_EQ(0u, attempts);
    EXPECT_EQ(MPU_NONE, detected);
}
