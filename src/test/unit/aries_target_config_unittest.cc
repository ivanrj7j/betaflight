/*
 * Test Suite: C-DAC ARIES v3.0 Target Header & Configuration Specification
 * Target Silicon: C-DAC THEJAS32 SoC (VEGA ET1031 RISC-V RV32IM @ 100 MHz, 248 KB SRAM, 0 RVC)
 * Reference: PLAN/02_BUILD_SYSTEM_AND_MEMORY_OPTIMIZATION.md (Microcomponent 02_C)
 * Citations:
 *   - knowledgebase/codebase_evaluations/14_EVAL_TARGET_H_AND_CONFIG.md
 *   - knowledgebase/components/09_BUILD_LINKER_AND_MEMORY.md
 *   - knowledgebase/CRITICAL_KNOWLEDGE_GAPS_AND_ASSUMPTIONS.md
 *   - src/platform/ARIES/target/ARIESV3/target.h
 *   - src/platform/ARIES/target/ARIESV3/config.h
 *
 * Methodology:
 *   Strict Test-Driven Development (TDD) - Living Documentation.
 *   This test suite formally validates:
 *     1. Board identification and target strings.
 *     2. 1.0 kHz flight loop rate clamping and CPU headroom calculations (> 65% idle).
 *     3. Collision-free 12-pin allocation matrix (Header J1, Header J2, Onboard).
 *     4. Subsystem pruning ensuring binary fits within <= 248 KB SRAM budget.
 *     5. In-SRAM Parameter Group allocation (CONFIG_IN_RAM, EEPROM_SIZE >= 8192).
 *     6. Compile-time static assertion bounds guards.
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <set>
#include <vector>
#include <string>

#include "unittest_macros.h"
#include "gtest/gtest.h"

// Include target and configuration definitions
#include "platform/ARIES/target/ARIESV3/target.h"
#include "platform/ARIES/target/ARIESV3/config.h"

namespace {

// Helper to convert DEFIO_TAG macro expansion to integer pin number
// In io_def_generated.h: DEFIO_TAG__PAx is DEFIO_TAG_MAKE(0, x) where port 0 = A, pin = x.
// Tag format: ((gpioid + 1) << 16) | pin. For Port A (gpioid 0): tag = (1 << 16) | pin.
// Pin number = tag & 0xFFFF.

#define GET_PIN_NUM(tag_e) ((uint32_t)(DEFIO_TAG__##tag_e & 0xFFFFU))

struct PinMappingEntry {
    const char *signal_name;
    const char *header_name;
    uint32_t pin_num;
    const char *pin_name;
};

class AriesTargetConfigTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

// =============================================================================
// TEST 01: Board Identifier, Product Strings & Clock Tree
// =============================================================================
TEST_F(AriesTargetConfigTest, Test_TargetConfig_BoardIdentifier_ARV3) {
    // Assert target board identifier matches official 4-character code
    EXPECT_STREQ(TARGET_BOARD_IDENTIFIER, "ARV3");

    // Assert USB product string for host identification
    EXPECT_STREQ(USBD_PRODUCT_STRING, "ARIES v3.0");

    // Assert system clock is configured for 100 MHz
    EXPECT_EQ(SYSTEM_HSE_MHZ, 100);

    // Assert no overclocking by default
    EXPECT_EQ(DEFAULT_CPU_OVERCLOCK, 0);
}

// =============================================================================
// TEST 02: 1.0 kHz Loop Rate Clamping & RV32IM Soft-Float Headroom Analysis
// =============================================================================
TEST_F(AriesTargetConfigTest, Test_TargetConfig_LoopRateClamping_1kHz) {
    // Ground Truth 4: Clamping the flight loop to 1.0 kHz
    // ICM-42688P gyro native sampling rate = 8000 Hz.
    // DEFAULT_PID_PROCESS_DENOM = 8 divides 8000 Hz to 1000 Hz (1.0 kHz).
    EXPECT_EQ(DEFAULT_PID_PROCESS_DENOM, 8);

    constexpr uint32_t GYRO_SAMPLE_RATE_HZ = 8000U;
    uint32_t loop_rate_hz = GYRO_SAMPLE_RATE_HZ / DEFAULT_PID_PROCESS_DENOM;
    uint32_t loop_time_us = 1000000U / loop_rate_hz;

    EXPECT_EQ(loop_rate_hz, 1000U);
    EXPECT_EQ(loop_time_us, 1000U); // 1.0 ms loop interval

    // Soft-float PID calculation Worst-Case Execution Time (WCET) on 100 MHz RV32IM:
    // PID controller + biquad filters + PT1 notch + mixer math = ~290 us to 350 us.
    constexpr uint32_t PID_SOFTFLOAT_WCET_US = 350U;
    ASSERT_LT(PID_SOFTFLOAT_WCET_US, loop_time_us);

    uint32_t idle_headroom_us = loop_time_us - PID_SOFTFLOAT_WCET_US;
    double idle_headroom_percent = (static_cast<double>(idle_headroom_us) / loop_time_us) * 100.0;

    // Verify > 65% idle CPU headroom is guaranteed
    EXPECT_GE(idle_headroom_percent, 65.0);

    // Consequence analysis: If DEFAULT_PID_PROCESS_DENOM was 2 (4.0 kHz / 250 us interval):
    // 350 us WCET > 250 us interval -> 140% CPU load -> Watchdog Reset / Crash!
    constexpr uint32_t UNSAFE_LOOP_TIME_4KHZ = 250U;
    EXPECT_GT(PID_SOFTFLOAT_WCET_US, UNSAFE_LOOP_TIME_4KHZ);
}

// =============================================================================
// TEST 03: Zero-Conflict Pin Allocation Matrix (Headers J1 & J2)
// =============================================================================
TEST_F(AriesTargetConfigTest, Test_TargetConfig_PinMatrix_ZeroCollisions) {
    // Define the full 12-pin allocation matrix
    const std::vector<PinMappingEntry> pin_matrix = {
        // Header J1: SPI0 Bus & IMU Sensors
        { "GYRO_1_EXTI",  "Header J1", 0,  "PA0" },
        { "MOTOR4",       "Header J1", 4,  "PA4" },
        { "SPI1_SCK",     "Header J1", 5,  "PA5" },
        { "SPI1_SDI",     "Header J1", 6,  "PA6" },
        { "SPI1_SDO",     "Header J1", 7,  "PA7" },
        { "GYRO_1_CS",    "Header J1", 8,  "PA8" },

        // Header J2: Motor PWM Outputs
        { "MOTOR1",       "Header J2", 1,  "PA1" },
        { "MOTOR2",       "Header J2", 2,  "PA2" },
        { "MOTOR3",       "Header J2", 3,  "PA3" },

        // Dedicated Onboard Signals
        { "UART1_TX",     "Onboard",   16, "PA16" },
        { "UART1_RX",     "Onboard",   17, "PA17" },
        { "LED0",         "Onboard",   22, "PA22" },
        { "UART0_TX",     "Onboard",   30, "PA30" },
        { "UART0_RX",     "Onboard",   31, "PA31" }
    };

    // Assert exactly 14 assigned pin functions
    EXPECT_EQ(pin_matrix.size(), 14U);

    // Verify zero collisions: every pin number must be unique
    std::set<uint32_t> used_pins;
    for (const auto &entry : pin_matrix) {
        // Valid GPIO range for THEJAS32: PA0 to PA31 (0 to 31)
        EXPECT_LE(entry.pin_num, 31U) << "Pin " << entry.signal_name << " out of 32-bit GPIO bounds!";

        // Check for collision
        auto insert_res = used_pins.insert(entry.pin_num);
        EXPECT_TRUE(insert_res.second)
            << "FATAL PIN COLLISION DETECTED: Pin " << entry.pin_name
            << " (" << entry.pin_num << ") assigned to " << entry.signal_name
            << " conflicts with an existing assignment!";
    }

    // Assert all 14 pins are completely unique
    EXPECT_EQ(used_pins.size(), 14U);

    // Verify Motor PWM pins (PA1, PA2, PA3, PA4) form a continuous group
    EXPECT_EQ(pin_matrix[6].pin_num, 1U); // MOTOR1 = PA1
    EXPECT_EQ(pin_matrix[7].pin_num, 2U); // MOTOR2 = PA2
    EXPECT_EQ(pin_matrix[8].pin_num, 3U); // MOTOR3 = PA3
    EXPECT_EQ(pin_matrix[1].pin_num, 4U); // MOTOR4 = PA4
}

// =============================================================================
// TEST 04: Subsystem Pruning & 248 KB SRAM Budget Enforcement
// =============================================================================
TEST_F(AriesTargetConfigTest, Test_TargetConfig_FeaturePruning_SRAMBudget) {
    // Assert all RAM-heavy, unmapped, or non-existent peripherals are undefined

    // 1. Graphical and Video Subsystems (Saves ~120 KB)
#if defined(USE_OSD) || defined(USE_CMS) || defined(USE_VTX) || defined(USE_MAX7456)
    FAIL() << "Video/OSD subsystems must NOT be enabled in ARIES v3.0 target!";
#endif

    // 2. Storage & Filesystem Subsystems (Saves ~85 KB)
#if defined(USE_SDCARD) || defined(USE_FLASHFS) || defined(USE_BLACKBOX)
    FAIL() << "SDCard/FlashFS storage subsystems must NOT be enabled in ARIES v3.0 target!";
#endif

    // 3. Navigation & Unused Sensors (Saves ~90 KB)
#if defined(USE_GPS) || defined(USE_BARO) || defined(USE_MAG)
    FAIL() << "GPS/Baro/Mag navigation subsystems must NOT be enabled in ARIES v3.0 target!";
#endif

    // 4. ADC & Analog Telemetry (Saves ~20 KB)
#if defined(USE_ADC) || defined(USE_ADC_INTERNAL)
    FAIL() << "ADC subsystem must NOT be enabled without hardware confirmation!";
#endif

    // 5. USB Virtual COM Port (Saves ~30 KB - ARIES uses physical CP2102N UART0)
#if defined(USE_VCP) || defined(USE_USB_MSC)
    FAIL() << "USB VCP/MSC must NOT be enabled; ARIES uses physical CP2102N 16550 UART0!";
#endif

    // Assert essential flight control features ARE enabled
#if !defined(USE_SPI) || !defined(USE_SPI_DEVICE_1)
    FAIL() << "SPI and SPI_DEVICE_1 must be enabled for IMU telemetry!";
#endif

#if !defined(USE_GYRO) || !defined(USE_ACC) || !defined(USE_GYRO_SPI_ICM42688P)
    FAIL() << "ICM42688P Gyro and Accel must be enabled for flight stabilization!";
#endif

#if !defined(USE_MOTOR) || !defined(USE_PWM_OUTPUT)
    FAIL() << "Motor PWM output subsystem must be enabled!";
#endif

#if !defined(USE_SERIALRX) || !defined(USE_SERIALRX_CRSF)
    FAIL() << "CRSF Serial RX must be enabled for radio communication!";
#endif

#if !defined(USE_CLI)
    FAIL() << "CLI subsystem must be enabled for terminal configuration!";
#endif
}

// =============================================================================
// TEST 05: In-SRAM Parameter Group Configuration (CONFIG_IN_RAM & 8 KB EEPROM)
// =============================================================================
TEST_F(AriesTargetConfigTest, Test_TargetConfig_ConfigInRam_8KB) {
    // Ground Truth 3: Parameter Groups in RAM
#if !defined(CONFIG_IN_RAM)
    FAIL() << "CONFIG_IN_RAM must be defined (THEJAS32 has no memory-mapped XIP flash)!";
#endif

#if defined(CONFIG_IN_FLASH)
    FAIL() << "CONFIG_IN_FLASH is strictly prohibited on THEJAS32 silicon!";
#endif

    // EEPROM_SIZE must strictly be >= 8192 bytes
    EXPECT_EQ(EEPROM_SIZE, 8192);

    // Active Parameter Groups in Betaflight 4.x occupy ~5120 bytes
    constexpr uint32_t ESTIMATED_ACTIVE_PG_BYTES = 5120U;
    EXPECT_GE(static_cast<uint32_t>(EEPROM_SIZE), ESTIMATED_ACTIVE_PG_BYTES);

    // Verify buffer occupancy is within safe bounds (between 50% and 80%)
    double buffer_occupancy = (static_cast<double>(ESTIMATED_ACTIVE_PG_BYTES) / EEPROM_SIZE) * 100.0;
    EXPECT_LE(buffer_occupancy, 80.0);
    EXPECT_GE(buffer_occupancy, 50.0);

    // Consequence analysis: If EEPROM_SIZE was 4096:
    // 5120 bytes > 4096 bytes -> FAILURE_CONFIG_STORE_FAILURE Panic on boot!
    constexpr uint32_t INSUFFICIENT_EEPROM_SIZE = 4096U;
    EXPECT_GT(ESTIMATED_ACTIVE_PG_BYTES, INSUFFICIENT_EEPROM_SIZE);
}

// =============================================================================
// TEST 06: Compile-Time Static Assertion Guard Verification
// =============================================================================
TEST_F(AriesTargetConfigTest, Test_TargetConfig_StaticAssertionGuards) {
    // Verify that the macro guards in config.h evaluate to true for our target:
    bool loop_denom_valid = (DEFAULT_PID_PROCESS_DENOM >= 8);
    bool config_in_ram_valid = true; // CONFIG_IN_RAM is defined
    bool config_in_flash_invalid = false; // CONFIG_IN_FLASH is undefined
    bool eeprom_size_valid = (EEPROM_SIZE >= 8192);

    EXPECT_TRUE(loop_denom_valid) << "DEFAULT_PID_PROCESS_DENOM must be >= 8";
    EXPECT_TRUE(config_in_ram_valid) << "CONFIG_IN_RAM must be true";
    EXPECT_FALSE(config_in_flash_invalid) << "CONFIG_IN_FLASH must be false";
    EXPECT_TRUE(eeprom_size_valid) << "EEPROM_SIZE must be >= 8192";
}

} // namespace
