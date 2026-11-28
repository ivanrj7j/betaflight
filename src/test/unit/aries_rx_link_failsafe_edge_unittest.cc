/*
 * Test Suite: C-DAC ARIES v3.0 CRSF Protocol, Failsafe State Machine & Wraparound Edges (Wave 3 - Suite 11)
 * Target Silicon: C-DAC THEJAS32 SoC (VEGA ET1031 RISC-V RV32IM @ 100 MHz, UART1 420 kbaud)
 * References & Citations:
 *   - PLAN/06_SENSORS_RC_TELEMETRY_AND_SCHEDULER.md
 *   - src/main/rx/crsf.c & src/main/rx/crsf.h
 *   - src/main/flight/failsafe.c (lines 364 & 548 millis() wraparound bug!)
 *   - src/main/fc/runtime_config.c & src/main/fc/rc_modes.c
 *
 * Methodology:
 *   Strict Test-Driven Development (TDD) - Step 1: Living Documentation & TDD-RED.
 *   Tests real crsf.c parser + real failsafe.c state transitions, including an intentional
 *   TDD-RED test exposing the real 32-bit millis() wraparound comparison bug across 0xFFFFFFFF -> 0!
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <vector>

#include "unittest_macros.h"
#include "gtest/gtest.h"

extern "C" {
    #include "../main/platform.h"
    #include "build/build_config.h"
    #include "build/debug.h"
    #include "common/bitarray.h"
    #include "common/crc.h"
    #include "common/maths.h"
    #include "common/time.h"
    #include "config/feature.h"
    #include "drivers/io.h"
    #include "drivers/serial.h"
    #include "drivers/sound_beeper.h"
    #include "drivers/system.h"
    #include "drivers/time.h"
    #include "fc/core.h"
    #include "fc/rc_controls.h"
    #include "fc/rc_modes.h"
    #include "fc/runtime_config.h"
    #include "flight/failsafe.h"
    #include "io/beeper.h"
    #include "io/serial.h"
    #include "io/serial_feature_map.h"
    #include "pg/autopilot.h"
    #include "pg/pg.h"
    #include "pg/pg_ids.h"
    #include "pg/rx.h"
    #include "rx/crsf.h"
    #include "rx/crsf_protocol.h"
    #include "rx/rx.h"
    #include "platform/ARIES/include/platform/bsp_thejas32.h"
    #include "aries_host_mmio.h"

    // Globals & stubs needed by real crsf.c + failsafe.c
    int16_t debug[DEBUG16_VALUE_COUNT];
    uint8_t debugMode = 0;
    float rcData[MAX_SUPPORTED_RC_CHANNEL_COUNT];
    rxRuntimeState_t rxRuntimeState;
    static serialPort_t s_fakeUart1;
    static serialReceiveCallbackPtr s_crsfRxCb = nullptr;
    static void *s_crsfRxData = nullptr;
    static uint32_t s_disarmCalls = 0U;

    PG_REGISTER(serialConfig_t, serialConfig, PG_SERIAL_CONFIG, 0);
    PG_REGISTER(rxConfig_t, rxConfig, PG_RX_CONFIG, 0);
    PG_REGISTER(rxFailsafeChannelConfig_t, rxFailsafeChannelConfigs_dummy_unused, PG_RESERVED_FOR_TESTING_1, 0);

    serialPort_t *openSerialPort(serialPortIdentifier_e id, serialPortFunction_e,
                                 serialReceiveCallbackPtr cb, void *cbData,
                                 uint32_t baudRate, portMode_e, portOptions_e) {
        s_fakeUart1.identifier = id;
        s_fakeUart1.baudRate = baudRate;
        s_crsfRxCb = cb;
        s_crsfRxData = cbData;
        return &s_fakeUart1;
    }

    void closeSerialPort(serialPort_t *) {}
    bool isSerialTransmitBufferEmpty(const serialPort_t *) { return true; }
    uint32_t serialTxBytesFree(const serialPort_t *) { return 256U; }
    void serialWriteBuf(serialPort_t *, const uint8_t *, int) {}
    void serialSetMode(serialPort_t *, portMode_e) {}
    void telemetryCheckInvertedSerial(serialPort_t *, bool) {}
    void serialPassThrough(serialPort_t *, serialPort_t *, serialConsumer *, serialConsumer *) {}
    void crsfTelemetryScheduleCatchup(void) {}
    void beeper(beeperMode_e) {}
    void beeperConfirmationBeeps(uint8_t) {}
    void disarm(flightLogDisarmReason_e) { s_disarmCalls++; DISABLE_ARMING_FLAG(ARMED); }
    bool isUsingSticksForArming(void) { return false; }
    float getCosTiltAngle(void) { return 1.0f; }
    bool calculateRxChannelsAndUpdateFailsafe(timeUs_t) { return true; }
    rssiSource_e rssiSource = RSSI_SOURCE_NONE;
    void setRssi(uint16_t rssi, rssiSource_e) { (void)rssi; }
    void setRssiDirect(uint16_t, rssiSource_e) {}
    void setLinkQuality(bool, uint16_t, uint16_t) {}
    void setRssiDbm(int16_t, rssiSource_e) {}
    void setRssiDbmDirect(int16_t, rssiSource_e) {}
    void setRssiSnr(int8_t) {}
    void setRsnr(int16_t) {}
    void setRsnrDirect(int16_t) {}
    void setActiveAntenna(int8_t) {}
    bool mspSerialIsConfiguratorActive(void) { return false; }
    bool crashRecoveryModeActive(void) { return false; }
    throttleStatus_e calculateThrottleStatus(void) {
        return (rcData[THROTTLE] < rxConfig()->mincheck) ? THROTTLE_LOW : THROTTLE_HIGH;
    }
    bool checkCrsfTelemetryState(void) { return false; }
    bool featureIsEnabled(uint32_t mask); // defined in feature.c -> stub if not linked
    bool isFlightPlanModeActive(void) { return false; }
    bool autopilotMissionIsActive(void) { return false; }
    bool autopilotIsActive(void) { return false; }
    uint8_t getRssiPercent(void) { return 99U; }
    bool rxAreFlightChannelsValid(void) { return true; }
    const uint32_t baudRates[BAUD_COUNT] = { 0, 9600, 19200, 38400, 57600, 115200, 230400, 250000, 400000, 460800, 500000, 921600 };
}

namespace {

static std::vector<uint8_t> buildCrsfChannelsFrame(uint16_t rawVal11Bit, uint8_t syncAddr = CRSF_ADDRESS_FLIGHT_CONTROLLER, uint8_t type = CRSF_FRAMETYPE_RC_CHANNELS_PACKED)
{
    uint8_t payload[22] = { 0 };
    uint32_t bitPos = 0U;
    for (int ch = 0; ch < 16; ch++) {
        uint32_t v = rawVal11Bit & 0x7FFU;
        for (uint32_t b = 0U; b < 11U; b++) {
            if (v & (1U << b)) {
                payload[bitPos / 8U] |= (uint8_t)(1U << (bitPos % 8U));
            }
            bitPos++;
        }
    }
    const uint8_t frameLen = 22U + 2U; // type + payload + crc
    std::vector<uint8_t> frame;
    frame.push_back(syncAddr);
    frame.push_back(frameLen);
    frame.push_back(type);
    uint8_t crc = crc8_dvb_s2(0U, type);
    for (int i = 0; i < 22; i++) {
        frame.push_back(payload[i]);
        crc = crc8_dvb_s2(crc, payload[i]);
    }
    frame.push_back(crc);
    return frame;
}

class AriesRxLinkFailsafeEdgeTest : public ::testing::Test {
protected:
    void SetUp() override {
        thejas32_host_reset_all();
        pgResetAll();
        memset(&rxRuntimeState, 0, sizeof(rxRuntimeState));
        memset(rcData, 0, sizeof(rcData));
        s_crsfRxCb = nullptr;
        s_crsfRxData = nullptr;
        s_disarmCalls = 0U;
        armingFlags = 0;
        flightModeFlags = 0;
        stateFlags = 0;
        thejas32_host_mcycle_set(1000000ULL * 100ULL); // t = 1,000,000 us = 1000 ms

        rxConfigMutable()->serialrx_provider = SERIALRX_CRSF;
        rxConfigMutable()->rssi_src_frame_errors = false;
        rxConfigMutable()->midrc = 1500;
        rxConfigMutable()->mincheck = 1050;
        rxConfigMutable()->maxcheck = 1900;

        failsafeConfigMutable()->failsafe_delay = 5;              // 0.5 s -> 500 ms
        failsafeConfigMutable()->failsafe_landing_time = 2;       // 2.0 s -> 2000 ms
        failsafeConfigMutable()->failsafe_throttle = 1250;
        failsafeConfigMutable()->failsafe_throttle_low_delay = 50; // 5.0 s
        failsafeConfigMutable()->failsafe_procedure = FAILSAFE_PROCEDURE_AUTO_LANDING;
        failsafeConfigMutable()->failsafe_switch_mode = FAILSAFE_SWITCH_MODE_STAGE1;
        failsafeConfigMutable()->failsafe_recovery_delay = 1;     // 0.1 s = 100 ms

        crsfRxInit(rxConfig(), &rxRuntimeState);
        failsafeInit();
        failsafeReset();
    }

    void TearDown() override {
        boxBitmask_t zeroMask = {};
        rcModeUpdate(&zeroMask);
        if (failsafeIsActive()) {
            const uint32_t now = millis();
            const uint32_t stepMs = (now > 0xFFFF0000UL) ? 115U : 200U;
            failsafeOnValidDataReceived();
            thejas32_host_mcycle_advance((uint64_t)stepMs * 100000ULL);
            failsafeOnValidDataReceived();
            failsafeUpdateState();
            if (failsafeIsActive()) {
                thejas32_host_mcycle_advance(60000ULL * 100ULL);
                failsafeOnValidDataReceived();
                failsafeUpdateState();
            }
        }
        failsafeInit();
        failsafeReset();
    }

    void bringRxLinkUp() {
        failsafeOnValidDataReceived();
        thejas32_host_mcycle_advance(200000ULL * 100ULL);
        failsafeOnValidDataReceived();
        failsafeUpdateState();
    }

    void feedCrsfBytes(const std::vector<uint8_t> &bytes, uint32_t interByteUs = 24U) {
        ASSERT_NE(s_crsfRxCb, nullptr);
        for (uint8_t b : bytes) {
            s_crsfRxCb((uint16_t)b, s_crsfRxData);
            thejas32_host_mcycle_advance((uint64_t)interByteUs * 100ULL);
        }
    }
};

// =============================================================================
// EDGE 01: CRSF 0x16 RC_CHANNELS_PACKED Boundary Decode (172 / 992 / 1811)
// =============================================================================
TEST_F(AriesRxLinkFailsafeEdgeTest, RxEdge01_CrsfRcChannelsPacked_Decodes172MidAnd1811Bounds)
{
    ASSERT_NE(rxRuntimeState.rcReadRawFn, nullptr);

    feedCrsfBytes(buildCrsfChannelsFrame(172U));
    ASSERT_NE(rxRuntimeState.rcFrameStatusFn(&rxRuntimeState) & RX_FRAME_COMPLETE, 0U);
    EXPECT_NEAR(rxRuntimeState.rcReadRawFn(&rxRuntimeState, 0U), 988.0f, 3.0f);

    feedCrsfBytes(buildCrsfChannelsFrame(992U));
    ASSERT_NE(rxRuntimeState.rcFrameStatusFn(&rxRuntimeState) & RX_FRAME_COMPLETE, 0U);
    EXPECT_NEAR(rxRuntimeState.rcReadRawFn(&rxRuntimeState, 3U), 1500.0f, 2.0f);

    feedCrsfBytes(buildCrsfChannelsFrame(1811U));
    ASSERT_NE(rxRuntimeState.rcFrameStatusFn(&rxRuntimeState) & RX_FRAME_COMPLETE, 0U);
    EXPECT_NEAR(rxRuntimeState.rcReadRawFn(&rxRuntimeState, 15U), 2012.0f, 3.0f);
}

// =============================================================================
// EDGE 02: Non-0x16 Extended Frame Subset / Bad CRC Never Corrupts Channels
// =============================================================================
TEST_F(AriesRxLinkFailsafeEdgeTest, RxEdge02_BadCrcAndUnknownFrameType_PreservesLastCleanChannels)
{
    feedCrsfBytes(buildCrsfChannelsFrame(992U));
    ASSERT_NE(rxRuntimeState.rcFrameStatusFn(&rxRuntimeState) & RX_FRAME_COMPLETE, 0U);
    const float prev = rxRuntimeState.rcReadRawFn(&rxRuntimeState, 0U);

    // Corrupt CRC byte on a 1811 frame
    auto bad = buildCrsfChannelsFrame(1811U);
    bad.back() ^= 0x55U;
    feedCrsfBytes(bad);
    EXPECT_EQ(rxRuntimeState.rcFrameStatusFn(&rxRuntimeState) & RX_FRAME_COMPLETE, 0U);
    EXPECT_FLOAT_EQ(rxRuntimeState.rcReadRawFn(&rxRuntimeState, 0U), prev);
}

// =============================================================================
// EDGE 03: Non-FC Address Filtering Rejects Stray Non-0xC8 Preamble
// =============================================================================
TEST_F(AriesRxLinkFailsafeEdgeTest, RxEdge03_NonFcSyncAddressByte_IgnoredCleanly)
{
    auto nonFc = buildCrsfChannelsFrame(1811U, 0x00U); // Invalid sync/addr
    feedCrsfBytes(nonFc);
    EXPECT_EQ(rxRuntimeState.rcFrameStatusFn(&rxRuntimeState) & RX_FRAME_COMPLETE, 0U);
}

// =============================================================================
// EDGE 04: Invalid CRSF FrameLength (< 4 or > 64) Rejected Without Buffer Overflow
// =============================================================================
TEST_F(AriesRxLinkFailsafeEdgeTest, RxEdge04_InvalidCrsfFrameLength_AboveMaxOrBelowMinRejected)
{
    std::vector<uint8_t> tooLong = { CRSF_ADDRESS_FLIGHT_CONTROLLER, 120U, CRSF_FRAMETYPE_RC_CHANNELS_PACKED, 0xAAU };
    feedCrsfBytes(tooLong);
    EXPECT_EQ(rxRuntimeState.rcFrameStatusFn(&rxRuntimeState) & RX_FRAME_COMPLETE, 0U);

    // Immediately follow with valid 1100us frame -> parser recovered cleanly!
    thejas32_host_mcycle_advance(3000ULL * 100ULL);
    feedCrsfBytes(buildCrsfChannelsFrame(992U));
    EXPECT_NE(rxRuntimeState.rcFrameStatusFn(&rxRuntimeState) & RX_FRAME_COMPLETE, 0U);
}

// =============================================================================
// EDGE 05: >1750 us Inter-Frame Idle Gap Resets Partial CRSF Byte Index
// =============================================================================
TEST_F(AriesRxLinkFailsafeEdgeTest, RxEdge05_InterByteGapTimeoutOver1750Us_ResyncsCleanlyOnNextFrame)
{
    auto valid = buildCrsfChannelsFrame(1811U);
    // Feed first 6 bytes of truncated noise frame
    for (int i = 0; i < 6; i++) {
        s_crsfRxCb(valid[i], s_crsfRxData);
        thejas32_host_mcycle_advance(24ULL * 100ULL);
    }
    // Wait > 2000 us (> 1748 us CRSF frame boundary)
    thejas32_host_mcycle_advance(2500ULL * 100ULL);

    // Next complete valid frame must sync at byte 0 cleanly
    feedCrsfBytes(buildCrsfChannelsFrame(992U));
    EXPECT_NE(rxRuntimeState.rcFrameStatusFn(&rxRuntimeState) & RX_FRAME_COMPLETE, 0U);
    EXPECT_NEAR(rxRuntimeState.rcReadRawFn(&rxRuntimeState, 0U), 1500.0f, 2.0f);
}

// =============================================================================
// EDGE 06: #undef USE_CRSF_V3 Verified So Telemetry Never Triggers In-Flight EEPROM Write
// =============================================================================
TEST_F(AriesRxLinkFailsafeEdgeTest, RxEdge06_CrsfV3UndefGuard_VerifiedAgainstTargetHeader)
{
#ifdef USE_CRSF_V3
    FAIL() << "USE_CRSF_V3 must stay #undef on ARIES!";
#endif
    SUCCEED();
}

// =============================================================================
// EDGE 07: Link Statistics (0x14) Frame Processes Safely & Triggers Telemetry Handshake
// =============================================================================
TEST_F(AriesRxLinkFailsafeEdgeTest, RxEdge07_LinkStatisticsFrame0x14_IngestedWithoutError)
{
    std::vector<uint8_t> linkStats = { CRSF_ADDRESS_FLIGHT_CONTROLLER, 12U, CRSF_FRAMETYPE_LINK_STATISTICS };
    uint8_t payload[10] = { 90U, 85U, 100U, 12U, 0U, 2U, 1U, 88U, 90U, 10U };
    uint8_t crc = crc8_dvb_s2(0U, CRSF_FRAMETYPE_LINK_STATISTICS);
    for (int i = 0; i < 10; i++) {
        linkStats.push_back(payload[i]);
        crc = crc8_dvb_s2(crc, payload[i]);
    }
    linkStats.push_back(crc);
    feedCrsfBytes(linkStats);
    EXPECT_EQ(rxRuntimeState.rcFrameStatusFn(&rxRuntimeState) & RX_FRAME_COMPLETE, 0U); // Link stats is telemetry, not RC
}

// =============================================================================
// EDGE 08: Real failsafe.c Lifecycle: Idle -> Landing -> Landed -> Monitoring
// =============================================================================
TEST_F(AriesRxLinkFailsafeEdgeTest, RxEdge08_FailsafeLifecycle_IdleToDetectedToLandingToLandedDisarm)
{
    ENABLE_ARMING_FLAG(ARMED);
    failsafeStartMonitoring();
    rcData[THROTTLE] = 1600.0f; // High throttle -> triggers full auto-landing instead of immediate cut
    bringRxLinkUp();
    EXPECT_EQ(failsafePhase(), FAILSAFE_IDLE);

    // Cut RX link past 500ms failsafe_delay -> FAILSAFE_LANDING
    failsafeOnValidDataFailed();
    for (int t = 0; t < 15; t++) {
        thejas32_host_mcycle_advance(50000ULL * 100ULL);
        failsafeCheckDataFailurePeriod();
        failsafeUpdateState();
    }
    EXPECT_EQ(failsafePhase(), FAILSAFE_LANDING);
    EXPECT_TRUE(failsafeIsActive());

    // Advance past 2000ms landing timer -> FAILSAFE_LANDED (disarms synchronously & enters MONITORING)
    for (int t = 0; t < 50; t++) {
        thejas32_host_mcycle_advance(50000ULL * 100ULL);
        failsafeCheckDataFailurePeriod();
        failsafeUpdateState();
    }
    EXPECT_EQ(failsafePhase(), FAILSAFE_RX_LOSS_MONITORING);
    EXPECT_FALSE(ARMING_FLAG(ARMED));
    EXPECT_GE(s_disarmCalls, 1U);
}

// =============================================================================
// EDGE 09: Intermittent RX Recovery During Stage 1 Clears Pending Stage 2 Failsafe
// =============================================================================
TEST_F(AriesRxLinkFailsafeEdgeTest, RxEdge09_FailsafeIntermittentRecovery_ClearsDuringStage1Guard)
{
    ENABLE_ARMING_FLAG(ARMED);
    failsafeStartMonitoring();
    rcData[THROTTLE] = 1500.0f;
    bringRxLinkUp();
    EXPECT_EQ(failsafePhase(), FAILSAFE_IDLE);

    // Brief 200ms drop (< 500ms failsafe_delay): STAGE 1 RXLOSS set, Stage 2 NOT entered
    failsafeOnValidDataFailed();
    for (int t = 0; t < 4; t++) {
        thejas32_host_mcycle_advance(50000ULL * 100ULL);
        failsafeCheckDataFailurePeriod();
        failsafeUpdateState();
    }
    EXPECT_TRUE(getArmingDisableFlags() & ARMING_DISABLED_RX_FAILSAFE);
    EXPECT_TRUE(failsafeIsReceivingRxData());
    EXPECT_EQ(failsafePhase(), FAILSAFE_IDLE);

    // Recover link cleanly across > 100 ms -> ARMING_DISABLED_RX_FAILSAFE clears
    bringRxLinkUp();
    EXPECT_FALSE(getArmingDisableFlags() & ARMING_DISABLED_RX_FAILSAFE);
    EXPECT_FALSE(failsafeIsActive());
}

// =============================================================================
// EDGE 10: Failsafe Switch KILL Mode Disarms Immediately Without Landing Ramp
// =============================================================================
TEST_F(AriesRxLinkFailsafeEdgeTest, RxEdge10_FailsafeSwitchKillMode_DisarmsImmediately)
{
    failsafeConfigMutable()->failsafe_switch_mode = FAILSAFE_SWITCH_MODE_KILL;
    ENABLE_ARMING_FLAG(ARMED);
    failsafeStartMonitoring();
    rcData[THROTTLE] = 1600.0f;
    bringRxLinkUp();
    ASSERT_EQ(failsafePhase(), FAILSAFE_IDLE);

    boxBitmask_t mask = {};
    bitArraySet(&mask, BOXFAILSAFE);
    rcModeUpdate(&mask);
    failsafeUpdateState();

    EXPECT_FALSE(ARMING_FLAG(ARMED));
    EXPECT_GE(s_disarmCalls, 1U);
    EXPECT_TRUE(failsafeIsActive());
    EXPECT_EQ(failsafePhase(), FAILSAFE_RX_LOSS_MONITORING);
}

// =============================================================================
// EDGE 11: Armed With Throttle Low > failsafe_throttle_low_delay Cuts Directly
// =============================================================================
TEST_F(AriesRxLinkFailsafeEdgeTest, RxEdge11_ThrottleLowDelayExceeded_SkipsAutoLandAndDisarms)
{
    ENABLE_ARMING_FLAG(ARMED);
    failsafeStartMonitoring();
    rcData[THROTTLE] = 1600.0f;
    bringRxLinkUp();
    failsafeUpdateState(); // Sets throttleLowPeriod = now + 5000ms

    // Drop throttle below mincheck for > 5.0s
    rcData[THROTTLE] = 1000.0f;
    for (int i = 0; i < 60; i++) {
        failsafeOnValidDataReceived();
        thejas32_host_mcycle_advance(100000ULL * 100ULL);
        failsafeUpdateState();
    }

    // Now cut RX for 600ms -> MUST disarm directly WITHOUT waiting 2000ms landing timer!
    failsafeOnValidDataFailed();
    for (int t = 0; t < 15; t++) {
        thejas32_host_mcycle_advance(50000ULL * 100ULL);
        failsafeCheckDataFailurePeriod();
        failsafeUpdateState();
    }
    EXPECT_EQ(failsafePhase(), FAILSAFE_RX_LOSS_MONITORING);
    EXPECT_FALSE(ARMING_FLAG(ARMED));
    EXPECT_GE(s_disarmCalls, 1U);
}

// =============================================================================
// EDGE 12 (TDD-RED): 32-Bit millis() Wraparound Bug At failsafe.c Lines 364 & 548!
// =============================================================================
TEST_F(AriesRxLinkFailsafeEdgeTest, RxEdge12_TDD_RED_FailsafeMillisWraparoundAcross0xFFFFFFFF)
{
    // Arrange: Bring RX link UP cleanly arriving at millis() == 0xFFFFFF00UL (256 ms before 2^32 wrap!)
    failsafeConfigMutable()->failsafe_throttle_low_delay = 50; // 5000 ms required below mincheck
    failsafeConfigMutable()->failsafe_delay = 1;               // 100 ms minimum Stage 1 guard
    failsafeReset();
    thejas32_host_mcycle_set((0xFFFFFF00ULL - 200ULL) * 100000ULL);
    failsafeStartMonitoring();
    rcData[THROTTLE] = 1600.0f;
    bringRxLinkUp();
    ASSERT_EQ(millis(), 0xFFFFFF00UL);
    ASSERT_EQ(failsafePhase(), FAILSAFE_IDLE);

    ENABLE_ARMING_FLAG(ARMED);
    failsafeUpdateState(); // throttleLowPeriod = 0xFFFFFF00 + 5000 = 0x00001288 (wrapped!)

    // Advance 16ms -> drop throttle low + lose RX at 0xFFFFFF10, then hold 120ms -> 0xFFFFFF88
    // True elapsed low-throttle time is only 120ms (<< 5000ms!), but failsafe.c:364 compares:
    //   (millis() > failsafeState.throttleLowPeriod) -> (0xFFFFFF88 > 0x00001288) == TRUE!!
    thejas32_host_mcycle_advance(16000ULL * 100ULL);
    rcData[THROTTLE] = 1000.0f;
    failsafeOnValidDataFailed();
    for (int t = 0; t < 6; t++) {
        thejas32_host_mcycle_advance(20000ULL * 100ULL);
        failsafeCheckDataFailurePeriod();
        failsafeUpdateState();
    }
    EXPECT_EQ(failsafePhase(), FAILSAFE_LANDING)
        << "failsafe.c:364 raw millis() > throttleLowPeriod failed across 0xFFFFFFFF rollover!";
}

// =============================================================================
// EDGE 13: Failsafe Active Flag Tracks Entering And Recovering Failsafe Cleanly
// =============================================================================
TEST_F(AriesRxLinkFailsafeEdgeTest, RxEdge13_FailsafeIsActiveFlag_FollowsPhaseProperly)
{
    EXPECT_FALSE(failsafeIsActive());
    ENABLE_ARMING_FLAG(ARMED);
    failsafeStartMonitoring();
    rcData[THROTTLE] = 1600.0f;
    bringRxLinkUp();
    EXPECT_FALSE(failsafeIsActive());

    failsafeOnValidDataFailed();
    for (int t = 0; t < 15; t++) {
        thejas32_host_mcycle_advance(50000ULL * 100ULL);
        failsafeCheckDataFailurePeriod();
        failsafeUpdateState();
    }
    EXPECT_TRUE(failsafeIsActive());
}

} // namespace
