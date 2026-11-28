/*
 * Test Suite: C-DAC ARIES v3.0 MSP + CLI Protocol Edge Cases (Wave 3 - Suite 12)
 * Target Silicon: C-DAC THEJAS32 SoC (VEGA ET1031 RISC-V RV32IM @ 100 MHz, 248 KB SRAM, 0 RVC)
 * References & Citations:
 *   - PLAN/03_SERIAL_COMMUNICATION_MSP_AND_CLI.md
 *   - src/main/msp/msp.c, src/main/msp/msp_serial.c, src/main/cli/cli.c
 *   - src/main/drivers/serial.c, src/main/drivers/buf_writer.c, src/platform/ARIES/serial_uart_aries.c
 *
 * Methodology:
 *   Strict Test-Driven Development (TDD) - Step 1: Living Documentation.
 *   Links REAL msp.c (--gc-sections), real msp_serial.c, real cli.c, real buf_writer.c,
 *   real serial.c, and real serial_uart_aries.c over UART0/UART1 host MMIO registers.
 *   Tests 12 protocol boundary, framing, resync, isolation, armed-lockout, and TX drain edges.
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
    #include "build/version.h"
    #include "cli/cli.h"
    #include "cli/settings.h"
    #include "common/crc.h"
    #include "common/maths.h"
    #include "common/sensor_alignment.h"
    #include "common/streambuf.h"
    #include "common/time.h"
    #include "config/config.h"
    #include "config/feature.h"
    #include "config/simplified_tuning.h"
    #include "drivers/buf_writer.h"
    #include "drivers/bus_spi.h"
    #include "drivers/display.h"
    #include "drivers/dma.h"
    #include "drivers/io.h"
    #include "drivers/motor.h"
    #include "drivers/serial.h"
    #include "drivers/serial_uart.h"
    #include "drivers/serial_uart_impl.h"
    #include "drivers/sound_beeper.h"
    #include "drivers/system.h"
    #include "drivers/time.h"
    #include "fc/board_info.h"
    #include "fc/controlrate_profile.h"
    #include "fc/core.h"
    #include "fc/rc.h"
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
    #include "io/beeper.h"
    #include "io/gps.h"
    #include "io/ledstrip.h"
    #include "io/serial.h"
    #include "io/serial_feature_map.h"
    #include "msp/msp.h"
    #include "msp/msp_box.h"
    #include "msp/msp_build_info.h"
    #include "msp/msp_protocol.h"
    #include "msp/msp_protocol_v2_common.h"
    #include "msp/msp_serial.h"
    #include "osd/osd.h"
    #include "pg/beeper.h"
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

    void uartPollRx(void);
    void uartPollTx(void);
    uint32_t handle_trap(uint32_t mcause, uint32_t mepc, uint32_t *sp);

    // -------------------------------------------------------------------------
    // Minimal CLI setting table & globals required by real cli.c + msp.c (--gc-sections)
    // -------------------------------------------------------------------------
    PG_REGISTER_ARRAY(int8_t, 3, edgeUnitData, PG_RESERVED_FOR_TESTING_1, 0);

    const clivalue_t valueTable[] = {
        { .name = "edge_array_test", .type = VAR_INT8 | MODE_ARRAY | MASTER_VALUE, .config = { .array = { .length = 3 } }, .pgn = PG_RESERVED_FOR_TESTING_1, .offset = 0 },
    };
    const uint16_t valueTableEntryCount = ARRAYLEN(valueTable);
    const lookupTableEntry_t lookupTables[] = { { nullptr, 0 } };
    static const char *stubIdentName(int) { return "NONE"; }
    static bool stubIdentValue(const char *, int *v) { *v = SERIAL_PORT_NONE; return true; }
    static const char *stubIdentAt(unsigned idx) { return idx == 0 ? "NONE" : nullptr; }
    const identifierLookupEntry_t identifierLookups[IDENTIFIER_LOOKUP_COUNT] = {
        [IDENTIFIER_LOOKUP_SERIAL_PORT] = { stubIdentName, stubIdentValue, stubIdentAt },
    };
    const char * const lookupTableOsdDisplayPortDevice[] = { "NONE" };

    int16_t debug[DEBUG16_VALUE_COUNT];
    uint8_t debugMode = 0;
    int32_t schedLoopStartCycles = 0;
    int32_t taskGuardCycles = 0;
    uint16_t averageSystemLoadPercent = 0;
    uint16_t currentRxIntervalUs = 9000;
    uint16_t batteryWarningVoltage = 0;

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
    float rcData[MAX_SUPPORTED_RC_CHANNEL_COUNT];
    const uint32_t baudRates[BAUD_COUNT] = { 0, 9600, 19200, 38400, 57600, 115200, 230400, 250000, 400000, 460800, 500000, 921600 };
    const serialPortIdentifier_e serialPortIdentifiers[SERIAL_PORT_COUNT] = { SERIAL_PORT_UART0, SERIAL_PORT_USART1 };
    const char pidNames[] = "ROLL\0PITCH\0YAW\0LEVEL\0";
    const char rcChannelLetters[] = "AERT12345678abcdefgh";
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

    PG_REGISTER(mspConfig_t, mspConfig, PG_MSP_CONFIG, 0);
    PG_REGISTER(serialConfig_t, serialConfig, PG_SERIAL_CONFIG, 0);
    PG_REGISTER(systemConfig_t, systemConfig, PG_SYSTEM_CONFIG, 0);
    PG_REGISTER(pilotConfig_t, pilotConfig, PG_PILOT_CONFIG, 0);
    PG_REGISTER(boardAlignment_t, boardAlignment, PG_BOARD_ALIGNMENT, 0);
    PG_REGISTER(armingConfig_t, armingConfig, PG_ARMING_CONFIG, 0);
    PG_REGISTER(motorConfig_t, motorConfig, PG_MOTOR_CONFIG, 0);
    PG_REGISTER_ARRAY(motorMixer_t, MAX_SUPPORTED_MOTORS, customMotorMixer, PG_MOTOR_MIXER, 0);
    PG_REGISTER(gyroConfig_t, gyroConfig, PG_GYRO_CONFIG, 0);
    PG_REGISTER(batteryConfig_t, batteryConfig, PG_BATTERY_CONFIG, 0);
    PG_REGISTER(beeperConfig_t, beeperConfig, PG_BEEPER_CONFIG, 0);
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
    PG_REGISTER_ARRAY(rxChannelRangeConfig_t, NON_AUX_CHANNEL_COUNT, rxChannelRangeConfigs, PG_RX_CHANNEL_RANGE_CONFIG, 0);
    PG_REGISTER_ARRAY(rxFailsafeChannelConfig_t, MAX_SUPPORTED_RC_CHANNEL_COUNT, rxFailsafeChannelConfigs, PG_RX_FAILSAFE_CHANNEL_CONFIG, 0);
    PG_REGISTER(currentSensorADCConfig_t, currentSensorADCConfig, PG_CURRENT_SENSOR_ADC_CONFIG, 0);
    PG_REGISTER_ARRAY(voltageSensorADCConfig_t, MAX_VOLTAGE_SENSOR_ADC, voltageSensorADCConfig, PG_VOLTAGE_SENSOR_ADC_CONFIG, 0);
    PG_REGISTER_ARRAY(batteryProfile_t, BATTERY_PROFILE_COUNT, batteryProfiles, PG_BATTERY_PROFILES, 1);

    // -------------------------------------------------------------------------
    // Test harness controllable serial & TX backpressure state
    // -------------------------------------------------------------------------
    static serialPort_t *s_boundUart0 = nullptr;
    static serialPort_t *s_boundUart1 = nullptr;
    static uint32_t s_uart0FuncMask = FUNCTION_MSP;
    static serialPortIdentifier_e s_displayPortSerial = SERIAL_PORT_NONE;
    static uint32_t s_eepromWrites = 0U;
    static uint32_t s_mixerResetCount = 0U;
    static bool s_captureBackpressureWire = false;
    static uint32_t s_txBackpressureStalls = 0U;
    static uint32_t s_waitTxPolls = 0U;
    static uint32_t s_waitTxBusyPollsBeforeReady = 0U;
    static std::vector<uint8_t> s_capturedBackpressureBytes;

    static void captureNext16FromUart0Ring(void) {
        if (!s_boundUart0) return;
        uint32_t tail = s_boundUart0->txBufferTail;
        const uint32_t head = s_boundUart0->txBufferHead;
        const uint32_t cap = s_boundUart0->txBufferSize;
        for (int n = 0; n < 16 && tail != head; n++) {
            s_capturedBackpressureBytes.push_back((uint8_t)s_boundUart0->txBuffer[tail]);
            tail = (tail + 1U) % cap;
        }
    }

    serialPort_t *openSerialPort(serialPortIdentifier_e id, serialPortFunction_e,
                                 serialReceiveCallbackPtr cb, void *cbData,
                                 uint32_t baudRate, portMode_e mode, portOptions_e options) {
        if (id == SERIAL_PORT_UART0 || id == SERIAL_PORT_USB_VCP) {
            s_boundUart0 = uartOpen(SERIAL_PORT_UART0, cb, cbData, baudRate, mode, options);
            return s_boundUart0;
        }
        if (id == SERIAL_PORT_USART1) {
            s_boundUart1 = uartOpen(SERIAL_PORT_USART1, cb, cbData, baudRate, mode, options);
            return s_boundUart1;
        }
        return nullptr;
    }

    void closeSerialPort(serialPort_t *) {}

    void waitForSerialPortToFinishTransmitting(serialPort_t *port) {
        while (port && !isSerialTransmitBufferEmpty(port)) {
            s_waitTxPolls++;
            if (s_waitTxPolls >= s_waitTxBusyPollsBeforeReady) {
                THEJAS32_UART(0).UART_LSR = 0x60U;
                THEJAS32_UART(1).UART_LSR = 0x60U;
            }
            if (s_captureBackpressureWire && port == s_boundUart0 && (THEJAS32_UART(0).UART_LSR & 0x20U)) {
                captureNext16FromUart0Ring();
            }
            uartPollTx();
            if (s_waitTxPolls > 2048U) {
                break;
            }
        }
    }

    void schedulerIgnoreTaskExecTime(void) {
        s_txBackpressureStalls++;
        if (s_captureBackpressureWire && s_boundUart0) {
            captureNext16FromUart0Ring();
            THEJAS32_UART(0).UART_LSR = 0x60U;
            uartPollTx();
            THEJAS32_UART(0).UART_LSR = 0x01U; // Re-arm THRE=0 so next chunk stays in ring before capture!
        } else {
            THEJAS32_UART(0).UART_LSR = 0x60U;
            THEJAS32_UART(1).UART_LSR = 0x60U;
        }
    }
    void schedulerIgnoreTaskStateTime(void) {}
    timeDelta_t getTaskDeltaTimeUs(taskId_e) { return 1000; }
    void rescheduleTask(taskId_e, timeDelta_t) {}
    void schedulerSetNextStateTime(timeDelta_t) {}
    void getTaskInfo(taskId_e, taskInfo_t *) {}
    void getCheckFuncInfo(cfCheckFuncInfo_t *) {}
    void schedulerResetTaskMaxExecutionTime(taskId_e) {}
    void schedulerResetCheckFunctionMaxExecutionTime(void) {}

    bool isSerialPortShared(serialPortIdentifier_e, uint16_t, serialPortFunction_e) { return false; }
    bool serialIsPortAvailable(serialPortIdentifier_e id) { return (id == SERIAL_PORT_UART0 || id == SERIAL_PORT_USART1); }
    int findSerialPortIndexByIdentifier(serialPortIdentifier_e id) {
        if (id == SERIAL_PORT_UART0) return 0;
        if (id == SERIAL_PORT_USART1) return 1;
        return -1;
    }
    serialPortUsage_t *findSerialPortUsageByIdentifier(serialPortIdentifier_e) { return nullptr; }
    uint8_t serialSynthesizePortBaud(serialPortIdentifier_e, serialBaudClass_e) { return BAUD_115200; }
    uint8_t serialDefaultPortBaud(serialBaudClass_e) { return BAUD_115200; }
    void serialApplyPortBaud(serialPortIdentifier_e, serialBaudClass_e, uint8_t) {}
    bool serialApplyFunctionMask(serialPortIdentifier_e, uint32_t) { return true; }
    unsigned serialGetPortClaims(serialPortIdentifier_e, serialPortClaim_t *, unsigned) { return 0U; }
    unsigned serialImpliedMspPorts(serialPortIdentifier_e *, unsigned) { return 0U; }
    uint32_t serialSynthesizeFunctionMask(serialPortIdentifier_e id) {
        return (id == SERIAL_PORT_UART0) ? s_uart0FuncMask : (uint32_t)FUNCTION_MSP;
    }
    const char *serialName(serialPortIdentifier_e, const char *notFound) { return notFound; }
    serialPortIdentifier_e findSerialPortByName(const char *, int (*)(const char *, const char *)) { return SERIAL_PORT_NONE; }
    baudRate_e lookupBaudRateIndex(uint32_t) { return BAUD_115200; }
    serialPortIdentifier_e displayPortMspGetSerial(void) { return s_displayPortSerial; }
    void setPrintfSerialPort(struct serialPort_s) {}

    void writeEEPROM(void) { s_eepromWrites++; }
    void writeUnmodifiedConfigToEEPROM(void) { s_eepromWrites++; }
    bool readEEPROM(void) { return true; }
    void writeReadEeprom(void *) { s_eepromWrites++; }
    bool resetEEPROM(void) { return true; }
    void resetConfig(void) {}
    size_t getEEPROMStorageSize(void) { return EEPROM_SIZE; }
    uint16_t getEEPROMConfigSize(void) { return 1024U; }
    uint8_t eepromData[EEPROM_SIZE];
    void setRebootRequired(void) {}
    bool getRebootRequired(void) { return false; }

    void motorShutdown(void) {}
    void mixerResetDisarmedMotors(void) { s_mixerResetCount++; }
    void mixerLoadMix(int, motorMixer_t *) {}
    void beeperConfirmationBeeps(uint8_t) {}
    void beeper(beeperMode_e) {}
    void beeperSilence(void) {}
    void beeperWarningBeeps(uint8_t) {}
    void beeperUpdate(timeUs_t) {}
    uint32_t getArmingBeepTimeMicros(void) { return 0; }
    beeperMode_e beeperModeForTableIndex(int) { return BEEPER_SILENCE; }
    uint32_t beeperModeMaskForTableIndex(int) { return 0; }
    const char *beeperNameForTableIndex(int) { return nullptr; }
    int beeperTableEntryCount(void) { return 0; }
    bool isBeeperOn(void) { return false; }
    void beeperOffSetAll(uint8_t) {}
    void setBeeperOffMask(uint32_t) {}
    void setPreferredBeeperOffMask(uint32_t) {}
    void beeperOffSet(uint32_t) {}
    void beeperOffClear(uint32_t) {}
    void beeperOffClearAll(void) {}
    uint32_t getBeeperOffMask(void) { return 0; }
    uint32_t getPreferredBeeperOffMask(void) { return 0; }
    uint8_t useHottAlarmSoundPeriod(void) { return 0; }
    void systemBeep(bool) {}
    void disarm(flightLogDisarmReason_e) { DISABLE_ARMING_FLAG(ARMED); }
    uint16_t getAverageSystemLoadPercent(void) { return 30U; }
    uint32_t getSysTickExeTimeUs(void) { return 300U; }
    uint32_t stackTotalSize(void) { return 0x2000U; }
    uint32_t stackHighMem(void) { return 0x0023FFF0U; }
    void getTargetName(char *dst, int maxLen) { strncpy(dst, TARGET_BOARD_IDENTIFIER, (size_t)maxLen); }
    const char *getMcuTypeName(void) { return "THEJAS32"; }
    const char *getBoardName(void) { return "ARIESV3"; }
    const char *getManufacturerId(void) { return "CDAC"; }
    bool boardInformationIsSet(void) { return true; }
    bool setBoardName(const char *) { return true; }
    bool setManufacturerId(const char *) { return true; }
    bool persistBoardInformation(void) { return true; }
    void sbufWriteBuildInfoFlags(sbuf_t *dst) { sbufWriteU16(dst, 0U); }
    const char * const *sensorHardwareNames(sensorIndex_e, int *c) { if (c) *c = 0; return nullptr; }
    sensorIndex_e sensorIndexFromName(const char *) { return SENSOR_INDEX_COUNT; }
    const char *sensorTypeName(sensorIndex_e) { return nullptr; }
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
    const char *getBatteryStateString(void) { return "NOT PRESENT"; }
    int32_t getMAhDrawn(void) { return 0; }
    int32_t getAmperage(void) { return 0; }
    uint16_t getRssi(void) { return 0U; }
    void setRssiMsp(uint8_t) {}
    float getCurrentRxRateHz(void) { return 0.0f; }
    bool getRxRateValid(void) { return false; }
    bool getRxBindSupported(void) { return false; }
    bool startRxBind(void) { return false; }
    void rcControlsInit(void) {}
    void initRcProcessing(void) {}
    void parseRcChannels(const char *, rxConfig_t *) {}
    void resetAllRxChannelRangeConfigurations(rxChannelRangeConfig_t *) {}
    void activeAdjustmentRangeReset(void) {}
    void analyzeModeActivationConditions(void) {}
    bool isModeActivationConditionConfigured(const modeActivationCondition_t *, const modeActivationCondition_t *) { return false; }
    void voltageMeterRead(voltageMeterId_e, voltageMeter_t *m) { memset(m, 0, sizeof(*m)); }
    void currentMeterRead(currentMeterId_e, currentMeter_t *m) { memset(m, 0, sizeof(*m)); }
    const box_t *findBoxByBoxId(boxId_e) { return nullptr; }
    const box_t *findBoxByPermanentId(uint8_t) { return nullptr; }
    int packFlightModeFlags(struct boxBitmask_s *) { return 0; }
    int serializeBoxNameFn(struct sbuf_s *, const box_t *) { return 0; }
    int serializeBoxPermanentIdFn(struct sbuf_s *, const box_t *) { return 0; }
    void serializeBoxReply(struct sbuf_s *, int, serializeBoxFn *) {}
    const uint16_t *getBuildOptions(unsigned *count) { *count = 0; return nullptr; }
    displayPort_t *osdGetDisplayPort(osdDisplayPortDevice_e *) { return nullptr; }
    void disableSimplifiedTuning(pidProfile_t *, gyroConfig_t *) {}
    uint8_t spiGetExtDeviceCount(const extDevice_t *) { return 0U; }
    rcSmoothingFilter_t *getRcSmoothingData(void) { return nullptr; }
    const char *getOwnerName(resourceOwner_e) { return "FREE"; }
    int resourceIndexToInput(resourceOwner_e, int index) { return index + 1; }
    int resourceInputToIndex(resourceOwner_e, int input) { return input - 1; }
    const resourceOwner_t *dmaGetOwner(dmaIdentifier_e) { return nullptr; }
}

namespace {

static std::vector<uint8_t> buildMspV1(uint8_t cmd, const std::vector<uint8_t> &payload = {}, bool corruptCks = false)
{
    std::vector<uint8_t> f = { '$', 'M', '<', (uint8_t)payload.size(), cmd };
    uint8_t cks = (uint8_t)(payload.size() ^ cmd);
    for (uint8_t b : payload) { f.push_back(b); cks ^= b; }
    if (corruptCks) cks ^= 0xFFU;
    f.push_back(cks);
    return f;
}

static std::vector<uint8_t> buildMspV2Native(uint16_t cmd, const std::vector<uint8_t> &payload = {}, bool corruptCrc = false)
{
    std::vector<uint8_t> f = {
        '$', 'X', '<', 0U,
        (uint8_t)(cmd & 0xFFU), (uint8_t)(cmd >> 8),
        (uint8_t)(payload.size() & 0xFFU), (uint8_t)(payload.size() >> 8)
    };
    uint8_t crc = 0U;
    for (size_t i = 3; i < f.size(); i++) crc = crc8_dvb_s2(crc, f[i]);
    for (uint8_t b : payload) { f.push_back(b); crc = crc8_dvb_s2(crc, b); }
    if (corruptCrc) crc ^= 0xA5U;
    f.push_back(crc);
    return f;
}

static std::vector<uint8_t> buildMspV2OverV1(uint16_t cmd, const std::vector<uint8_t> &payload = {}, bool corruptV2Crc = false)
{
    const uint8_t v2HeaderAndData[5] = {
        0U,
        (uint8_t)(cmd & 0xFFU), (uint8_t)(cmd >> 8),
        (uint8_t)(payload.size() & 0xFFU), (uint8_t)(payload.size() >> 8)
    };
    uint8_t crc2 = crc8_dvb_s2_update(0U, v2HeaderAndData, sizeof(v2HeaderAndData));
    crc2 = crc8_dvb_s2_update(crc2, payload.empty() ? nullptr : payload.data(), payload.size());
    if (corruptV2Crc) crc2 ^= 0x55U;

    const uint8_t v1Size = (uint8_t)(sizeof(v2HeaderAndData) + payload.size() + 1U);
    std::vector<uint8_t> f = { '$', 'M', '<', v1Size, MSP_V2_FRAME_ID };
    for (uint8_t b : v2HeaderAndData) f.push_back(b);
    for (uint8_t b : payload) f.push_back(b);
    f.push_back(crc2);

    uint8_t crc1 = 0U;
    for (size_t i = 3; i < f.size(); i++) crc1 ^= f[i];
    f.push_back(crc1);
    return f;
}

static uint32_t s_customCmdCalls = 0U;
static uint16_t s_lastCustomCmd = 0U;
static size_t s_lastCustomSize = 0U;
static uint8_t s_lastCustomFirstByte = 0U;
static uint8_t s_lastCustomLastByte = 0U;

static mspResult_e spyMspCommand(mspDescriptor_t srcDesc, mspPacket_t *cmd, mspPacket_t *reply, mspPostProcessFnPtr *pp)
{
    s_customCmdCalls++;
    s_lastCustomCmd = (uint16_t)cmd->cmd;
    s_lastCustomSize = (size_t)sbufBytesRemaining(&cmd->buf);
    if (s_lastCustomSize > 0U) {
        const uint8_t *p = sbufPtr(&cmd->buf);
        s_lastCustomFirstByte = p[0];
        s_lastCustomLastByte  = p[s_lastCustomSize - 1U];
    }
    if (cmd->cmd == 240U) {
        sbufWriteU8(reply->buf.ptr == reply->buf.end ? &reply->buf : &reply->buf, 0xABU);
        return MSP_RESULT_ACK;
    }
    return mspFcProcessCommand(srcDesc, cmd, reply, pp);
}

class AriesMspCliProtocolEdgeTest : public ::testing::Test {
protected:
    void SetUp() override {
        thejas32_host_reset_all();
        IOInitGlobal();
        pgResetAll();

        s_boundUart0 = nullptr;
        s_boundUart1 = nullptr;
        s_uart0FuncMask = FUNCTION_MSP;
        s_displayPortSerial = SERIAL_PORT_NONE;
        s_eepromWrites = 0U;
        s_mixerResetCount = 0U;
        s_captureBackpressureWire = false;
        s_txBackpressureStalls = 0U;
        s_waitTxPolls = 0U;
        s_waitTxBusyPollsBeforeReady = 0U;
        s_capturedBackpressureBytes.clear();
        s_customCmdCalls = 0U;
        s_lastCustomCmd = 0U;
        s_lastCustomSize = 0U;
        armingFlags = 0;
        cliMode = false;

        serialPinConfig_t pinCfg = {};
        pinCfg.ioTagTx[0] = DEFIO_TAG_E__PA30;
        pinCfg.ioTagRx[0] = DEFIO_TAG_E__PA31;
        pinCfg.ioTagTx[1] = DEFIO_TAG_E(PA16);
        pinCfg.ioTagRx[1] = DEFIO_TAG_E(PA17);
        uartPinConfigure(&pinCfg);

        mspConfigMutable()->msp_uart[0] = SERIAL_PORT_UART0;
        mspConfigMutable()->msp_uart[1] = SERIAL_PORT_USART1;
        serialConfigMutable()->reboot_character = 'R';
        thejas32_host_mcycle_set(1000000ULL);
        mspSerialInit();
    }

    void pushUartBytes(int ch, const std::vector<uint8_t> &bytes) {
        const uint8_t irq = (ch == 0) ? THEJAS32_IRQ_UART0 : THEJAS32_IRQ_UART1;
        for (uint8_t b : bytes) {
            THEJAS32_UART(ch).UART_DR = b;
            THEJAS32_UART(ch).UART_LSR = 0x01U;
            THEJAS32_INTR_CTRL.INTR_STATUS = (1UL << irq);
            handle_trap(0x8000000BUL, 0U, nullptr);
        }
    }

    void snapshotAndDrainTx(int ch, serialPort_t *port, std::vector<uint8_t> &txOut) {
        ASSERT_NE(port, nullptr);
        uint32_t tail = port->txBufferTail;
        const uint32_t head = port->txBufferHead;
        const uint32_t cap = port->txBufferSize;
        while (tail != head) {
            txOut.push_back((uint8_t)port->txBuffer[tail]);
            tail = (tail + 1U) % cap;
        }
        for (int i = 0; i < 64 && !isSerialTransmitBufferEmpty(port); i++) {
            THEJAS32_UART(ch).UART_LSR = 0x60U;
            uartPollTx();
        }
    }

    void pumpBoth(std::vector<uint8_t> &tx0, std::vector<uint8_t> &tx1,
                  mspEvaluateNonMspData_e evalMode = MSP_EVALUATE_NON_MSP_DATA) {
        int guard = 0;
        while (((s_boundUart0 && serialRxBytesWaiting(s_boundUart0)) ||
                (s_boundUart1 && serialRxBytesWaiting(s_boundUart1)) || guard == 0) && guard < 128) {
            mspSerialProcess(evalMode, spyMspCommand, mspFcProcessReply);
            guard++;
        }
        snapshotAndDrainTx(0, s_boundUart0, tx0);
        snapshotAndDrainTx(1, s_boundUart1, tx1);
    }
};

// =============================================================================
// EDGE 01: MSPv1 Payload Length 0 & 192 Accepted, 193 (> MSP_PORT_INBUF_SIZE) Drops Safely
// =============================================================================
TEST_F(AriesMspCliProtocolEdgeTest, Edge01_MspV1PayloadBoundary_0And192Accepted_193DropsToIdle)
{
    std::vector<uint8_t> tx0, tx1;

    // 0-byte payload (MSP_API_VERSION)
    pushUartBytes(0, buildMspV1(MSP_API_VERSION));
    pumpBoth(tx0, tx1);
    ASSERT_GE(tx0.size(), 9U);
    EXPECT_EQ(tx0[2], '>');
    EXPECT_EQ(tx0[4], MSP_API_VERSION);

    // Exactly MSP_PORT_INBUF_SIZE (192 bytes)
    tx0.clear();
    std::vector<uint8_t> p192(192U);
    for (size_t i = 0; i < 192U; i++) p192[i] = (uint8_t)(i + 1U);
    s_customCmdCalls = 0U;
    pushUartBytes(0, buildMspV1(240U, p192));
    pumpBoth(tx0, tx1);
    EXPECT_EQ(s_customCmdCalls, 1U);
    EXPECT_EQ(s_lastCustomSize, 192U);
    EXPECT_EQ(s_lastCustomFirstByte, 1U);
    EXPECT_EQ(s_lastCustomLastByte, 192U);

    // 193 bytes exceeds MSP_PORT_INBUF_SIZE -> header state machine drops cleanly to MSP_IDLE
    tx0.clear();
    std::vector<uint8_t> p193 = { '$', 'M', '<', 193U, 240U };
    for (size_t i = 0; i < 193U; i++) p193.push_back(0xAAU);
    p193.push_back(0U);
    s_customCmdCalls = 0U;
    pushUartBytes(0, p193);
    pumpBoth(tx0, tx1);
    EXPECT_EQ(s_customCmdCalls, 0U);

    // Subsequent valid frame must recover immediately without state wedge!
    tx0.clear();
    pushUartBytes(0, buildMspV1(MSP_FC_VARIANT));
    pumpBoth(tx0, tx1);
    ASSERT_GE(tx0.size(), 10U);
    EXPECT_EQ(memcmp(&tx0[5], "BTFL", 4U), 0);
}

// =============================================================================
// EDGE 02: MSPv2-over-V1 (Cmd 255) & Native $X< Framing & Dual CRC Validation
// =============================================================================
TEST_F(AriesMspCliProtocolEdgeTest, Edge02_MspV2OverV1Cmd255AndNativeXFraming_DualChecksumValidation)
{
    std::vector<uint8_t> tx0, tx1;

    // 2A. Valid $X< native MSPv2 frame -> reply starts "$X>" with CRC8-DVB-S2
    pushUartBytes(0, buildMspV2Native(MSP_API_VERSION));
    pumpBoth(tx0, tx1);
    ASSERT_GE(tx0.size(), 12U);
    EXPECT_EQ(tx0[0], '$');
    EXPECT_EQ(tx0[1], 'X');
    EXPECT_EQ(tx0[2], '>');
    EXPECT_EQ(tx0[4], MSP_API_VERSION);

    // 2B. Corrupt V2-over-V1 V2 CRC8 even with recomputed V1 XOR -> rejected cleanly
    tx0.clear();
    s_customCmdCalls = 0U;
    pushUartBytes(0, buildMspV2OverV1(MSP_API_VERSION, {}, true));
    pumpBoth(tx0, tx1);
    EXPECT_EQ(s_customCmdCalls, 0U);
    EXPECT_TRUE(tx0.empty());

    // 2C. Valid V2-over-V1 cmd=255 -> encoded back with cmd 255 ('$','M','>', len, 255, ...)
    pushUartBytes(0, buildMspV2OverV1(MSP_FC_VARIANT));
    pumpBoth(tx0, tx1);
    ASSERT_GE(tx0.size(), 16U);
    EXPECT_EQ(tx0[0], '$');
    EXPECT_EQ(tx0[1], 'M');
    EXPECT_EQ(tx0[2], '>');
    EXPECT_EQ(tx0[4], MSP_V2_FRAME_ID);
}

// =============================================================================
// EDGE 03: Random Garbage, Truncated $X?, & Bad $X< CRC Resyncs Cleanly
// =============================================================================
TEST_F(AriesMspCliProtocolEdgeTest, Edge03_GarbageAndXFrameCorruption_ResyncsCleanlyWithoutGhostCommands)
{
    std::vector<uint8_t> stream = { 0x11, 0x22, '$', 'Z', '$', 'X', '?' };
    const auto badX = buildMspV2Native(MSP_API_VERSION, {}, true);
    stream.insert(stream.end(), badX.begin(), badX.end());
    const auto goodX = buildMspV2Native(MSP_FC_VARIANT, {}, false);
    stream.insert(stream.end(), goodX.begin(), goodX.end());

    pushUartBytes(0, stream);
    std::vector<uint8_t> tx0, tx1;
    pumpBoth(tx0, tx1);

    EXPECT_EQ(s_customCmdCalls, 1U);
    EXPECT_EQ(s_lastCustomCmd, MSP_FC_VARIANT);
    ASSERT_GE(tx0.size(), 12U);
    EXPECT_EQ(tx0[0], '$');
    EXPECT_EQ(tx0[1], 'X');
    EXPECT_EQ(tx0[2], '>');
}

// =============================================================================
// EDGE 04: MSP_SKIP_NON_MSP_DATA Ignores '#'/CR/'R'/0x02 While Still Decoding $M<
// =============================================================================
TEST_F(AriesMspCliProtocolEdgeTest, Edge04_SkipNonMspDataMode_IgnoresCliHashAndRebootChar)
{
    THEJAS32_INTR_CTRL.INTR_EN = 0xFFFFFFFFUL;
    thejas32_host_mstatus = 0x8U;

    pushUartBytes(0, { '#', '\r', '\n', 'R', 0x02U });
    std::vector<uint8_t> tx0, tx1;
    pumpBoth(tx0, tx1, MSP_SKIP_NON_MSP_DATA);
    thejas32_host_mcycle_advance(200000ULL * 100ULL);
    mspSerialProcess(MSP_SKIP_NON_MSP_DATA, spyMspCommand, mspFcProcessReply);

    EXPECT_FALSE(cliMode);
    EXPECT_NE(THEJAS32_INTR_CTRL.INTR_EN, 0U);
    EXPECT_NE(thejas32_host_mstatus & 0x8U, 0U);

    // Valid MSP frame in MSP_SKIP_NON_MSP_DATA mode MUST still be answered
    pushUartBytes(0, buildMspV1(MSP_API_VERSION));
    pumpBoth(tx0, tx1, MSP_SKIP_NON_MSP_DATA);
    ASSERT_GE(tx0.size(), 9U);
    EXPECT_EQ(tx0[2], '>');
}

// =============================================================================
// EDGE 05: Peripheral VTX_MSP / DisplayPort Port Isolation
// =============================================================================
TEST_F(AriesMspCliProtocolEdgeTest, Edge05_PeripheralVtxAndDisplayPortIsolation_NeverTriggersCliOrReboot)
{
    THEJAS32_INTR_CTRL.INTR_EN = 0xFFFFFFFFUL;
    thejas32_host_mstatus = 0x8U;

    // Case A: FUNCTION_VTX_MSP on UART0
    s_uart0FuncMask = FUNCTION_VTX_MSP;
    pushUartBytes(0, { '#', '\r', 'R', 0x02U });
    std::vector<uint8_t> tx0, tx1;
    pumpBoth(tx0, tx1);
    thejas32_host_mcycle_advance(150000ULL * 100ULL);
    mspSerialProcess(MSP_EVALUATE_NON_MSP_DATA, spyMspCommand, mspFcProcessReply);
    EXPECT_FALSE(cliMode);
    EXPECT_NE(THEJAS32_INTR_CTRL.INTR_EN, 0U);
    EXPECT_FALSE(mspSerialIsConfiguratorActive());

    // Case B: FUNCTION_LIDAR peripheral isolation on UART0 (never triggers CLI/reboot/configurator active)
    s_uart0FuncMask = FUNCTION_LIDAR;
    s_displayPortSerial = SERIAL_PORT_NONE;
    pushUartBytes(0, { '#', 'R' });
    pumpBoth(tx0, tx1);
    thejas32_host_mcycle_advance(150000ULL * 100ULL);
    mspSerialProcess(MSP_EVALUATE_NON_MSP_DATA, spyMspCommand, mspFcProcessReply);
    EXPECT_FALSE(cliMode);
    EXPECT_NE(THEJAS32_INTR_CTRL.INTR_EN, 0U);
    EXPECT_FALSE(mspSerialIsConfiguratorActive());
}

// =============================================================================
// EDGE 06: USE_IMMEDIATE_CLI Fast Entry & Reboot 'R' Cancellation Timing
// =============================================================================
TEST_F(AriesMspCliProtocolEdgeTest, Edge06_ImmediateCliAndRebootCancellation_TimingContracts)
{
    THEJAS32_INTR_CTRL.INTR_EN = 0xFFFFFFFFUL;
    thejas32_host_mstatus = 0x8U;

    // Stray 'R' cancelled by unrecognized byte 'a' -> zero bootloader reboot even after 200ms
    pushUartBytes(0, { 'R', 'a' });
    std::vector<uint8_t> tx0, tx1;
    pumpBoth(tx0, tx1);
    thejas32_host_mcycle_advance(200000ULL * 100ULL);
    mspSerialProcess(MSP_EVALUATE_NON_MSP_DATA, spyMspCommand, mspFcProcessReply);
    EXPECT_NE(THEJAS32_INTR_CTRL.INTR_EN, 0U);

    // '\n' enters CLI immediately under USE_IMMEDIATE_CLI (0ms delay required!)
    pushUartBytes(0, { '\n' });
    pumpBoth(tx0, tx1);
    EXPECT_TRUE(cliMode);
}

// =============================================================================
// EDGE 07: PORT_CLI_CMD STX (0x02), ETX (0x03), & 2-Second Timeout Exit
// =============================================================================
TEST_F(AriesMspCliProtocolEdgeTest, Edge07_PortCliCmdStxEtxAnd2sIdleTimeout_NonInteractiveCleanup)
{
    std::vector<uint8_t> tx0, tx1;
    pushUartBytes(0, { 0x02U });
    pumpBoth(tx0, tx1);
    EXPECT_TRUE(cliMode);
    ASSERT_FALSE(tx0.empty());
    EXPECT_EQ(tx0.front(), 0x02U); // STX acknowledged

    // Non-interactive unknown command prints "ERR_CMD_NA: " without crashing; 0x03 (ETX) exits
    // Use s_captureBackpressureWire + THRE=0 so cliExit()->waitForSerialPortToFinishTransmitting()
    // records full TX bytes before draining into HW UART_DR.
    s_captureBackpressureWire = true;
    s_capturedBackpressureBytes.clear();
    THEJAS32_UART(0).UART_LSR = 0x01U;
    const char *seq = "badcmd\n\x03";
    pushUartBytes(0, std::vector<uint8_t>(seq, seq + 8));
    pumpBoth(tx0, tx1);
    EXPECT_FALSE(cliMode);
    std::string nonIntReply(s_capturedBackpressureBytes.begin(), s_capturedBackpressureBytes.end());
    EXPECT_NE(nonIntReply.find("ERR_CMD_NA: badcmd"), std::string::npos);
    ASSERT_FALSE(s_capturedBackpressureBytes.empty());
    EXPECT_EQ(s_capturedBackpressureBytes.back(), 0x03U);
    EXPECT_EQ((uint8_t)(THEJAS32_UART(0).UART_DR & 0xFFU), 0x03U);

    // Re-enter 0x02 and verify 2-second (> 2000ms) idle guard force-exits cleanly and drains 0x03 ETX to UART0 HW
    s_captureBackpressureWire = false;
    tx0.clear();
    pushUartBytes(0, { 0x02U });
    pumpBoth(tx0, tx1);
    ASSERT_TRUE(cliMode);
    thejas32_host_mcycle_advance(2100000ULL * 100ULL);
    tx0.clear();
    THEJAS32_UART(0).UART_DR = 0U;
    pushUartBytes(0, { 'v' });
    pumpBoth(tx0, tx1);
    EXPECT_FALSE(cliMode);
    // cliExit(false) drains ETX (0x03) synchronously via waitForSerialPortToFinishTransmitting() into UART_DR
    EXPECT_EQ((uint8_t)(THEJAS32_UART(0).UART_DR & 0xFFU), 0x03U);
    EXPECT_TRUE(isSerialTransmitBufferEmpty(s_boundUart0));
}

// =============================================================================
// EDGE 08: Real cli.c Interactive Comment Stripping, >255B Buffer Clamp & CTRL-D Exit
// =============================================================================
TEST_F(AriesMspCliProtocolEdgeTest, Edge08_RealCliInteractive_CommentStripLineOverflow256BAndCtrlDExit)
{
    THEJAS32_INTR_CTRL.INTR_EN = 0xFFFFFFFFUL;
    thejas32_host_mstatus = 0x8U;

    std::vector<uint8_t> tx0, tx1;
    pushUartBytes(0, { '#' });
    pumpBoth(tx0, tx1);
    ASSERT_TRUE(cliMode);

    // Inline comment '#' & trailing spaces stripped cleanly before dispatching 'version'
    tx0.clear();
    const char *verComment = "version   # strip this trailing comment   \n";
    pushUartBytes(0, std::vector<uint8_t>(verComment, verComment + strlen(verComment)));
    pumpBoth(tx0, tx1);
    std::string verReply(tx0.begin(), tx0.end());
    EXPECT_NE(verReply.find("Betaflight"), std::string::npos);

    // Overlong 300-char line (> CLI_IN_BUFFER_SIZE - 1 = 255) fed in RX-safe chunks clamps cleanly without crash
    tx0.clear();
    for (int chunk = 0; chunk < 3; chunk++) {
        pushUartBytes(0, std::vector<uint8_t>(100U, 'x'));
        pumpBoth(tx0, tx1);
    }
    pushUartBytes(0, { '\n' });
    pumpBoth(tx0, tx1);
    EXPECT_TRUE(cliMode);

    // CTRL-D (0x04) at empty prompt invokes cliExit(true) -> resets disarmed motors & systemReset()
    tx0.clear();
    s_mixerResetCount = 0U;
    pushUartBytes(0, { 0x04U });
    pumpBoth(tx0, tx1);
    EXPECT_FALSE(cliMode);
    EXPECT_GE(s_mixerResetCount, 1U);
    EXPECT_EQ(THEJAS32_INTR_CTRL.INTR_EN, 0U);
    EXPECT_EQ(thejas32_host_mstatus & 0x8U, 0U);
}

// =============================================================================
// EDGE 09: MSP_EEPROM_WRITE & cliExecuteCommand("save") Armed Safety Lockout
// =============================================================================
TEST_F(AriesMspCliProtocolEdgeTest, Edge09_MspEepromWriteAndCliSaveArmedLockout_RejectsInFlightAllowsDisarmed)
{
    ENABLE_ARMING_FLAG(ARMED);
    std::vector<uint8_t> tx0, tx1;
    pushUartBytes(0, buildMspV1(MSP_EEPROM_WRITE));
    pumpBoth(tx0, tx1);
    ASSERT_GE(tx0.size(), 6U);
    EXPECT_EQ(tx0[2], '!');
    EXPECT_EQ(s_eepromWrites, 0U);

    char out[128] = { 0 };
    EXPECT_EQ(cliExecuteCommand("save", out, sizeof(out)), CLI_COMMAND_REFUSED);
    EXPECT_EQ(s_eepromWrites, 0U);

    DISABLE_ARMING_FLAG(ARMED);
    tx0.clear();
    pushUartBytes(0, buildMspV1(MSP_EEPROM_WRITE));
    pumpBoth(tx0, tx1);
    ASSERT_GE(tx0.size(), 6U);
    EXPECT_EQ(tx0[2], '>');
    EXPECT_EQ(s_eepromWrites, 1U);

    EXPECT_GE(cliExecuteCommand("save", out, sizeof(out)), 0);
    EXPECT_EQ(s_eepromWrites, 2U);
}

// =============================================================================
// EDGE 10: Multi-Port UART0 & UART1 Concurrent Partial MSP Frame Isolation
// =============================================================================
TEST_F(AriesMspCliProtocolEdgeTest, Edge10_MultiPortMspIsolation_Uart0AndUart1PartialFramesDoNotCrossTalk)
{
    // Interleave UART0 first half ("$M<\0") with complete UART1 MSP_FC_VARIANT frame
    pushUartBytes(0, { '$', 'M', '<', 0U });
    pushUartBytes(1, buildMspV1(MSP_FC_VARIANT));

    std::vector<uint8_t> tx0, tx1;
    pumpBoth(tx0, tx1);
    // UART1 answered immediately while UART0 kept its partial state silently
    EXPECT_TRUE(tx0.empty());
    ASSERT_GE(tx1.size(), 10U);
    EXPECT_EQ(memcmp(&tx1[5], "BTFL", 4U), 0);

    // Finish UART0 remaining 2 bytes: cmd=MSP_API_VERSION (1), checksum=(0^1)=1
    tx1.clear();
    pushUartBytes(0, { MSP_API_VERSION, (uint8_t)(0U ^ MSP_API_VERSION) });
    pumpBoth(tx0, tx1);
    ASSERT_GE(tx0.size(), 9U);
    EXPECT_EQ(tx0[4], MSP_API_VERSION);
    EXPECT_TRUE(tx1.empty());
}

// =============================================================================
// EDGE 11: waitForSerialPortToFinishTransmitting Drains With HW TX Backpressure
// =============================================================================
TEST_F(AriesMspCliProtocolEdgeTest, Edge11_WaitForSerialPortFinishTransmitting_DrainsWithHwBackpressure)
{
    ASSERT_NE(s_boundUart0, nullptr);
    THEJAS32_UART(0).UART_LSR = 0x01U; // THRE=0 (TX shift register temporarily full)
    const uint8_t payload[40] = "ARIESV3_BACKPRESSURE_DRAIN_VERIFIED_012";
    serialWriteBuf(s_boundUart0, payload, 40);
    EXPECT_FALSE(isSerialTransmitBufferEmpty(s_boundUart0));

    s_waitTxPolls = 0U;
    s_waitTxBusyPollsBeforeReady = 3U; // Busy for 3 polls before LSR THRE clears
    waitForSerialPortToFinishTransmitting(s_boundUart0);

    EXPECT_GE(s_waitTxPolls, 3U);
    EXPECT_TRUE(isSerialTransmitBufferEmpty(s_boundUart0));
}

// =============================================================================
// EDGE 12: Bounded cliHelp TX Flush (> 256B TX Ring Capacity) Under THRE Backpressure
// =============================================================================
TEST_F(AriesMspCliProtocolEdgeTest, Edge12_BoundedCliHelpTxFlush_DoesNotDeadlockOnSmallUartTxRing)
{
    ASSERT_NE(s_boundUart0, nullptr);
    pushUartBytes(0, { '#' });
    std::vector<uint8_t> tx0, tx1;
    pumpBoth(tx0, tx1);
    ASSERT_TRUE(cliMode);

    s_captureBackpressureWire = true;
    s_txBackpressureStalls = 0U;
    s_capturedBackpressureBytes.clear();

    // Force initial TX FIFO backpressure so serialWriteBufBlocking() exercises uartPollTx()
    THEJAS32_UART(0).UART_LSR = 0x01U;
    const char *helpCmd = "help\n";
    pushUartBytes(0, std::vector<uint8_t>(helpCmd, helpCmd + 5));
    int guard = 0;
    while (serialRxBytesWaiting(s_boundUart0) && guard < 32) {
        mspSerialProcess(MSP_EVALUATE_NON_MSP_DATA, spyMspCommand, mspFcProcessReply);
        guard++;
    }
    waitForSerialPortToFinishTransmitting(s_boundUart0);

    std::string helpOut(s_capturedBackpressureBytes.begin(), s_capturedBackpressureBytes.end());
    EXPECT_GT(helpOut.size(), 256U); // Must exceed single 256-byte UART0 TX ring capacity
    EXPECT_GE(s_txBackpressureStalls, 1U);
    EXPECT_NE(helpOut.find("version"), std::string::npos);
    EXPECT_NE(helpOut.find("save"), std::string::npos);
    EXPECT_NE(helpOut.find("help"), std::string::npos);
    EXPECT_TRUE(isSerialTransmitBufferEmpty(s_boundUart0));
}

} // namespace
