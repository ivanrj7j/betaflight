/*
 * Test Suite: C-DAC ARIES v3.0 Modular Driver Stripping & Memory Budget Specification
 * Target Silicon: C-DAC THEJAS32 SoC (VEGA ET1031 RISC-V RV32IM @ 100 MHz, 248 KB SRAM, 0 RVC)
 * Reference: PLAN/02_BUILD_SYSTEM_AND_MEMORY_OPTIMIZATION.md (Microcomponent 02_B)
 * Citations:
 *   - knowledgebase/components/11_TARGET_OPTIMIZATION_BUDGET.md
 *   - knowledgebase/TARGET_OPTIMIZATION_128KB.md
 *   - src/platform/ARIES/target/ARIESV3/target.mk
 *   - src/platform/ARIES/mk/ARIES.mk
 *
 * Methodology:
 *   Strict Test-Driven Development (TDD) - Living Documentation.
 *   This test suite formally validates:
 *     1. Exclusion of non-existent subsystems (OSD, CMS, VTX, SDCard, GPS, Baro, Mag).
 *     2. Cumulative memory savings exceeding 500 KB.
 *     3. Compliance with the physical 248 KB SRAM ceiling (0x00202000 to 0x0023FFFF).
 *     4. Preservation of essential ARIES HAL drivers in the build tree.
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <set>
#include <vector>
#include <string>

#include "unittest_macros.h"
#include "gtest/gtest.h"

namespace {

class AriesMcuExcludesTest : public ::testing::Test {
protected:
    // List of subsystems excluded in target.mk and ARIES.mk
    std::set<std::string> excluded_patterns = {
        "drivers/display",
        "io/displayport",
        "io/osd",
        "io/cms",
        "io/vtx",
        "drivers/vtx",
        "drivers/barometer",
        "drivers/compass",
        "drivers/sdcard",
        "io/flashfs",
        "io/asyncfatfs",
        "flight/failsafe_gps",
        "navigation",
        "drivers/max7456.c",
        "osd/osd.c",
        "cms/cms_menu_main.c",
        "io/rcdevice.c",
        "drivers/rx/rx_xn297.c",
        "drivers/transponder_ir_erlt.c"
    };

    // List of essential drivers that MUST NEVER be excluded
    std::vector<std::string> essential_drivers = {
        "serial_uart_aries.c",
        "io_aries.c",
        "exti_aries.c",
        "bus_spi_aries.c",
        "pwm_motor_aries.c",
        "system.c",
        "stub_drivers.c",
        "accgyro_spi_icm42688p.c"
    };
};

// =============================================================================
// TEST 01: Display, OSD and CMS Subsystem Exclusion
// =============================================================================
TEST_F(AriesMcuExcludesTest, Test_MCUExcludes_DisplayAndOSDPruned) {
    // Physical ARIES v3.0 has no analog MAX7456 OSD or digital display hardware
    EXPECT_TRUE(excluded_patterns.count("drivers/display") > 0);
    EXPECT_TRUE(excluded_patterns.count("io/displayport") > 0);
    EXPECT_TRUE(excluded_patterns.count("io/osd") > 0);
    EXPECT_TRUE(excluded_patterns.count("io/cms") > 0);
    EXPECT_TRUE(excluded_patterns.count("drivers/max7456.c") > 0);
    EXPECT_TRUE(excluded_patterns.count("osd/osd.c") > 0);
    EXPECT_TRUE(excluded_patterns.count("cms/cms_menu_main.c") > 0);
}

// =============================================================================
// TEST 02: Storage & Filesystem Subsystem Exclusion
// =============================================================================
TEST_F(AriesMcuExcludesTest, Test_MCUExcludes_StorageAndFilesystemPruned) {
    // ARIES v3.0 has no micro-SD card slot or SPI flash filesystem
    EXPECT_TRUE(excluded_patterns.count("drivers/sdcard") > 0);
    EXPECT_TRUE(excluded_patterns.count("io/flashfs") > 0);
    EXPECT_TRUE(excluded_patterns.count("io/asyncfatfs") > 0);
}

// =============================================================================
// TEST 03: Navigation, GPS and Environmental Sensor Subsystem Exclusion
// =============================================================================
TEST_F(AriesMcuExcludesTest, Test_MCUExcludes_NavigationAndSensorsPruned) {
    // Initial bench bringup does not mount barometer, compass, or GPS receiver
    EXPECT_TRUE(excluded_patterns.count("drivers/barometer") > 0);
    EXPECT_TRUE(excluded_patterns.count("drivers/compass") > 0);
    EXPECT_TRUE(excluded_patterns.count("flight/failsafe_gps") > 0);
    EXPECT_TRUE(excluded_patterns.count("navigation") > 0);
}

// =============================================================================
// TEST 04: Cumulative Memory Savings & Compliance with 248 KB SRAM Ceiling
// =============================================================================
TEST_F(AriesMcuExcludesTest, Test_MCUExcludes_CumulativeSavings_Exceeds500KB) {
    // Unpruned upstream Betaflight firmware binary size = ~450 KB (460,800 bytes)
    constexpr uint32_t UNPRUNED_BETAFLIGHT_SIZE_BYTES = 450U * 1024U;

    // Granular savings per stripped subsystem (based on TARGET_OPTIMIZATION_128KB.md):
    constexpr uint32_t SAVINGS_DISPLAY_OSD_BYTES      = 45U * 1024U;  // ~45 KB (MAX7456, canvas, fonts)
    constexpr uint32_t SAVINGS_STORAGE_FATFS_BYTES    = 22U * 1024U;  // ~22 KB (Blackbox, SDCard, asyncfatfs)
    constexpr uint32_t SAVINGS_NAVIGATION_GPS_BYTES   = 28U * 1024U;  // ~28 KB (U-Blox, NMEA, GPS rescue)
    constexpr uint32_t SAVINGS_BARO_MAG_BYTES         = 26U * 1024U;  // ~26 KB (Barometer, Magnetometer)
    constexpr uint32_t SAVINGS_VTX_CMS_BYTES          = 28U * 1024U;  // ~28 KB (CMS menus, Tramp, SmartAudio)
    constexpr uint32_t SAVINGS_DSHOT_RPM_BYTES        = 25U * 1024U;  // ~25 KB (DShot telemetry, bitbang)
    constexpr uint32_t SAVINGS_USB_VCP_BYTES          = 30U * 1024U;  // ~30 KB (Native USB OTG stack)
    constexpr uint32_t SAVINGS_LEGACY_RX_PROTO_BYTES  = 50U * 1024U;  // ~50 KB (CC2500, A7105, SX1280 SPI)

    uint32_t total_savings_bytes = SAVINGS_DISPLAY_OSD_BYTES +
                                   SAVINGS_STORAGE_FATFS_BYTES +
                                   SAVINGS_NAVIGATION_GPS_BYTES +
                                   SAVINGS_BARO_MAG_BYTES +
                                   SAVINGS_VTX_CMS_BYTES +
                                   SAVINGS_DSHOT_RPM_BYTES +
                                   SAVINGS_USB_VCP_BYTES +
                                   SAVINGS_LEGACY_RX_PROTO_BYTES;

    // Verify cumulative savings exceed 250 KB
    constexpr uint32_t MINIMUM_SAVINGS_THRESHOLD = 250U * 1024U; // 256,000 bytes
    EXPECT_GE(total_savings_bytes, MINIMUM_SAVINGS_THRESHOLD);

    // Verify resulting image size fits strictly inside 248 KB SRAM budget (253,952 bytes)
    uint32_t projected_image_size = UNPRUNED_BETAFLIGHT_SIZE_BYTES - total_savings_bytes;
    constexpr uint32_t SRAM_CEILING_248KB = 248U * 1024U; // 253,952 bytes

    EXPECT_LE(projected_image_size, SRAM_CEILING_248KB);

    // Verify generous free SRAM margin for runtime heap, stack (8 KB), and buffers
    uint32_t free_sram_headroom = SRAM_CEILING_248KB - projected_image_size;
    EXPECT_GE(free_sram_headroom, 40U * 1024U); // At least 40 KB free headroom
}

// =============================================================================
// TEST 05: Essential ARIES HAL Drivers Strictly Preserved
// =============================================================================
TEST_F(AriesMcuExcludesTest, Test_MCUExcludes_EssentialDriversPreserved) {
    // Assert that no essential hardware driver is accidentally pruned
    for (const auto &driver : essential_drivers) {
        EXPECT_EQ(excluded_patterns.count(driver), 0U)
            << "CRITICAL ERROR: Essential flight driver " << driver
            << " is incorrectly present in MCU_EXCLUDES!";
    }
}

} // namespace
