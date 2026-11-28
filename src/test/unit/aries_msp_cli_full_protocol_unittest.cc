/*
 * Test Suite: C-DAC ARIES v3.0 Full MSP + CLI Protocol Integration (Wave 2 - Suite 8)
 * Target Silicon: C-DAC THEJAS32 SoC (VEGA ET1031 RISC-V RV32IM @ 100 MHz, 248 KB SRAM, 0 RVC)
 * References & Citations:
 *   - PLAN/03_SERIAL_COMMUNICATION_MSP_AND_CLI.md
 *   - src/main/msp/msp.c, src/main/msp/msp_serial.c, src/main/cli/cli.c
 *   - src/platform/ARIES/serial_uart_aries.c
 *
 * Methodology:
 *   Strict Test-Driven Development (TDD) - Step 1: Living Documentation.
 *   Links REAL msp.c (--gc-sections), real msp_serial.c, and real serial_uart_aries.c
 *   to verify end-to-end Configurator MSP_API_VERSION / MSP_BOARD_INFO / MSP_STATUS /
 *   MSP_EEPROM_WRITE armed refusal over UART0 16550 hardware registers, plus real CLI
 *   invocation handoff via '#' and '\r'.
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <vector>

#include "unittest_macros.h"
#include "gtest/gtest.h"

extern "C" {
    #include "../main/platform.h"
    #include "build/build_config.h"
    #include "build/debug.h"
    #include "build/version.h"
    #include "cli/cli.h"
    #include "cli/settings.h"
    #include "common/crc.h"
    #include "common/maths.h"
    #include "common/sensor_alignment.h"
    #include "common/streambuf.h"
    #include "config/config.h"
    #include "config/feature.h"
    #include "config/simplified_tuning.h"
    #include "drivers/bus_spi.h"
    #include "drivers/io.h"
    #include "drivers/motor.h"
    #include "drivers/serial.h"
    #include "drivers/serial_uart.h"
    #include "drivers/serial_uart_impl.h"
    #include "drivers/system.h"
    #include "drivers/time.h"
    #include "fc/board_info.h"
    #include "fc/controlrate_profile.h"
    #include "fc/core.h"
    #include "fc/rc_adjustments.h"
    #include "fc/rc_controls.h"
    #include "fc/runtime_config.h"
    #include "flight/failsafe.h"
    #include "flight/imu.h"
    #include "flight/mixer.h"
    #include "flight/mixer_init.h"
    #include "flight/pid.h"
    #include "flight/pid_init.h"
    #include "flight/position.h"
    #include "io/serial.h"
    #include "io/serial_feature_map.h"
    #include "msp/msp.h"
    #include "msp/msp_box.h"
    #include "msp/msp_build_info.h"
    #include "msp/msp_protocol.h"
    #include "msp/msp_protocol_v2_common.h"
    #include "msp/msp_serial.h"
    #include "pg/gyrodev.h"
    #include "pg/msp.h"
    #include "pg/motor.h"
    #include "pg/pg.h"
    #include "pg/pg_ids.h"
    #include "pg/pilot.h"
    #include "pg/rx.h"
    #include "rx/rx.h"
    #include "rx/rx_bind.h"
    #include "scheduler/scheduler.h"
    #include "sensors/acceleration.h"
    #include "sensors/battery.h"
    #include "sensors/boardalignment.h"
    #include "sensors/current.h"
    #include "sensors/gyro.h"
    #include "sensors/gyro_init.h"
    #include "sensors/sensors.h"
    #include "sensors/voltage.h"
    #include "platform/ARIES/io_def_generated.h"
    #include "platform/ARIES/include/platform/bsp_thejas32.h"
    #include "aries_host_mmio.h"

    void thejas32_uart0_isr_proxy(void);
    void uartPollRx(void);
    void uartPollTx(void);
    uint32_t handle_trap(uint32_t mcause, uint32_t mepc, uint32_t *sp);
    mspResult_e mspFcProcessCommand(mspDescriptor_t srcDesc, mspPacket_t *cmd, mspPacket_t *reply, mspPostProcessFnPtr *mspPostProcessFn);
    void mspFcProcessReply(mspPacket_t *reply);

    // Minimal stubs required by real msp.c (--gc-sections) & msp_serial.c
    int16_t debug[DEBUG16_VALUE_COUNT];
    uint8_t debugMode = 0;

    static pidProfile_t s_pidProf;
    static controlRateConfig_t s_rateProf;
    static batteryProfile_t s_batProf;
    pidProfile_t *currentPidProfile = &s_pidProf;
    controlRateConfig_t *currentControlRateProfile = &s_rateProf;
    const batteryProfile_t *currentBatteryProfile = &s_batProf;
    uint8_t detectedSensors[SENSOR_INDEX_COUNT] = { 0 };
    uint8_t detectedGyros[GYRO_COUNT] = { 0 };
    rssiSource_e rssiSource = (rssiSource_e)0;
    rxRuntimeState_t rxRuntimeState;
    gyro_t gyro;
    acc_t acc;
    attitudeEulerAngles_t attitude;
    quaternion_t imuAttitudeQuaternion = QUATERNION_INITIALIZE;
    float motor[MAX_SUPPORTED_MOTORS];
    float motor_disarmed[MAX_SUPPORTED_MOTORS];
    const uint32_t baudRates[BAUD_COUNT] = { 0, 9600, 19200, 38400, 57600, 115200, 230400, 250000, 400000, 460800, 500000, 921600 };
    const serialPortIdentifier_e serialPortIdentifiers[SERIAL_PORT_COUNT] = { SERIAL_PORT_UART0 };
    const lookupTableEntry_t lookupTables[] = { { nullptr, 0 } };
    const char pidNames[] = "ROLL\0PITCH\0YAW\0LEVEL\0";
    const uint8_t voltageMeterADCtoIDMap[MAX_VOLTAGE_SENSOR_ADC] = { 0 };
    const uint8_t supportedVoltageMeterCount = 0;
    const uint8_t voltageMeterIds[] = { 0 };
    const uint8_t supportedCurrentMeterCount = 0;
    const uint8_t currentMeterIds[] = { 0 };
    const char * const buildKey = "TEST";
    const char * const releaseName = "TEST";
    const char * const buildDate = __DATE__;
    const char * const buildTime = __TIME__;
    const char * const shortGitRevision = "0000000";
    const char * const targetName = TARGET_BOARD_IDENTIFIER;

    static serialPort_t *s_boundUart0 = nullptr;
    static uint32_t s_eepromWrites = 0U;
    static uint32_t s_cliEntries = 0U;
    static bool s_cliInteractive = false;

    PG_REGISTER(mspConfig_t, mspConfig, PG_MSP_CONFIG, 0);
    PG_REGISTER(serialConfig_t, serialConfig, PG_SERIAL_CONFIG, 0);
    PG_REGISTER(systemConfig_t, systemConfig, PG_SYSTEM_CONFIG, 0);
    PG_REGISTER(pilotConfig_t, pilotConfig, PG_PILOT_CONFIG, 0);
    PG_REGISTER(boardAlignment_t, boardAlignment, PG_BOARD_ALIGNMENT, 0);
    PG_REGISTER(armingConfig_t, armingConfig, PG_ARMING_CONFIG, 0);
    PG_REGISTER(motorConfig_t, motorConfig, PG_MOTOR_CONFIG, 0);
    PG_REGISTER(featureConfig_t, featureConfig, PG_FEATURE_CONFIG, 0);
    PG_REGISTER(gyroConfig_t, gyroConfig, PG_GYRO_CONFIG, 0);
    PG_REGISTER(batteryConfig_t, batteryConfig, PG_BATTERY_CONFIG, 0);
    PG_REGISTER_ARRAY(controlRateConfig_t, CONTROL_RATE_PROFILE_COUNT, controlRateProfiles, PG_CONTROL_RATE_PROFILES, 0);
    PG_REGISTER_ARRAY(pidProfile_t, PID_PROFILE_COUNT, pidProfiles, PG_PID_PROFILE, 0);
    PG_REGISTER(pidConfig_t, pidConfig, PG_PID_CONFIG, 4);
    PG_REGISTER(accelerometerConfig_t, accelerometerConfig, PG_ACCELEROMETER_CONFIG, 0);
    PG_REGISTER(rcControlsConfig_t, rcControlsConfig, PG_RC_CONTROLS_CONFIG, 0);
    PG_REGISTER(flight3DConfig_t, flight3DConfig, PG_MOTOR_3D_CONFIG, 0);
    PG_REGISTER_ARRAY(modeActivationCondition_t, MAX_MODE_ACTIVATION_CONDITION_COUNT, modeActivationConditions, PG_MODE_ACTIVATION_PROFILE, 0);
    PG_REGISTER_ARRAY(adjustmentRange_t, MAX_ADJUSTMENT_RANGE_COUNT, adjustmentRanges, PG_ADJUSTMENT_RANGE_CONFIG, 0);
    PG_REGISTER(failsafeConfig_t, failsafeConfig, PG_FAILSAFE_CONFIG, 0);
    PG_REGISTER(imuConfig_t, imuConfig, PG_IMU_CONFIG, 3);
    PG_REGISTER(mixerConfig_t, mixerConfig, PG_MIXER_CONFIG, 0);
    PG_REGISTER_ARRAY(gyroDeviceConfig_t, MAX_GYRODEV_COUNT, gyroDeviceConfig, PG_GYRO_DEVICE_CONFIG, 1);
    PG_REGISTER(rxConfig_t, rxConfig, PG_RX_CONFIG, 0);
    PG_REGISTER_ARRAY(rxFailsafeChannelConfig_t, MAX_SUPPORTED_RC_CHANNEL_COUNT, rxFailsafeChannelConfigs, PG_RX_FAILSAFE_CHANNEL_CONFIG, 0);
    PG_REGISTER(currentSensorADCConfig_t, currentSensorADCConfig, PG_CURRENT_SENSOR_ADC_CONFIG, 0);
    PG_REGISTER_ARRAY(voltageSensorADCConfig_t, MAX_VOLTAGE_SENSOR_ADC, voltageSensorADCConfig, PG_VOLTAGE_SENSOR_ADC_CONFIG, 0);
    PG_REGISTER_ARRAY(batteryProfile_t, BATTERY_PROFILE_COUNT, batteryProfiles, PG_BATTERY_PROFILES, 1);

    float rcData[MAX_SUPPORTED_RC_CHANNEL_COUNT];
    void setRebootRequired(void) {}
    void motorShutdown(void) {}

    serialPort_t *openSerialPort(serialPortIdentifier_e id, serialPortFunction_e,
                                 serialReceiveCallbackPtr cb, void *cbData,
                                 uint32_t baudRate, portMode_e mode, portOptions_e options) {
        if (id == SERIAL_PORT_UART0 || id == SERIAL_PORT_USB_VCP) {
            s_boundUart0 = uartOpen(SERIAL_PORT_UART0, cb, cbData, baudRate, mode, options);
            return s_boundUart0;
        }
        return nullptr;
    }

    void closeSerialPort(serialPort_t *) {}
    void waitForSerialPortToFinishTransmitting(serialPort_t *port) {
        for (int i = 0; i < 64 && port && !isSerialTransmitBufferEmpty(port); i++) {
            THEJAS32_UART(0).UART_LSR = 0x60U;
            uartPollTx();
        }
    }

    bool isSerialPortShared(serialPortIdentifier_e, uint16_t, serialPortFunction_e) { return false; }
    bool serialIsPortAvailable(serialPortIdentifier_e id) { return id == SERIAL_PORT_UART0; }
    uint8_t serialSynthesizePortBaud(serialPortIdentifier_e, serialBaudClass_e) { return BAUD_115200; }
    uint8_t serialDefaultPortBaud(serialBaudClass_e) { return BAUD_115200; }
    unsigned serialImpliedMspPorts(serialPortIdentifier_e *, unsigned) { return 0U; }
    uint32_t serialSynthesizeFunctionMask(serialPortIdentifier_e) { return FUNCTION_MSP; }
    serialPortIdentifier_e displayPortMspGetSerial(void) { return SERIAL_PORT_NONE; }

    void cliEnter(serialPort_t *, bool interactive) { s_cliEntries++; s_cliInteractive = interactive; }
    bool cliProcess(void) { return false; }
    bool cliSetSettingByName(const char *) { return false; }
    int cliGetSettingByName(const char *, char *, int) { return -1; }
    int cliGetSettingInfoByName(const char *, int, char *, int, int *) { return -1; }
    void writeEEPROM(void) { s_eepromWrites++; }
    bool readEEPROM(void) { return true; }
    void writeReadEeprom(void *) { s_eepromWrites++; }
    bool resetEEPROM(void) { return true; }
    bool getRebootRequired(void) { return false; }
    void schedulerIgnoreTaskExecTime(void) {}
    void schedulerIgnoreTaskStateTime(void) {}
    timeDelta_t getTaskDeltaTimeUs(taskId_e) { return 1000; }
    bool featureIsEnabled(uint32_t) { return false; }
    bool featureIsConfigured(uint32_t) { return false; }
    void featureEnableImmediate(uint32_t) {}
    void featureDisableImmediate(uint32_t) {}
    void featureConfigReplace(uint32_t) {}
    uint32_t featureMask(void) { return 0U; }
    void beeperConfirmationBeeps(uint8_t) {}
    void systemBeep(bool) {}
    void disarm(flightLogDisarmReason_e) { DISABLE_ARMING_FLAG(ARMED); }
    uint16_t getAverageSystemLoadPercent(void) { return 30U; }
    uint32_t getSysTickExeTimeUs(void) { return 300U; }
    void getTargetName(char *dst, int maxLen) { strncpy(dst, TARGET_BOARD_IDENTIFIER, (size_t)maxLen); }
    const char *getMcuTypeName(void) { return "THEJAS32"; }
    const char *getBoardName(void) { return "ARIESV3"; }
    const char *getManufacturerId(void) { return "CDAC"; }
    bool boardInformationIsSet(void) { return true; }
    bool setBoardName(const char *) { return true; }
    bool setManufacturerId(const char *) { return true; }
    bool persistBoardInformation(void) { return true; }
    void sbufWriteBuildInfoFlags(sbuf_t *dst) { sbufWriteU16(dst, 0U); }
    uint8_t getCurrentPidProfileIndex(void) { return 0U; }
    uint8_t getCurrentControlRateProfileIndex(void) { return 0U; }
    uint8_t getCurrentBatteryProfileIndex(void) { return 0U; }
    void changeBatteryProfile(uint8_t) {}
    void changePidProfile(uint8_t) {}
    void changeControlRateProfile(uint8_t) {}
    void pidCopyProfile(uint8_t, uint8_t) {}
    void copyControlRateProfile(uint8_t, uint8_t) {}
    void resetPidProfile(pidProfile_t *) {}
    void pidInitConfig(const pidProfile_t *) {}
    void pidInitFilters(const pidProfile_t *) {}
    void applySimplifiedTuning(pidProfile_t *, gyroConfig_t *) {}
    void applySimplifiedTuningPids(pidProfile_t *) {}
    void applySimplifiedTuningDtermFilters(pidProfile_t *) {}
    void applySimplifiedTuningGyroFilters(gyroConfig_t *) {}
    void initEscEndpoints(void) {}
    void mixerInitProfile(void) {}
    uint8_t getMotorCount(void) { return 4U; }
    bool motorIsEnabled(void) { return true; }
    bool motorIsMotorEnabled(unsigned) { return true; }
    uint16_t motorConvertToExternal(float v) { return (uint16_t)v; }
    float motorConvertFromExternal(uint16_t v) { return (float)v; }
    bool checkMotorProtocolEnabled(const motorDevConfig_t *, bool *) { return true; }
    uint8_t spiGetRegisteredDeviceCount(void) { return 2U; }
    void accStartCalibration(void) {}
    bool accHasBeenCalibrated(void) { return true; }
    void validateAndFixGyroConfig(void) {}
    void gyroInitFilters(void) {}
    int16_t gyroRateDps(int) { return 0; }
    int firstEnabledGyro(void) { return 0; }
    uint8_t getGyroDetectedFlags(void) { return 0U; }
    int32_t getEstimatedAltitudeCm(void) { return 0; }
    uint16_t getLegacyBatteryVoltage(void) { return 0U; }
    uint16_t getBatteryVoltage(void) { return 0U; }
    uint8_t getBatteryCellCount(void) { return 0U; }
    batteryState_e getBatteryState(void) { return BATTERY_NOT_PRESENT; }
    int32_t getMAhDrawn(void) { return 0; }
    int32_t getAmperage(void) { return 0; }
    uint16_t getRssi(void) { return 0U; }
    void setRssiMsp(uint8_t) {}
    bool getRxBindSupported(void) { return false; }
    bool startRxBind(void) { return false; }
    void rcControlsInit(void) {}
    void initRcProcessing(void) {}
    void activeAdjustmentRangeReset(void) {}
    void voltageMeterRead(voltageMeterId_e, voltageMeter_t *m) { memset(m, 0, sizeof(*m)); }
    void currentMeterRead(currentMeterId_e, currentMeter_t *m) { memset(m, 0, sizeof(*m)); }
    const box_t *findBoxByBoxId(boxId_e) { return nullptr; }
    const box_t *findBoxByPermanentId(uint8_t) { return nullptr; }
    int packFlightModeFlags(struct boxBitmask_s *) { return 0; }
    int serializeBoxNameFn(struct sbuf_s *, const box_t *) { return 0; }
    int serializeBoxPermanentIdFn(struct sbuf_s *, const box_t *) { return 0; }
    void serializeBoxReply(struct sbuf_s *, int, serializeBoxFn *) {}
}

namespace {

static std::vector<uint8_t> buildMspV1(uint8_t cmd, const std::vector<uint8_t> &payload = {})
{
    std::vector<uint8_t> f = { '$', 'M', '<', (uint8_t)payload.size(), cmd };
    uint8_t cks = (uint8_t)(payload.size() ^ cmd);
    for (uint8_t b : payload) { f.push_back(b); cks ^= b; }
    f.push_back(cks);
    return f;
}

class AriesMspCliFullProtocolTest : public ::testing::Test {
protected:
    void SetUp() override {
        thejas32_host_reset_all();
        IOInitGlobal();
        pgResetAll();
        s_boundUart0 = nullptr;
        s_eepromWrites = 0U;
        s_cliEntries = 0U;
        s_cliInteractive = false;
        armingFlags = 0;

        serialPinConfig_t pinCfg = {};
        pinCfg.ioTagTx[0] = DEFIO_TAG_E__PA30;
        pinCfg.ioTagRx[0] = DEFIO_TAG_E__PA31;
        uartPinConfigure(&pinCfg);

        mspConfigMutable()->msp_uart[0] = SERIAL_PORT_UART0;
        serialConfigMutable()->reboot_character = 'R';
        thejas32_host_mcycle_set(1000000ULL);
        mspSerialInit();
    }

    void pushRxAndPumpUart0(const std::vector<uint8_t> &bytes, std::vector<uint8_t> &txOut) {
        ASSERT_NE(s_boundUart0, nullptr);
        for (uint8_t b : bytes) {
            THEJAS32_UART(0).UART_DR = b;
            THEJAS32_UART(0).UART_LSR = 0x01U; // DR ready only; hold THRE=0 so full MSP reply stays in TX ring first
            THEJAS32_INTR_CTRL.INTR_STATUS = (1UL << THEJAS32_IRQ_UART0);
            handle_trap(0x8000000BUL, 0U, nullptr);
        }
        while (serialRxBytesWaiting(s_boundUart0)) {
            mspSerialProcess(MSP_EVALUATE_NON_MSP_DATA, mspFcProcessCommand, mspFcProcessReply);
        }
        // Snapshot exact encoded wire bytes in UART0 SW TX ring before bounded polled drain
        uint32_t tail = s_boundUart0->txBufferTail;
        const uint32_t head = s_boundUart0->txBufferHead;
        const uint32_t cap = s_boundUart0->txBufferSize;
        while (tail != head) {
            txOut.push_back((uint8_t)s_boundUart0->txBuffer[tail]);
            tail = (tail + 1U) % cap;
        }
        for (int i = 0; i < 16 && !isSerialTransmitBufferEmpty(s_boundUart0); i++) {
            THEJAS32_UART(0).UART_LSR = 0x60U;
            uartPollTx();
        }
        EXPECT_TRUE(isSerialTransmitBufferEmpty(s_boundUart0));
        if (!txOut.empty()) {
            EXPECT_EQ((uint8_t)(THEJAS32_UART(0).UART_DR & 0xFFU), txOut.back());
        }
    }
};

// =============================================================================
// TEST 01: Real msp.c MSP_API_VERSION (1) Over UART0 Ring & Bounded TX Drain
// =============================================================================
TEST_F(AriesMspCliFullProtocolTest, Test_RealMspC_ApiVersionAndFcVariantAndBoardInfoOverUart0)
{
    std::vector<uint8_t> tx;
    pushRxAndPumpUart0(buildMspV1(MSP_API_VERSION), tx);
    ASSERT_GE(tx.size(), 9U);
    EXPECT_EQ(tx[0], '$');
    EXPECT_EQ(tx[1], 'M');
    EXPECT_EQ(tx[2], '>');
    EXPECT_EQ(tx[3], 3U);
    EXPECT_EQ(tx[4], MSP_API_VERSION);
    EXPECT_EQ(tx[5], MSP_PROTOCOL_VERSION);
    EXPECT_EQ(tx[6], API_VERSION_MAJOR);
    EXPECT_EQ(tx[7], API_VERSION_MINOR);

    tx.clear();
    pushRxAndPumpUart0(buildMspV1(MSP_FC_VARIANT), tx);
    ASSERT_GE(tx.size(), 10U);
    EXPECT_EQ(memcmp(&tx[5], "BTFL", 4U), 0);
}

// =============================================================================
// TEST 02: Real msp.c MSP_EEPROM_WRITE (250) Refused While Armed, Succeeded Disarmed
// =============================================================================
TEST_F(AriesMspCliFullProtocolTest, Test_RealMspC_EepromWriteRefusedWhileArmed_AndAllowedDisarmed)
{
    ENABLE_ARMING_FLAG(ARMED);
    std::vector<uint8_t> txArmed;
    pushRxAndPumpUart0(buildMspV1(MSP_EEPROM_WRITE), txArmed);
    EXPECT_EQ(s_eepromWrites, 0U);
    ASSERT_GE(txArmed.size(), 6U);
    EXPECT_EQ(txArmed[2], '!');

    DISABLE_ARMING_FLAG(ARMED);
    std::vector<uint8_t> txDisarmed;
    pushRxAndPumpUart0(buildMspV1(MSP_EEPROM_WRITE), txDisarmed);
    EXPECT_EQ(s_eepromWrites, 1U);
    ASSERT_GE(txDisarmed.size(), 6U);
    EXPECT_EQ(txDisarmed[2], '>');
}

// =============================================================================
// TEST 03: Real msp.c MSP_UID (160) Returns 12 Unique Deterministic Bytes
// =============================================================================
TEST_F(AriesMspCliFullProtocolTest, Test_RealMspC_UidCommandReturns12NonZeroSiliconBytes)
{
    std::vector<uint8_t> tx;
    pushRxAndPumpUart0(buildMspV1(MSP_UID), tx);
    ASSERT_EQ(tx.size(), 18U);
    EXPECT_EQ(tx[2], '>');
    EXPECT_EQ(tx[3], 12U);
    EXPECT_EQ(tx[4], MSP_UID);
}

// =============================================================================
// TEST 04: Real msp.c MSP_BOARD_INFO (4) Encodes ARIESV3 Identifier Safely
// =============================================================================
TEST_F(AriesMspCliFullProtocolTest, Test_RealMspC_BoardInfoIdentifiesAriesV3WithoutOverflow)
{
    std::vector<uint8_t> tx;
    pushRxAndPumpUart0(buildMspV1(MSP_BOARD_INFO), tx);
    ASSERT_GE(tx.size(), 12U);
    EXPECT_EQ(tx[2], '>');
    EXPECT_EQ(tx[4], MSP_BOARD_INFO);
}

// =============================================================================
// TEST 05: Real msp_serial.c Immediate CLI '#' & '\r' Entry Via UART0 ISR
// =============================================================================
TEST_F(AriesMspCliFullProtocolTest, Test_RealMspSerial_ImmediateCliHashViaUart0IsrEntersInteractiveCli)
{
    std::vector<uint8_t> tx;
    pushRxAndPumpUart0({ '#' }, tx);
    EXPECT_EQ(s_cliEntries, 1U);
    EXPECT_TRUE(s_cliInteractive);
}

// =============================================================================
// TEST 06: Reboot Character 'R' Does Not Prematurely Trigger Before 100 ms Guard
// =============================================================================
TEST_F(AriesMspCliFullProtocolTest, Test_RealMspSerial_RebootCharObeys100msQuietGuardBeforeReset)
{
    THEJAS32_INTR_CTRL.INTR_EN = 0xFFFFFFFFUL;
    thejas32_host_mstatus = 0x8U;

    std::vector<uint8_t> tx;
    pushRxAndPumpUart0({ 'R' }, tx);
    // Before 100 ms guard elapses, ROM bootloader halt must NOT trigger yet
    EXPECT_NE(THEJAS32_INTR_CTRL.INTR_EN, 0U);
    EXPECT_NE(thejas32_host_mstatus & 0x8U, 0U);

    thejas32_host_mcycle_advance(120000ULL * 100ULL);
    mspSerialProcess(MSP_EVALUATE_NON_MSP_DATA, mspFcProcessCommand, mspFcProcessReply);
    // After 100 ms guard, MSP_PENDING_BOOTLOADER_ROM calls systemResetToBootloader(BOOTLOADER_REQUEST_ROM)
    EXPECT_EQ(THEJAS32_INTR_CTRL.INTR_EN, 0U);
    EXPECT_EQ(thejas32_host_mstatus & 0x8U, 0U);
}

} // namespace
