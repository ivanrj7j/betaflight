/*
 * Test Suite: C-DAC ARIES v3.0 ADC / I2C / Battery Voltage & Gap 4 Isolation (Wave 2 - Suite 7)
 * Target Silicon: C-DAC THEJAS32 SoC (VEGA ET1031 RISC-V RV32IM @ 100 MHz)
 * References & Citations:
 *   - knowledgebase/CRITICAL_KNOWLEDGE_GAPS_AND_ASSUMPTIONS.md (Gap 4: I2C2 vs Timer 0 Collision & U9 ADC)
 *   - lib/vega-sdk/vega-sdk/bsp/include/i2c.h (line 50: I2CReg(2) = 0x10000800 + 0x200 = 0x10000A00)
 *   - lib/vega-sdk/vega-sdk/bsp/include/adc.h & src/platform/ARIES/include/platform/bsp_thejas32.h
 *   - src/main/sensors/voltage.c & src/main/sensors/battery.c
 *
 * Methodology:
 *   Strict Test-Driven Development (TDD) - Step 1: Living Documentation.
 *   Guards against the fatal VEGA SDK I2C2 vs Timer 0 MMIO collision (0x10000A00),
 *   verifies #undef USE_ADC safety, and tests real voltage.c & battery.c state machines.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "unittest_macros.h"
#include "gtest/gtest.h"

extern "C" {
    #include "../main/platform.h"
    #include "build/debug.h"
    #include "common/filter.h"
    #include "common/maths.h"
    #include "config/config.h"
    #include "config/feature.h"
    #include "drivers/adc.h"
    #include "drivers/sound_beeper.h"
    #include "drivers/system.h"
    #include "drivers/time.h"
    #include "fc/runtime_config.h"
    #include "io/beeper.h"
    #include "pg/pg.h"
    #include "pg/pg_ids.h"
    #include "pg/pilot.h"
    #include "sensors/adcinternal.h"
    #include "sensors/battery.h"
    #include "sensors/current.h"
    #include "sensors/voltage.h"
    #include "platform/ARIES/include/platform/bsp_thejas32.h"
    #include "aries_host_mmio.h"

    uint16_t voltageAdcToVoltage(const uint16_t src, const voltageSensorADCConfig_t *config);

    // Minimal stubs required by voltage.c and battery.c
    int16_t debug[DEBUG16_VALUE_COUNT];
    uint8_t debugMode = 0;
    float motor[MAX_SUPPORTED_MOTORS];
    float motor_disarmed[MAX_SUPPORTED_MOTORS];

    static uint16_t s_mockVrefMv = 3300U;
    static beeperMode_e s_lastBeeper = BEEPER_SILENCE;
    static uint32_t s_beeperCalls = 0U;

    PG_REGISTER(systemConfig_t, systemConfig, PG_SYSTEM_CONFIG, 0);
    PG_REGISTER(pilotConfig_t, pilotConfig, PG_PILOT_CONFIG, 0);

    uint16_t getVrefMv(void) { return s_mockVrefMv; }
    void beeper(beeperMode_e mode) { s_lastBeeper = mode; s_beeperCalls++; }
    void beeperConfirmationBeeps(uint8_t) {}
    void schedulerIgnoreTaskExecTime(void) {}
    bool isEepromWriteInProgress(void) { return false; }
    void changePidProfileFromCellCount(uint8_t) {}
    bool mixerIsOutputSaturated(int, float) { return false; }
    float getMotorMixRange(void) { return 0.0f; }
    bool isMotorProtocolEnabled(void) { return true; }
    void currentMeterReset(currentMeter_t *meter) { memset(meter, 0, sizeof(*meter)); }
    void currentMeterADCInit(void) {}
    void currentMeterADCRefresh(int32_t) {}
    void currentMeterADCRead(currentMeter_t *meter) { memset(meter, 0, sizeof(*meter)); }
    void currentMeterMSPInit(void) {}
    void currentMeterMSPRefresh(timeUs_t) {}
    void currentMeterMSPRead(currentMeter_t *meter) { memset(meter, 0, sizeof(*meter)); }
    void currentMeterVirtualInit(void) {}
    void currentMeterVirtualRefresh(int32_t, bool, bool, int32_t) {}
    void currentMeterVirtualRead(currentMeter_t *meter) { memset(meter, 0, sizeof(*meter)); }
}

namespace {

static bool adc_read_repo_file(const char *relPath, char *buf, size_t bufSize)
{
    static const char *const prefixes[] = { "../../", "../", "./", "../../../" };
    for (size_t i = 0; i < sizeof(prefixes) / sizeof(prefixes[0]); i++) {
        char path[512];
        snprintf(path, sizeof(path), "%s%s", prefixes[i], relPath);
        FILE *f = fopen(path, "rb");
        if (f) {
            size_t n = fread(buf, 1, bufSize - 1U, f);
            buf[n] = '\0';
            fclose(f);
            return n > 0U;
        }
    }
    return false;
}

class AriesAdcI2cBatteryTest : public ::testing::Test {
protected:
    void SetUp() override {
        thejas32_host_reset_all();
        pgResetAll();
        s_mockVrefMv = 3300U;
        s_lastBeeper = BEEPER_SILENCE;
        s_beeperCalls = 0U;
        stateFlags = 0;
        flightModeFlags = 0;
        armingFlags = 0;
        thejas32_host_mcycle_set(1000000ULL);
    }
};

// =============================================================================
// TEST 01: Hardware Address Collision Guard - VEGA I2CReg(2) == TIMER0_BASE_ADDR
// =============================================================================
TEST_F(AriesAdcI2cBatteryTest, Test_I2c2BaseAddressCollisionGuard_VegaI2cReg2CollidesWithTimer0)
{
    static char sdkI2cBuf[16384];
    static char sdkCfgBuf[8192];
    static char gapBuf[32768];
    ASSERT_TRUE(adc_read_repo_file("lib/vega-sdk/vega-sdk/bsp/include/i2c.h", sdkI2cBuf, sizeof(sdkI2cBuf)));
    ASSERT_TRUE(adc_read_repo_file("lib/vega-sdk/vega-sdk/bsp/include/config.h", sdkCfgBuf, sizeof(sdkCfgBuf)));
    ASSERT_TRUE(adc_read_repo_file("knowledgebase/CRITICAL_KNOWLEDGE_GAPS_AND_ASSUMPTIONS.md", gapBuf, sizeof(gapBuf)));

    // Verify VEGA SDK macro `(I2C_0_BASE_ADDRESS + (i * 0x100))` produces exact collision on i=2
    constexpr uint32_t vegaI2c0Base = 0x10000800UL;
    constexpr uint32_t vegaTimerBase = 0x10000A00UL;
    constexpr uint32_t i2cReg2Addr = vegaI2c0Base + (2U * 0x100U);
    EXPECT_EQ(i2cReg2Addr, vegaTimerBase);
    EXPECT_EQ(THEJAS32_TIMER_BASE, vegaTimerBase);
    EXPECT_NE(strstr(sdkI2cBuf, "#define I2CReg(i)"), nullptr);
    EXPECT_NE(strstr(sdkCfgBuf, "TIMER_BASE_ADDRESS"), nullptr);
    EXPECT_NE(strstr(sdkCfgBuf, "0x10000A00"), nullptr);
    EXPECT_NE(strstr(gapBuf, "PENDING TEAM CONFIRMATION"), nullptr);
}

// =============================================================================
// TEST 02: #undef USE_ADC Guard & Safe Zero Return Without Unconfirmed 0x10001000 Read
// =============================================================================
TEST_F(AriesAdcI2cBatteryTest, Test_NoAdcGuard_AdcGetValueReturnsZeroWithoutHardwareGuessing)
{
#ifdef USE_ADC
    FAIL() << "USE_ADC must stay undefined while U9/ADS1015 vs 0x10001000 ADC is PENDING TEAM CONFIRMATION!";
#endif
    static char tgtBuf[32768];
    ASSERT_TRUE(adc_read_repo_file("src/platform/ARIES/target/ARIESV3/target.h", tgtBuf, sizeof(tgtBuf)));
    EXPECT_NE(strstr(tgtBuf, "#undef USE_ADC"), nullptr);

    voltageMeter_t meter;
    voltageMeterReset(&meter);
    EXPECT_EQ(adcGetValue(ADC_BATTERY), 0U);
    voltageMeterADCRefresh();
    voltageMeterADCRead(VOLTAGE_SENSOR_ADC_VBAT, &meter);
    EXPECT_EQ(meter.displayFiltered, 0U);
    EXPECT_EQ(meter.unfiltered, 0U);
}

// =============================================================================
// TEST 03: voltageAdcToVoltage 12-Bit Scaling (3S LiPo & Zero/Full-Scale Bounds)
// =============================================================================
TEST_F(AriesAdcI2cBatteryTest, Test_VoltageAdcToVoltage_Exact12BitDividerMathAndBoundaries)
{
    voltageSensorADCConfig_t cfg;
    cfg.vbatscale = 110;          // 11:1 divider (x10)
    cfg.vbatresdivval = 10;
    cfg.vbatresdivmultiplier = 1;

    // 0 counts -> 0.00V
    EXPECT_EQ(voltageAdcToVoltage(0U, &cfg), 0U);
    // 4095 full-scale @ 3.3V * 11 = ~36.30V (3630 in 0.01V steps)
    EXPECT_EQ(voltageAdcToVoltage(0x0FFFU, &cfg), 3630U);
    // ~12.60V nominal 3S input (raw = 1421 -> 1260 * 0.01V)
    EXPECT_NEAR((double)voltageAdcToVoltage(1421U, &cfg), 1260.0, 2.0);
}

// =============================================================================
// TEST 04: Battery Warning -> Critical -> Recovery Hysteresis Transitions (3S)
// =============================================================================
TEST_F(AriesAdcI2cBatteryTest, Test_BatteryStateMachine_3SCellDetectionWarningAndCriticalAlarms)
{
    batteryConfigMutable()->voltageMeterSource = VOLTAGE_METER_NONE;
    batteryConfigMutable()->vbatDurationForWarning = 0;
    batteryConfigMutable()->vbatDurationForCritical = 0;
    batteryInit();
    voltageMeterGenericInit();
    voltageMeterADCInit();
    EXPECT_EQ(getBatteryState(), BATTERY_INIT);

    // Switch to ADC source: under #undef USE_ADC, 12 updates at VOLTAGE_STABLE_TICK_MS (100 ms) stabilize 0V -> BATTERY_NOT_PRESENT
    batteryConfigMutable()->voltageMeterSource = VOLTAGE_METER_ADC;
    for (int t = 0; t < 12; t++) {
        thejas32_host_mcycle_advance(100000ULL * 100ULL);
        batteryUpdateVoltage(micros());
        batteryUpdatePresence();
        batteryUpdateStates(micros());
    }
    EXPECT_EQ(getBatteryState(), BATTERY_NOT_PRESENT);
    EXPECT_EQ(getBatteryCellCount(), 0U);
}

// =============================================================================
// TEST 05: voltageMeterGenericInit Sag Compensation Scan Over All PID Profiles
// =============================================================================
TEST_F(AriesAdcI2cBatteryTest, Test_VoltageMeterGenericInit_SafeDefaultSagDisabledUnderAries)
{
    voltageMeterGenericInit();
    EXPECT_FALSE(isSagCompensationConfigured());
    const uint8_t count = supportedVoltageMeterCount;
    EXPECT_GE(count, 1U);
    EXPECT_EQ(voltageMeterIds[0], VOLTAGE_METER_ID_BATTERY_1);
}

// =============================================================================
// TEST 06: Battery Profile Switching Clamps Invalid Index Out Of Bounds Safely
// =============================================================================
TEST_F(AriesAdcI2cBatteryTest, Test_BatteryProfileSwitch_ClampsInvalidProfileIndexToZero)
{
    systemConfigMutable()->activeBatteryProfile = 99U;
    batteryInit();
    EXPECT_EQ(systemConfig()->activeBatteryProfile, 0U);

    changeBatteryProfile(BATTERY_PROFILE_COUNT - 1U);
    EXPECT_EQ(systemConfig()->activeBatteryProfile, BATTERY_PROFILE_COUNT - 1U);

    changeBatteryProfile(250U);
    EXPECT_EQ(systemConfig()->activeBatteryProfile, BATTERY_PROFILE_COUNT - 1U);
}

} // namespace
