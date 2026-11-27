/*
 * Test Suite: C-DAC ARIES v3.0 Non-Inverted CRSF/ExpressLRS RX (UART1) & Safe Battery Telemetry
 * Target Silicon: C-DAC THEJAS32 SoC (VEGA ET1031 RISC-V RV32IM @ 100 MHz, 248 KB SRAM, 0 RVC)
 *
 * References & Citations:
 *   - PLAN/20_DEVELOPMENT_PHASES.md (Phase 19: Non-Inverted CRSF/ExpressLRS Receiver & Safe Battery)
 *   - PLAN/06_SENSORS_RC_TELEMETRY_AND_SCHEDULER.md (Microcomponents 06_A..06_E)
 *   - PLAN/03_SERIAL_COMMUNICATION_MSP_AND_CLI.md (16550 UART Register Map & Bounded TX Drain)
 *   - knowledgebase/CRITICAL_KNOWLEDGE_GAPS_AND_ASSUMPTIONS.md (Gap 2 No Inverter, Gap 4 U9 Pending, Erratum 1 FCR=0x00)
 *   - knowledgebase/components/03_SERIAL_UART_AND_MSP.md
 *   - knowledgebase/components/08_ANALOG_ADC_SUBSYSTEM.md
 *   - knowledgebase/codebase_evaluations/06_EVAL_SERIAL_UART_ARIES_C.md
 *   - knowledgebase/codebase_evaluations/12_EVAL_STUB_DRIVERS_C.md
 *   - src/platform/ARIES/include/platform/bsp_thejas32.h
 *   - src/platform/ARIES/serial_uart_aries.c
 *   - src/platform/ARIES/stub_drivers.c
 *   - src/main/rx/crsf.c, src/main/rx/rx.c, src/main/sensors/voltage.c, src/main/sensors/battery.c
 *
 * Methodology:
 *   Strict Test-Driven Development (TDD) - Living Documentation (TDD Step 1).
 *   Tests real Betaflight C subsystems (rx.c, crsf.c, voltage.c, battery.c, serial.c, rc_modes.c)
 *   against faithful ARIES THEJAS32 UART1 / ADC register and fallback models.
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>
#include <algorithm>
#include <vector>

#include "unittest_macros.h"
#include "gtest/gtest.h"
#include "aries_host_mmio.h"

extern "C" {
    #include "../main/platform.h"
    #include "build/build_config.h"
    #include "build/debug.h"

    #include "common/axis.h"
    #include "common/bitarray.h"
    #include "common/crc.h"
    #include "common/filter.h"
    #include "common/maths.h"
    #include "common/time.h"
    #include "common/utils.h"

    #include "config/config.h"
    #include "config/feature.h"

    #include "drivers/adc.h"
    #include "drivers/io.h"
    #include "drivers/persistent.h"
    #include "drivers/serial.h"
    #include "drivers/serial_uart.h"
    #include "drivers/serial_uart_impl.h"
    #include "drivers/system.h"

    #include "fc/rc_controls.h"
    #include "fc/rc_modes.h"
    #include "fc/runtime_config.h"
    #include "fc/tasks.h"

    #include "flight/failsafe.h"
    #include "flight/mixer.h"

    #include "io/beeper.h"
    #include "io/serial.h"

    #include "pg/pg.h"
    #include "pg/pg_ids.h"
    #include "pg/rx.h"

    #include "rx/rx.h"
    #include "rx/crsf.h"
    #include "telemetry/crsf.h"

    #include "sensors/adcinternal.h"
    #include "sensors/battery.h"
    #include "sensors/voltage.h"

    #include "platform/ARIES/include/platform/bsp_thejas32.h"

    // STATIC_UNIT_TESTED accessors exported by Betaflight rx/crsf.c & sensors/voltage.c
    void crsfDataReceive(uint16_t c, void *data);
    uint8_t crsfFrameCRC(void);
    uint8_t crsfFrameCmdCRC(void);
    uint8_t crsfFrameStatus(rxRuntimeState_t *rxRuntimeState);
    float crsfReadRawRC(const rxRuntimeState_t *rxRuntimeState, uint8_t chan);
    uint16_t voltageAdcToVoltage(const uint16_t src, const voltageSensorADCConfig_t *config);

    extern bool crsfFrameDone;
    extern crsfFrame_t crsfFrame;
    extern crsfFrame_t crsfChannelDataFrame;
    extern uint32_t crsfChannelData[CRSF_MAX_CHANNEL];

    extern boxBitmask_t rcModeActivationMask;

    // PGs required by linked Betaflight core files (not defined in linked sources)
    PG_REGISTER(systemConfig_t, systemConfig, PG_SYSTEM_CONFIG, 4);
    PG_REGISTER(flight3DConfig_t, flight3DConfig, PG_MOTOR_3D_CONFIG, 0);
    PG_REGISTER(failsafeConfig_t, failsafeConfig, PG_FAILSAFE_CONFIG, 0);
}

namespace {

// =============================================================================
// ARIES THEJAS32 Silicon Constants & Mock 16550 UART1 / U9 ADC Harness
// =============================================================================

constexpr uint32_t THEJAS32_APB_CLOCK_HZ     = 100000000UL;
constexpr uint32_t ARIES_UART0_MMIO_ADDR     = 0x10000100UL;
constexpr uint32_t ARIES_UART1_MMIO_ADDR     = 0x10000200UL;
constexpr uint32_t ARIES_I2C_MMIO_ADDR       = 0x10000800UL;
constexpr uint32_t ARIES_ADC_MMIO_ADDR       = 0x10001000UL;
constexpr uint32_t CRSF_INTERFRAME_US        = 1748UL;
constexpr uint32_t CRSF_LINK_STATS_TIMEOUT   = 250000UL;
constexpr uint32_t RXLOSS_TIMEOUT_US         = 150000UL;

constexpr uint8_t UART_LCR_8N1               = 0x03;
constexpr uint8_t UART_LCR_DLAB              = 0x80;
constexpr uint8_t UART_LSR_DR                = 0x01;
constexpr uint8_t UART_LSR_OE                = 0x02;
constexpr uint8_t UART_LSR_PE                = 0x04;
constexpr uint8_t UART_LSR_FE                = 0x08;
constexpr uint8_t UART_LSR_THRE              = 0x20;
constexpr uint8_t UART_LSR_TEMT              = 0x40;

struct MockUart1State {
    uint8_t RBR;
    uint8_t THR;
    uint8_t DLL;
    uint8_t DLM;
    uint8_t IER;
    uint8_t FCR;
    uint8_t LCR;
    uint8_t LSR;

    bool opened;
    serialPortIdentifier_e openedPortId;
    serialPortFunction_e openedFunction;
    serialReceiveCallbackPtr rxCallback;
    void *rxCallbackData;
    uint32_t requestedBaud;
    uint32_t clampedBaud;
    portMode_e openedMode;
    portOptions_e openedOptions;

    std::vector<uint8_t> txStream;
    std::vector<size_t> txBurstChunks;
    std::vector<uint8_t> rxHardwareBuf;
    uint32_t framingErrorCount;
    uint32_t overrunErrorCount;

    void reset() {
        RBR = 0;
        THR = 0;
        DLL = 0;
        DLM = 0;
        IER = 0;
        FCR = 0;
        LCR = 0;
        LSR = UART_LSR_THRE | UART_LSR_TEMT;
        opened = false;
        openedPortId = SERIAL_PORT_NONE;
        openedFunction = FUNCTION_NONE;
        rxCallback = NULL;
        rxCallbackData = NULL;
        requestedBaud = 0;
        clampedBaud = 0;
        openedMode = (portMode_e)0;
        openedOptions = (portOptions_e)0;
        txStream.clear();
        txBurstChunks.clear();
        rxHardwareBuf.clear();
        framingErrorCount = 0;
        overrunErrorCount = 0;
    }

    // Mirrors serial_uart_aries.c::thejas32_uart_set_baudrate_ex()
    uint32_t configure16550(uint32_t baudRate, portOptions_e options, portMode_e mode) {
        if (baudRate == 0) {
            baudRate = 115200U;
        }
        if (baudRate > 460800U) {
            baudRate = 420000U;
        }
        clampedBaud = baudRate;

        uint32_t divisor = (THEJAS32_APB_CLOCK_HZ + (baudRate * 8U)) / (baudRate * 16U);
        if (divisor == 0U) {
            divisor = 1U;
        }

        uint8_t lcr = UART_LCR_8N1;
        if (options & SERIAL_STOPBITS_2) {
            lcr |= 0x04;
        }
        if (options & SERIAL_PARITY_EVEN) {
            lcr |= 0x18;
        }

        LCR = (uint8_t)(UART_LCR_DLAB | lcr);
        DLL = (uint8_t)(divisor & 0xFFU);
        DLM = (uint8_t)((divisor >> 8) & 0xFFU);
        LCR = lcr;

        // Erratum 1: DW_apb_uart 16550 FIFO disabled (1-byte register mode)
        FCR = 0x00;

        // RX interrupt enabled only; THRE bit 1 stays 0
        IER = (mode & MODE_RX) ? 0x01 : 0x00;
        return divisor;
    }

    // Mirrors serial_uart_aries.c::uartIrqHandler() with LSR read-to-clear semantics
    bool serviceRxByte(uint8_t byte, uint8_t lsrFlags) {
        LSR = lsrFlags | UART_LSR_THRE | UART_LSR_TEMT;
        const uint8_t lsrSnapshot = LSR;
        // 16550 LSR read clears OE/PE/FE
        LSR &= (uint8_t)~(UART_LSR_OE | UART_LSR_PE | UART_LSR_FE);

        if (lsrSnapshot & UART_LSR_OE) {
            overrunErrorCount++;
        }
        if (lsrSnapshot & UART_LSR_FE) {
            framingErrorCount++;
            LSR &= (uint8_t)~UART_LSR_DR;
            return false; // Reject corrupted inverted/framing-errored wire byte
        }
        if ((lsrSnapshot & UART_LSR_DR) && rxCallback) {
            RBR = byte;
            LSR &= (uint8_t)~UART_LSR_DR;
            rxCallback(RBR, rxCallbackData);
            return true;
        }
        return false;
    }
};

struct TestMockState {
    uint32_t simTimeUs;
    uint32_t simTimeMs;
    MockUart1State uart1;
    serialPort_t mockSerialPort;

    // Stub telemetry/runtime counters
    uint32_t failsafeValidCount;
    uint32_t failsafeFailedCount;
    bool failsafeActive;
    bool taskRxInProgress;
    bool forceNullSerialPort = false;
    bool crsfBaudNegInProgress;
    bool eepromWriting;
    uint32_t setCrsfDefaultSpeedCalls;
    uint32_t persistentWrites;
    uint32_t persistentLastId;
    uint32_t persistentLastVal;
    uint32_t crsfCmdProcessCount;
    uint8_t lastProcessedCmd[16];
    uint16_t ariesAdcCache[4];

    void reset() {
        simTimeUs = 1000000UL;
        simTimeMs = 1000UL;
        uart1.reset();
        memset(&mockSerialPort, 0, sizeof(mockSerialPort));
        failsafeValidCount = 0;
        failsafeFailedCount = 0;
        failsafeActive = false;
        taskRxInProgress = false;
        crsfBaudNegInProgress = false;
        eepromWriting = false;
        setCrsfDefaultSpeedCalls = 0;
        persistentWrites = 0;
        persistentLastId = 0;
        persistentLastVal = 0;
        crsfCmdProcessCount = 0;
        memset(lastProcessedCmd, 0, sizeof(lastProcessedCmd));
        // Exact stub_drivers.c default safe nominal values
        ariesAdcCache[0] = 2048; // ADC_BATTERY nominal ~12.6V safe cache
        ariesAdcCache[1] = 0;    // ADC_CURRENT
        ariesAdcCache[2] = 1024; // ADC_RSSI
        ariesAdcCache[3] = 0;    // AUX/spare
    }
};

static TestMockState g_mock;
#ifdef USE_TELEMETRY
static task_attribute_t g_mockTelemetryAttr = {};
static task_t g_mockTelemetryTask = { .attribute = &g_mockTelemetryAttr };
#endif

void mockSerialWrite(serialPort_t *, uint8_t ch)
{
    g_mock.uart1.txStream.push_back(ch);
}

void mockSerialWriteBuf(serialPort_t *, const void *data, int count)
{
    const uint8_t *bytes = static_cast<const uint8_t *>(data);
    int offset = 0;
    // Mirrors serial_uart_aries.c::uartEnableTxInterrupt() bounded 16-byte burst chunks
    while (offset < count) {
        const size_t chunk = (size_t)std::min(16, count - offset);
        for (size_t i = 0; i < chunk; i++) {
            g_mock.uart1.txStream.push_back(bytes[offset + (int)i]);
        }
        g_mock.uart1.txBurstChunks.push_back(chunk);
        offset += (int)chunk;
    }
}

uint32_t mockSerialTotalRxWaiting(const serialPort_t *) { return 0; }
uint32_t mockSerialTotalTxFree(const serialPort_t *) { return 256; }
uint8_t mockSerialRead(serialPort_t *) { return 0; }
void mockSerialSetBaudRate(serialPort_t *instance, uint32_t baudRate)
{
    instance->baudRate = baudRate;
    g_mock.uart1.configure16550(baudRate, instance->options, instance->mode);
}
bool mockSerialIsTxEmpty(const serialPort_t *) { return true; }
void mockSerialSetMode(serialPort_t *instance, portMode_e mode) { instance->mode = mode; }

const serialPortVTable g_mockUart1VTable = {
    .serialWrite = mockSerialWrite,
    .serialTotalRxWaiting = mockSerialTotalRxWaiting,
    .serialTotalTxFree = mockSerialTotalTxFree,
    .serialRead = mockSerialRead,
    .serialSetBaudRate = mockSerialSetBaudRate,
    .isSerialTransmitBufferEmpty = mockSerialIsTxEmpty,
    .setMode = mockSerialSetMode,
    .setCtrlLineStateCb = NULL,
    .setBaudRateCb = NULL,
    .writeBuf = mockSerialWriteBuf,
    .beginWrite = NULL,
    .endWrite = NULL,
};

// CRSF Frame Builders (shared helpers — zero test duplication)

void pack11BitChannels(const uint16_t ch[CRSF_MAX_CHANNEL], uint8_t payload[CRSF_FRAME_RC_CHANNELS_PAYLOAD_SIZE])
{
    memset(payload, 0, CRSF_FRAME_RC_CHANNELS_PAYLOAD_SIZE);
    uint32_t bitPos = 0;
    for (int c = 0; c < CRSF_MAX_CHANNEL; c++) {
        uint32_t val = ch[c] & 0x7FFU;
        for (int b = 0; b < 11; b++) {
            if (val & (1U << b)) {
                payload[bitPos / 8] |= (uint8_t)(1U << (bitPos % 8));
            }
            bitPos++;
        }
    }
}

uint8_t computeCrsfDvbS2Crc(const uint8_t *typeAndPayload, size_t len)
{
    uint8_t crc = 0;
    for (size_t i = 0; i < len; i++) {
        crc = crc8_dvb_s2(crc, typeAndPayload[i]);
    }
    return crc;
}

uint8_t computeCrsfPoly0xBaCrc(const uint8_t *typeAndPayload, size_t len)
{
    uint8_t crc = 0;
    for (size_t i = 0; i < len; i++) {
        crc = crc8_poly_0xba(crc, typeAndPayload[i]);
    }
    return crc;
}

std::vector<uint8_t> buildRcChannelsFrame(uint8_t deviceAddress, const uint16_t ch[CRSF_MAX_CHANNEL])
{
    std::vector<uint8_t> frame(26, 0);
    frame[0] = deviceAddress;
    frame[1] = CRSF_FRAME_RC_CHANNELS_PAYLOAD_SIZE + CRSF_FRAME_LENGTH_TYPE_CRC; // 24 (0x18)
    frame[2] = CRSF_FRAMETYPE_RC_CHANNELS_PACKED; // 0x16
    pack11BitChannels(ch, &frame[3]);
    frame[25] = computeCrsfDvbS2Crc(&frame[2], 1 + CRSF_FRAME_RC_CHANNELS_PAYLOAD_SIZE);
    return frame;
}

std::vector<uint8_t> buildCrsfLinkStatsFrame(
    uint8_t rssi1, uint8_t rssi2, uint8_t lq, int8_t snr,
    uint8_t activeAntenna, uint8_t rfMode, uint8_t txPowerIdx)
{
    std::vector<uint8_t> frame(14, 0);
    frame[0] = CRSF_ADDRESS_FLIGHT_CONTROLLER;
    frame[1] = CRSF_FRAME_ORIGIN_DEST_SIZE + CRSF_FRAME_LINK_STATISTICS_PAYLOAD_SIZE; // 12
    frame[2] = CRSF_FRAMETYPE_LINK_STATISTICS; // 0x14
    frame[3] = rssi1;
    frame[4] = rssi2;
    frame[5] = lq;
    frame[6] = (uint8_t)snr;
    frame[7] = activeAntenna;
    frame[8] = rfMode;
    frame[9] = txPowerIdx;
    frame[10] = 0;
    frame[11] = 0;
    frame[12] = 0;
    frame[13] = computeCrsfDvbS2Crc(&frame[2], 1 + CRSF_FRAME_LINK_STATISTICS_PAYLOAD_SIZE);
    return frame;
}

#if defined(USE_CRSF_V3)
std::vector<uint8_t> buildCrsfLinkStatsTxFrame(uint8_t rssi, uint8_t rssiPct, uint8_t lq, int8_t snr)
{
    std::vector<uint8_t> frame(10, 0);
    frame[0] = CRSF_ADDRESS_FLIGHT_CONTROLLER;
    frame[1] = CRSF_FRAME_ORIGIN_DEST_SIZE + CRSF_FRAME_LINK_STATISTICS_TX_PAYLOAD_SIZE; // 8
    frame[2] = CRSF_FRAMETYPE_LINK_STATISTICS_TX; // 0x1D
    frame[3] = rssi;
    frame[4] = rssiPct;
    frame[5] = lq;
    frame[6] = (uint8_t)snr;
    frame[7] = 0;
    frame[8] = 0;
    frame[9] = computeCrsfDvbS2Crc(&frame[2], 1 + CRSF_FRAME_LINK_STATISTICS_TX_PAYLOAD_SIZE);
    return frame;
}
#endif

std::vector<uint8_t> buildCrsfCommandFrame(uint8_t destAddr, uint8_t originAddr, uint8_t cmdId, uint8_t subCmd)
{
    std::vector<uint8_t> frame(9, 0);
    frame[0] = CRSF_SYNC_BYTE;
    frame[1] = 0x07;
    frame[2] = CRSF_FRAMETYPE_COMMAND;
    frame[3] = destAddr;
    frame[4] = originAddr;
    frame[5] = cmdId;
    frame[6] = subCmd;
    frame[7] = computeCrsfPoly0xBaCrc(&frame[2], 5); // inner command CRC
    frame[8] = computeCrsfDvbS2Crc(&frame[2], 6);    // outer frame CRC
    return frame;
}

void feedFrameToCrsf(const std::vector<uint8_t> &frame, uint32_t perByteUs = 21U)
{
    for (uint8_t b : frame) {
        g_mock.simTimeUs += perByteUs;
        crsfDataReceive(b, &rxRuntimeState);
    }
}

void makeDefaultChannels(uint16_t ch[CRSF_MAX_CHANNEL], uint16_t roll = 992, uint16_t pitch = 992, uint16_t yaw = 992, uint16_t throttle = 172, uint16_t aux1 = 172)
{
    for (int i = 0; i < CRSF_MAX_CHANNEL; i++) {
        ch[i] = 992;
    }
    ch[0] = roll;
    ch[1] = pitch;
    ch[2] = yaw;
    ch[3] = throttle;
    ch[4] = aux1;
}

// Simulated U9 I2C2 non-blocking poll guard (Gap 4 unconfirmed safe fallback)
struct U9PollResult {
    bool success;
    bool timedOut;
    uint32_t pollsExecuted;
    uint16_t voltageRaw;
};

U9PollResult simulateNonBlockingU9Poll(bool busHungOrNack, uint32_t maxPolls)
{
    U9PollResult res = { false, false, 0, g_mock.ariesAdcCache[0] };
    for (uint32_t i = 0; i < maxPolls; i++) {
        res.pollsExecuted++;
        if (!busHungOrNack) {
            res.success = true;
            g_mock.ariesAdcCache[0] = 1420U; // Fresh sample when bus responds
            res.voltageRaw = g_mock.ariesAdcCache[0];
            return res;
        }
    }
    // Bounded abort: retain previous safe cached reading instead of zeroing or spinning
    res.timedOut = true;
    res.voltageRaw = g_mock.ariesAdcCache[0];
    return res;
}

class AriesRxCrsfTest : public ::testing::Test {
protected:
    void SetUp() override {
        g_mock.reset();
        g_mock.forceNullSerialPort = false;
        pgResetAll();

        // Advance clock cleanly beyond 1748 us so crsf.c static parser resets to byte 0
        g_mock.simTimeUs += 500000U;
        g_mock.simTimeMs += 500U;
        crsfFrameDone = false;
        memset(&crsfFrame, 0, sizeof(crsfFrame));
        memset(&crsfChannelDataFrame, 0, sizeof(crsfChannelDataFrame));
        memset(crsfChannelData, 0, sizeof(crsfChannelData));
        memset(&rcModeActivationMask, 0, sizeof(rcModeActivationMask));

        featureConfigMutable()->enabledFeatures = FEATURE_RX_SERIAL;
        featureInit();
        rxConfigMutable()->rx_uart = SERIAL_PORT_USART1;
        rxConfigMutable()->serialrx_provider = SERIALRX_CRSF;
        rxConfigMutable()->serialrx_inverted = 0;
        rxConfigMutable()->crsf_use_negotiated_baud = false;
        rxConfigMutable()->rssi_smoothing = 0;

        // Reset crsf.c static serialPort pointer to NULL before clearing telemetry sources
        g_mock.forceNullSerialPort = true;
        rxRuntimeState_t resetState = {};
        crsfRxInit(rxConfig(), &resetState);
        g_mock.forceNullSerialPort = false;
        g_mock.uart1.reset();

        rssiSource = RSSI_SOURCE_NONE;
        linkQualitySource = LQ_SOURCE_NONE;

        failsafeConfigMutable()->failsafe_throttle = 1000;
        failsafeConfigMutable()->failsafe_delay = 10;

        batteryConfigMutable()->voltageMeterSource = VOLTAGE_METER_NONE;
        batteryConfigMutable()->currentMeterSource = CURRENT_METER_NONE;
    }

    void TearDown() override {
        g_mock.simTimeUs += 500000U;
    }
};

// =============================================================================
// Category 1: UART1 HW Binding, 420k Divisor & Non-Inversion Guards (Tests 1-5)
// =============================================================================

TEST_F(AriesRxCrsfTest, Category1_Test01_UART1PortIrqAndMmioBinding) {
    // Arrange / Act: Verify C-DAC THEJAS32 BSP offsets and Betaflight UART1 assignment
    const uintptr_t uart0Addr = (uintptr_t)THEJAS32_UART_BASE;
    const uintptr_t uart1Addr = (uintptr_t)(THEJAS32_UART_BASE + 0x100UL);

    // Assert: Physical UART1 MMIO & IRQ 1 (correcting stale PLAN/06 USART2/IRQ3 text)
    EXPECT_EQ(ARIES_UART0_MMIO_ADDR, uart0Addr);
    EXPECT_EQ(ARIES_UART1_MMIO_ADDR, uart1Addr);
    EXPECT_EQ(1U, (uint32_t)THEJAS32_IRQ_UART1);
    EXPECT_EQ(0U, (uint32_t)THEJAS32_IRQ_UART0);
    EXPECT_EQ(51, (int)SERIAL_PORT_USART1);
    EXPECT_EQ(420000U, (uint32_t)CRSF_BAUDRATE);
    EXPECT_TRUE(featureIsEnabled(FEATURE_RX_SERIAL));
    EXPECT_EQ(SERIAL_PORT_USART1, rxConfig()->rx_uart);
    EXPECT_EQ(SERIALRX_CRSF, rxConfig()->serialrx_provider);
}

TEST_F(AriesRxCrsfTest, Category1_Test02_UART1_420kBaudDivisor15Precision) {
    // Act: Compute 16550 DLAB divisor at 420k and check high-rate clamping (>460.8k -> 420k)
    const uint32_t div420k = g_mock.uart1.configure16550(420000U, (portOptions_e)CRSF_PORT_OPTIONS, CRSF_PORT_MODE);
    const double actualBaud420k = (double)THEJAS32_APB_CLOCK_HZ / (16.0 * (double)div420k);
    const double errPct420k = ((actualBaud420k - 420000.0) / 420000.0) * 100.0;

    // Assert: Divisor 15 yields 416,666.67 bps (-0.794% error, well within +/-2% CRSF spec)
    EXPECT_EQ(15U, div420k);
    EXPECT_EQ(420000U, g_mock.uart1.clampedBaud);
    EXPECT_NEAR(-0.794, errPct420k, 0.01);
    EXPECT_LT(std::abs(errPct420k), 2.0);

    // High baud > 460800 (e.g., 921600) must clamp to 420000 (because unclamped div 7 = -3.12%)
    const uint32_t divClamped = g_mock.uart1.configure16550(921600U, (portOptions_e)CRSF_PORT_OPTIONS, CRSF_PORT_MODE);
    EXPECT_EQ(420000U, g_mock.uart1.clampedBaud);
    EXPECT_EQ(15U, divClamped);
    const double unclamped921kErr = (((double)THEJAS32_APB_CLOCK_HZ / (16.0 * 7.0)) - 921600.0) / 921600.0 * 100.0;
    EXPECT_GT(std::abs(unclamped921kErr), 2.5);
}

TEST_F(AriesRxCrsfTest, Category1_Test03_UART1_8N1_FcrZeroErrataAndIer) {
    // Act: Configure UART1 for CRSF 8N1 MODE_RXTX
    g_mock.uart1.configure16550(CRSF_BAUDRATE, (portOptions_e)CRSF_PORT_OPTIONS, CRSF_PORT_MODE);

    // Assert: DLAB cleared, 8N1 framing (0x03), Erratum 1 FCR=0x00, RX-only IER=0x01
    EXPECT_EQ(UART_LCR_8N1, g_mock.uart1.LCR);
    EXPECT_EQ(15U, g_mock.uart1.DLL);
    EXPECT_EQ(0U, g_mock.uart1.DLM);
    EXPECT_EQ(0x00U, g_mock.uart1.FCR);
    EXPECT_EQ(0x01U, g_mock.uart1.IER);
}

TEST_F(AriesRxCrsfTest, Category1_Test04_NonInvertedCrsfVsInvertedSbusGuard) {
    // Arrange: Initialize real CRSF on UART1
    ASSERT_TRUE(crsfRxInit(rxConfig(), &rxRuntimeState));
    EXPECT_EQ(0U, rxConfig()->serialrx_inverted);
    EXPECT_EQ(0U, g_mock.uart1.openedOptions & SERIAL_INVERTED);

    uint16_t ch[CRSF_MAX_CHANNEL];
    makeDefaultChannels(ch, 1200, 800, 992, 500, 172);
    const auto validFrame = buildRcChannelsFrame(CRSF_ADDRESS_FLIGHT_CONTROLLER, ch);

    // Act 1: Feed inverted SBUS-like bytes flagging 16550 LSR Framing Error + Overrun
    for (int i = 0; i < 6; i++) {
        g_mock.simTimeUs += 21U;
        const bool accepted = g_mock.uart1.serviceRxByte((uint8_t)(~validFrame[i]), UART_LSR_DR | UART_LSR_FE | UART_LSR_OE);
        EXPECT_FALSE(accepted);
    }
    EXPECT_EQ(6U, g_mock.uart1.framingErrorCount);
    EXPECT_EQ(6U, g_mock.uart1.overrunErrorCount);
    // Verify LSR error flags auto-clear after LSR read (no sticky ISR storm)
    EXPECT_EQ(0U, g_mock.uart1.LSR & (UART_LSR_FE | UART_LSR_OE | UART_LSR_PE));
    EXPECT_FALSE(crsfFrameDone);

    // Act 2: Feed valid non-inverted CRSF frame through UART1 ISR callback
    g_mock.simTimeUs += 5000U;
    for (uint8_t b : validFrame) {
        g_mock.simTimeUs += 21U;
        ASSERT_TRUE(g_mock.uart1.serviceRxByte(b, UART_LSR_DR));
    }

    // Assert: Non-inverted CRSF frame cleanly decoded
    EXPECT_EQ(RX_FRAME_COMPLETE, crsfFrameStatus(&rxRuntimeState));
    EXPECT_EQ(1200U, crsfChannelData[0]);
    EXPECT_EQ(800U, crsfChannelData[1]);
}

TEST_F(AriesRxCrsfTest, Category1_Test05_CrsfRxInit_ValidAndNullPort) {
    // Act 1: Valid UART1 initialization
    rxRuntimeState_t state = {};
    ASSERT_TRUE(crsfRxInit(rxConfig(), &state));

    // Assert 1: Correct port, baudrate, mode, options, and RX sources bound
    EXPECT_TRUE(g_mock.uart1.opened);
    EXPECT_EQ(SERIAL_PORT_USART1, g_mock.uart1.openedPortId);
    EXPECT_EQ(FUNCTION_RX_SERIAL, g_mock.uart1.openedFunction);
    EXPECT_EQ((uint32_t)CRSF_BAUDRATE, g_mock.uart1.requestedBaud);
    EXPECT_EQ(CRSF_PORT_MODE, g_mock.uart1.openedMode);
    EXPECT_EQ((portOptions_e)CRSF_PORT_OPTIONS, g_mock.uart1.openedOptions);
    EXPECT_EQ(CRSF_MAX_CHANNEL, state.channelCount);
    EXPECT_NE(nullptr, (void *)state.rcReadRawFn);
    EXPECT_NE(nullptr, (void *)state.rcFrameStatusFn);
    EXPECT_TRUE(crsfRxIsActive());
    EXPECT_EQ(RSSI_SOURCE_RX_PROTOCOL_CRSF, rssiSource);
#ifdef USE_RX_LINK_QUALITY_INFO
    EXPECT_EQ(LQ_SOURCE_RX_PROTOCOL_CRSF, linkQualitySource);
#else
    EXPECT_EQ(LQ_SOURCE_NONE, linkQualitySource);
#endif

    // Act 2: Null / unconfigured port rejection
    rxConfigMutable()->rx_uart = SERIAL_PORT_NONE;
    rxRuntimeState_t emptyState = {};
    EXPECT_FALSE(crsfRxInit(rxConfig(), &emptyState));
}

// =============================================================================
// Category 2: Real crsf.c Ingestion, 0xD5/0xBA CRC8 & Resync (Tests 6-10)
// =============================================================================

TEST_F(AriesRxCrsfTest, Category2_Test06_CrsfFrameIngest_Valid0xD5Crc) {
    // Arrange
    uint16_t ch[CRSF_MAX_CHANNEL];
    makeDefaultChannels(ch, 992, 992, 992, 172, 992);
    const auto frame = buildRcChannelsFrame(CRSF_ADDRESS_FLIGHT_CONTROLLER, ch);

    // Act
    feedFrameToCrsf(frame);

    // Assert: Exact CRC8-DVB-S2 (0xD5) validation (not CRC8 0x3D)
    EXPECT_TRUE(crsfFrameDone);
    EXPECT_EQ(frame[25], crsfFrameCRC());
    uint8_t wrongPolyCrc = crc8_update(0, &frame[2], 23, 0x3D);
    EXPECT_NE(wrongPolyCrc, frame[25]);
    EXPECT_EQ(RX_FRAME_COMPLETE, crsfFrameStatus(&rxRuntimeState));
    EXPECT_FALSE(crsfFrameDone);
}

TEST_F(AriesRxCrsfTest, Category2_Test07_CrsfCmdFrame_Poly0xBaValidation) {
    // Arrange: Valid CRSF command frame (dest=FC, origin=CRSF_RECEIVER, RX bind subcmd)
    auto cmdFrame = buildCrsfCommandFrame(
        CRSF_ADDRESS_FLIGHT_CONTROLLER,
        CRSF_ADDRESS_CRSF_RECEIVER,
        CRSF_COMMAND_SUBCMD_RX,
        CRSF_COMMAND_SUBCMD_RX_BIND
    );

    // Act 1: Feed valid command frame via crsfDataReceive
    feedFrameToCrsf(cmdFrame);

    // Assert 1: Outer 0xD5 CRC matches; inner 0xBA cmd dispatch is active when USE_CRSF_V3 is enabled
    EXPECT_EQ(cmdFrame[8], crsfFrameCRC());
#if defined(USE_CRSF_V3) || defined(UNIT_TEST)
    EXPECT_EQ(cmdFrame[7], crsfFrameCmdCRC());
#endif
#if defined(USE_CRSF_V3)
    EXPECT_EQ(1U, g_mock.crsfCmdProcessCount);
    EXPECT_EQ(CRSF_COMMAND_SUBCMD_RX, g_mock.lastProcessedCmd[0]);
    EXPECT_EQ(CRSF_COMMAND_SUBCMD_RX_BIND, g_mock.lastProcessedCmd[1]);

    // Act 2: Flip bit in inner command byte while recomputing only outer CRC -> inner 0xBA must reject
    g_mock.simTimeUs += 5000U;
    cmdFrame[6] ^= 0x01U;
    cmdFrame[8] = computeCrsfDvbS2Crc(&cmdFrame[2], 6);
    feedFrameToCrsf(cmdFrame);

    // Assert 2: Inner CRC mismatch prevents command dispatch
    EXPECT_NE(cmdFrame[7], crsfFrameCmdCRC());
    EXPECT_EQ(1U, g_mock.crsfCmdProcessCount);
#else
    // ARIESV3 disables USE_TELEMETRY_CRSF -> common_post.h disables USE_CRSF_V3 command execution
    EXPECT_EQ(0U, g_mock.crsfCmdProcessCount);
#endif
}

TEST_F(AriesRxCrsfTest, Category2_Test08_CrsfSyncAddressFilter_GarbageAndWrongAddr) {
    // Arrange
    uint16_t ch[CRSF_MAX_CHANNEL];
    makeDefaultChannels(ch, 500, 600, 700, 800, 900);
    const auto wrongAddrFrame = buildRcChannelsFrame(CRSF_ADDRESS_GPS, ch);
    const auto validFcFrame = buildRcChannelsFrame(CRSF_ADDRESS_FLIGHT_CONTROLLER, ch);

    // Act 1: Frame addressed to GPS (0xC2) has valid CRC, must NOT populate RC FC channel data
    feedFrameToCrsf(wrongAddrFrame);
    EXPECT_FALSE(crsfFrameDone);

    // Act 2: Pre-sync garbage followed after >1748 us gap by valid 0xC8 FC frame
    const uint8_t noise[] = { 0x00, 0x55, 0xAA, 0x7F, 0xFF };
    for (uint8_t n : noise) {
        g_mock.simTimeUs += 21U;
        crsfDataReceive(n, &rxRuntimeState);
    }
    g_mock.simTimeUs += 5000U; // Inter-frame timeout resets parser cleanly
    feedFrameToCrsf(validFcFrame);

    // Assert: Valid 0xC8 FC frame succeeds after noise + wrong address
    EXPECT_TRUE(crsfFrameDone);
    EXPECT_EQ(RX_FRAME_COMPLETE, crsfFrameStatus(&rxRuntimeState));
    EXPECT_EQ(500U, crsfChannelData[0]);
}

TEST_F(AriesRxCrsfTest, Category2_Test09_CrsfMalformedLengthAndCrcResync) {
    // Arrange
    uint16_t ch[CRSF_MAX_CHANNEL];
    makeDefaultChannels(ch, 1500, 1200, 800, 300, 1811);
    const auto validFrame = buildRcChannelsFrame(CRSF_ADDRESS_FLIGHT_CONTROLLER, ch);

    const uint8_t badLengths[] = {
        0,
        CRSF_FRAME_LENGTH_TYPE_CRC - 1,
        (uint8_t)(CRSF_FRAME_SIZE_MAX - CRSF_FRAME_LENGTH_ADDRESS - CRSF_FRAME_LENGTH_FRAMELENGTH + 1),
        CRSF_FRAME_SIZE_MAX
    };

    // Act 1: Inject invalid declared frame lengths; parser resets immediately without overflow
    for (uint8_t len : badLengths) {
        g_mock.simTimeUs += 5000U;
        crsfDataReceive(CRSF_ADDRESS_FLIGHT_CONTROLLER, &rxRuntimeState);
        g_mock.simTimeUs += 21U;
        crsfDataReceive(len, &rxRuntimeState);
        g_mock.simTimeUs += 21U;
        crsfDataReceive(CRSF_FRAMETYPE_RC_CHANNELS_PACKED, &rxRuntimeState);
        EXPECT_FALSE(crsfFrameDone);
    }

    // Act 2: Inject single-bit CRC corruption
    g_mock.simTimeUs += 5000U;
    auto badCrcFrame = validFrame;
    badCrcFrame[25] ^= 0x80U;
    feedFrameToCrsf(badCrcFrame);
    EXPECT_FALSE(crsfFrameDone);

    // Act 3: Immediate subsequent clean frame must recover seamlessly
    g_mock.simTimeUs += 5000U;
    feedFrameToCrsf(validFrame);
    EXPECT_EQ(RX_FRAME_COMPLETE, crsfFrameStatus(&rxRuntimeState));
    EXPECT_EQ(1500U, crsfChannelData[0]);
    EXPECT_EQ(1811U, crsfChannelData[4]);
}

TEST_F(AriesRxCrsfTest, Category2_Test10_CrsfInterFrameTimeout_1748usReset) {
    // Arrange
    uint16_t chOld[CRSF_MAX_CHANNEL], chNew[CRSF_MAX_CHANNEL];
    makeDefaultChannels(chOld, 172, 172, 172, 172, 172);
    makeDefaultChannels(chNew, 1811, 992, 500, 1400, 1811);
    const auto partialFrame = buildRcChannelsFrame(CRSF_ADDRESS_FLIGHT_CONTROLLER, chOld);
    const auto freshFrame = buildRcChannelsFrame(CRSF_ADDRESS_FLIGHT_CONTROLLER, chNew);

    // Act: Feed first 10 bytes, then wait > 1748 us (inter-frame reset threshold) before new frame
    for (size_t i = 0; i < 10; i++) {
        g_mock.simTimeUs += 21U;
        crsfDataReceive(partialFrame[i], &rxRuntimeState);
    }
    EXPECT_FALSE(crsfFrameDone);

    g_mock.simTimeUs += (CRSF_INTERFRAME_US + 2U); // > 1748 us
    feedFrameToCrsf(freshFrame);

    // Assert: Stale partial bytes were discarded and freshFrame decoded without byte shift
    ASSERT_TRUE(crsfFrameDone);
    EXPECT_EQ(RX_FRAME_COMPLETE, crsfFrameStatus(&rxRuntimeState));
    EXPECT_EQ(1811U, crsfChannelData[0]);
    EXPECT_EQ(992U, crsfChannelData[1]);
    EXPECT_EQ(500U, crsfChannelData[2]);
    EXPECT_EQ(1400U, crsfChannelData[3]);
    EXPECT_EQ(1811U, crsfChannelData[4]);
}

// =============================================================================
// Category 3: 16-Channel Unpacking, Scaling, Failsafe & TX (Tests 11-17)
// =============================================================================

TEST_F(AriesRxCrsfTest, Category3_Test11_Crsf16Channels_BitBoundaryUnpacking) {
    // Arrange: Distinct alternating 11-bit boundary patterns across all 16 channels
    const uint16_t expected[CRSF_MAX_CHANNEL] = {
        0U, 2047U, 172U, 1811U, 992U, 0x555U, 0x2AAU, 1024U,
        1023U, 1U, 2046U, 1234U, 567U, 1999U, 42U, 2047U
    };
    const auto frame = buildRcChannelsFrame(CRSF_ADDRESS_FLIGHT_CONTROLLER, expected);

    // Act
    feedFrameToCrsf(frame);
    ASSERT_EQ(RX_FRAME_COMPLETE, crsfFrameStatus(&rxRuntimeState));

    // Assert: Exact bit-accurate extraction for all 16 channels
    for (int i = 0; i < CRSF_MAX_CHANNEL; i++) {
        EXPECT_EQ(expected[i], crsfChannelData[i]) << "Channel " << i << " mismatch";
    }
}

TEST_F(AriesRxCrsfTest, Category3_Test12_CrsfLinearScaling_172Mid992Max1811) {
    // Arrange
    uint16_t ch[CRSF_MAX_CHANNEL];
    makeDefaultChannels(ch, 172, 992, 1811, 172, 992);
    const auto frame = buildRcChannelsFrame(CRSF_ADDRESS_FLIGHT_CONTROLLER, ch);

    // Act
    feedFrameToCrsf(frame);
    ASSERT_EQ(RX_FRAME_COMPLETE, crsfFrameStatus(&rxRuntimeState));

    // Assert: Linear mapping 172->988us, 992->1500us, 1811->2012us (±1 us rounding)
    EXPECT_NEAR(988.0f, crsfReadRawRC(&rxRuntimeState, 0), 1.0f);
    EXPECT_NEAR(1500.0f, crsfReadRawRC(&rxRuntimeState, 1), 1.0f);
    EXPECT_NEAR(2012.0f, crsfReadRawRC(&rxRuntimeState, 2), 1.0f);
}

TEST_F(AriesRxCrsfTest, Category3_Test13_CrsfSubsetChannels0x17_BoundsAndResolutions) {
    // Arrange 1: Valid captured 0x17 subset RC frame starting at Ch4 (11-bit resolution)
    const uint8_t validSubset[] = { 0xC8, 0x09, 0x17, 0x24, 0x00, 0xA0, 0xD9, 0xE9, 0xFF, 0x0F, 0xD1 };
    feedFrameToCrsf(std::vector<uint8_t>(validSubset, validSubset + sizeof(validSubset)));

    // Assert 1
    EXPECT_EQ(RX_FRAME_COMPLETE, crsfFrameStatus(&rxRuntimeState));
    EXPECT_EQ(0U, crsfChannelData[4]);
    EXPECT_EQ(820U, crsfChannelData[5]);
    EXPECT_EQ(1959U, crsfChannelData[6]);
    EXPECT_EQ(2047U, crsfChannelData[7]);

    // Arrange 2: Out-of-bounds startChannel = 16 (CRSF_MAX_CHANNEL) must drop frame safely
    g_mock.simTimeUs += 5000U;
    uint8_t oobSubset[8] = {
        CRSF_ADDRESS_FLIGHT_CONTROLLER, 6, CRSF_FRAMETYPE_SUBSET_RC_CHANNELS_PACKED,
        (uint8_t)(CRSF_MAX_CHANNEL | (CRSF_SUBSET_RC_RES_CONF_10B << CRSF_SUBSET_RC_STARTING_CHANNEL_BITS)),
        0x55, 0xA9, 0x0A, 0x00
    };
    oobSubset[7] = computeCrsfDvbS2Crc(&oobSubset[2], 5);
    feedFrameToCrsf(std::vector<uint8_t>(oobSubset, oobSubset + sizeof(oobSubset)));

    // Assert 2: Dropped cleanly with zero OOB array corruption
    EXPECT_EQ(RX_FRAME_DROPPED, crsfFrameStatus(&rxRuntimeState));
}

TEST_F(AriesRxCrsfTest, Category3_Test14_CrsfAux1ArmSwitchAndRxModes) {
    // Arrange: Configure BOXARM on AUX1 active range 1700..2100 us, initialize full RX stack
    modeActivationConditionsMutable(0)->modeId = BOXARM;
    modeActivationConditionsMutable(0)->auxChannelIndex = 0; // AUX1 == channel 4
    modeActivationConditionsMutable(0)->range.startStep = CHANNEL_VALUE_TO_STEP(1700);
    modeActivationConditionsMutable(0)->range.endStep = CHANNEL_VALUE_TO_STEP(2100);
    analyzeModeActivationConditions();

    rxInit();
    ASSERT_TRUE(crsfRxIsActive());

    // Clear initial failsafe/timeout window cleanly
    g_mock.failsafeActive = false;

    // Act 1: AUX1 = 172 (~988 us, Disarmed)
    uint16_t ch[CRSF_MAX_CHANNEL];
    makeDefaultChannels(ch, 992, 992, 992, 172, 172);
    feedFrameToCrsf(buildRcChannelsFrame(CRSF_ADDRESS_FLIGHT_CONTROLLER, ch));
    rxFrameCheck(g_mock.simTimeUs, 1000);
    ASSERT_TRUE(calculateRxChannelsAndUpdateFailsafe(g_mock.simTimeUs));
    updateActivatedModes();
    EXPECT_FALSE(IS_RC_MODE_ACTIVE(BOXARM));
    EXPECT_TRUE(isRxReceivingSignal());
    EXPECT_TRUE(rxAreFlightChannelsValid());

    // Act 2: Flip AUX1 = 1811 (~2012 us, Armed)
    g_mock.simTimeUs += 6667U;
    g_mock.simTimeMs += 7U;
    ch[4] = 1811U;
    feedFrameToCrsf(buildRcChannelsFrame(CRSF_ADDRESS_FLIGHT_CONTROLLER, ch));
    rxFrameCheck(g_mock.simTimeUs, 6667);
    ASSERT_TRUE(calculateRxChannelsAndUpdateFailsafe(g_mock.simTimeUs));
    updateActivatedModes();
    EXPECT_TRUE(IS_RC_MODE_ACTIVE(BOXARM));

    // Act 3: Flip AUX1 back to 172 (Disarm)
    g_mock.simTimeUs += 6667U;
    g_mock.simTimeMs += 7U;
    ch[4] = 172U;
    feedFrameToCrsf(buildRcChannelsFrame(CRSF_ADDRESS_FLIGHT_CONTROLLER, ch));
    rxFrameCheck(g_mock.simTimeUs, 6667);
    ASSERT_TRUE(calculateRxChannelsAndUpdateFailsafe(g_mock.simTimeUs));
    updateActivatedModes();
    EXPECT_FALSE(IS_RC_MODE_ACTIVE(BOXARM));
}

TEST_F(AriesRxCrsfTest, Category3_Test15_CrsfFailsafe_NoFramesTransition) {
    // Arrange: Initialize RX and establish active CRSF link with valid mid/high throttle
    rxInit();
    uint16_t ch[CRSF_MAX_CHANNEL];
    makeDefaultChannels(ch, 1200, 992, 992, 992, 1811);
    feedFrameToCrsf(buildRcChannelsFrame(CRSF_ADDRESS_FLIGHT_CONTROLLER, ch));
    rxFrameCheck(g_mock.simTimeUs, 1000);
    ASSERT_TRUE(calculateRxChannelsAndUpdateFailsafe(g_mock.simTimeUs));
    ASSERT_TRUE(isRxReceivingSignal());
    ASSERT_TRUE(rxAreFlightChannelsValid());

    // Act: Stop CRSF frames and advance past 150 ms RXLOSS trigger + 300 ms bad-pulse window
    g_mock.simTimeUs += RXLOSS_TIMEOUT_US + 1000U;
    g_mock.simTimeMs += 151U;
    rxFrameCheck(g_mock.simTimeUs, RXLOSS_TIMEOUT_US + 1000U);
    EXPECT_FALSE(isRxReceivingSignal());

    // Simulate stage-2 failsafe active after failsafe delay expires
    g_mock.failsafeActive = true;
    g_mock.simTimeUs += 350000U;
    g_mock.simTimeMs += 350U;
    rxFrameCheck(g_mock.simTimeUs, 350000U);
    ASSERT_TRUE(calculateRxChannelsAndUpdateFailsafe(g_mock.simTimeUs));

    // Assert: Flight channels marked invalid, failsafe throttle & midrc roll applied
    EXPECT_FALSE(rxAreFlightChannelsValid());
    EXPECT_EQ(1000.0f, rcData[THROTTLE]);
    EXPECT_EQ((float)rxConfig()->midrc, rcData[ROLL]);
    EXPECT_GT(g_mock.failsafeFailedCount, 0U);
}

TEST_F(AriesRxCrsfTest, Category3_Test16_CrsfTelemetryWriteAndBoundedTxDrain) {
    // Arrange
    ASSERT_TRUE(crsfRxInit(rxConfig(), &rxRuntimeState));
    EXPECT_TRUE(crsfRxIsTelemetryBufEmpty());

    // 28-byte telemetry payload tests >16-byte chunked non-blocking UART1 TX drain
    uint8_t telemPayload[28];
    telemPayload[0] = CRSF_ADDRESS_RADIO_TRANSMITTER;
    telemPayload[1] = 26;
    telemPayload[2] = CRSF_FRAMETYPE_BATTERY_SENSOR;
    for (int i = 3; i < 27; i++) {
        telemPayload[i] = (uint8_t)(i * 3);
    }
    telemPayload[27] = computeCrsfDvbS2Crc(&telemPayload[2], 25);

    // Act
    crsfRxWriteTelemetryData(telemPayload, sizeof(telemPayload));
    EXPECT_FALSE(crsfRxIsTelemetryBufEmpty());
    crsfRxSendTelemetryData();

    // Assert: Buffer consumed, exact bytes written via <= 16-byte burst chunks
    EXPECT_TRUE(crsfRxIsTelemetryBufEmpty());
    ASSERT_EQ(sizeof(telemPayload), g_mock.uart1.txStream.size());
    EXPECT_EQ(0, memcmp(telemPayload, g_mock.uart1.txStream.data(), sizeof(telemPayload)));
    ASSERT_GE(g_mock.uart1.txBurstChunks.size(), 2U);
    for (size_t chunkSize : g_mock.uart1.txBurstChunks) {
        EXPECT_LE(chunkSize, 16U);
    }
}

TEST_F(AriesRxCrsfTest, Category3_Test17_CrsfRxBind_CommandPacket) {
    // Act 1: crsfRxBind when openSerialPort returns NULL must safely no-op
    g_mock.forceNullSerialPort = true;
    EXPECT_FALSE(crsfRxInit(rxConfig(), &rxRuntimeState));
    g_mock.forceNullSerialPort = false;
    crsfRxBind();
    EXPECT_TRUE(g_mock.uart1.txStream.empty());

    // Act 2: crsfRxBind on open UART1 must emit exact 9-byte CRSF bind command
    rxConfigMutable()->rx_uart = SERIAL_PORT_USART1;
    ASSERT_TRUE(crsfRxInit(rxConfig(), &rxRuntimeState));
    crsfRxBind();

    // Assert
    const uint8_t expectedBind[] = {
        CRSF_SYNC_BYTE, 0x07, CRSF_FRAMETYPE_COMMAND,
        CRSF_ADDRESS_CRSF_RECEIVER, CRSF_ADDRESS_FLIGHT_CONTROLLER,
        CRSF_COMMAND_SUBCMD_RX, CRSF_COMMAND_SUBCMD_RX_BIND,
        0x9E, 0xE8
    };
    ASSERT_EQ(sizeof(expectedBind), g_mock.uart1.txStream.size());
    EXPECT_EQ(0, memcmp(expectedBind, g_mock.uart1.txStream.data(), sizeof(expectedBind)));
    EXPECT_EQ(expectedBind[7], computeCrsfPoly0xBaCrc(&expectedBind[2], 5));
    EXPECT_EQ(expectedBind[8], computeCrsfDvbS2Crc(&expectedBind[2], 6));
}

// =============================================================================
// Category 4: Link Statistics (0x14 / 0x1D), Diversity & Decay (Tests 18-20)
// =============================================================================

TEST_F(AriesRxCrsfTest, Category4_Test18_CrsfLinkStats0x14_Getters) {
    // Arrange
    rxInit();
    ASSERT_EQ(RSSI_SOURCE_RX_PROTOCOL_CRSF, rssiSource);

    // Uplink ant1=-65dBm, ant2=-88dBm, LQ=99%, SNR=8dB, active=0, rfMode=2 (150Hz), TXPwrIdx=3 (100mW)
    const auto stats = buildCrsfLinkStatsFrame(65, 88, 99, 8, 0, 2, 3);

    // Act
    feedFrameToCrsf(stats);
    for (int i = 0; i < 4; i++) {
        g_mock.simTimeUs += 10000U;
        updateRSSI(g_mock.simTimeUs);
    }

    // Assert: 0x14 Link Statistics (USE_CRSF_LINK_STATISTICS + USE_RX_RSSI_DBM + USE_RX_RSNR) active on ARIESV3
    EXPECT_EQ(-65, getRssiDbm());
    EXPECT_EQ(0, getActiveAntenna());
    EXPECT_EQ(8, getRsnr());
    EXPECT_GT(getRssi(), 0U);
#ifdef USE_RX_LINK_QUALITY_INFO
    EXPECT_EQ(LQ_SOURCE_RX_PROTOCOL_CRSF, linkQualitySource);
    EXPECT_EQ(99U, rxGetLinkQuality());
    EXPECT_EQ(99U, rxGetLinkQualityPercent());
    EXPECT_EQ(2U, rxGetRfMode());
#endif
#ifdef USE_RX_LINK_UPLINK_POWER
    EXPECT_EQ(100U, rxGetUplinkTxPwrMw());
#endif
}

TEST_F(AriesRxCrsfTest, Category4_Test19_CrsfDiversitySwitchAndV3TxStats0x1D) {
    // Arrange
    rxInit();

    // Act 1: Antenna 0 fades to -110 dBm while Antenna 1 stays strong at -52 dBm -> diversity switches to ant 1
    feedFrameToCrsf(buildCrsfLinkStatsFrame(110, 52, 95, 6, 1, 2, 5));
    for (int i = 0; i < 4; i++) {
        g_mock.simTimeUs += 10000U;
        updateRSSI(g_mock.simTimeUs);
    }

    // Assert 1
    EXPECT_EQ(1, getActiveAntenna());
    EXPECT_EQ(-52, getRssiDbm());
#ifdef USE_RX_LINK_UPLINK_POWER
    EXPECT_EQ(1000U, rxGetUplinkTxPwrMw()); // index 5 == 1000 mW
#endif

#if defined(USE_CRSF_V3)
    // Act 2: CRSF v3 TX Link Statistics (0x1D): uplink -72dBm, 85%, LQ=92, SNR=4
    g_mock.simTimeUs += 5000U;
    feedFrameToCrsf(buildCrsfLinkStatsTxFrame(72, 85, 92, 4));
    for (int i = 0; i < 4; i++) {
        g_mock.simTimeUs += 10000U;
        updateRSSI(g_mock.simTimeUs);
    }

    // Assert 2
    EXPECT_EQ(-72, getRssiDbm());
    EXPECT_EQ(92U, rxGetLinkQuality());
    EXPECT_EQ(4, getRsnr());
#endif
}

TEST_F(AriesRxCrsfTest, Category4_Test20_CrsfLinkStats_250msDecay) {
    // Arrange: Populate strong link stats first
    rxInit();
    feedFrameToCrsf(buildCrsfLinkStatsFrame(60, 62, 100, 10, 0, 2, 3));
    for (int i = 0; i < 4; i++) {
        g_mock.simTimeUs += 10000U;
        updateRSSI(g_mock.simTimeUs);
    }
    ASSERT_EQ(-60, getRssiDbm());
#ifdef USE_RX_LINK_QUALITY_INFO
    ASSERT_EQ(100U, rxGetLinkQuality());
#endif

    // Act: Silence > 250 ms triggers crsfCheckRssi() inside crsfFrameStatus()
    g_mock.simTimeUs += CRSF_LINK_STATS_TIMEOUT + 10000U;
    EXPECT_EQ(RX_FRAME_PENDING, crsfFrameStatus(&rxRuntimeState));

    // Assert: Zeroed/floored cleanly on timeout (prevents frozen positive RSSI display)
    EXPECT_EQ(0U, getRssi());
    EXPECT_EQ(CRSF_RSSI_MIN, getRssiDbm());
    EXPECT_EQ(CRSF_SNR_MIN, getRsnr());
#ifdef USE_RX_LINK_QUALITY_INFO
    EXPECT_EQ(0U, rxGetLinkQuality());
#endif
}

// =============================================================================
// Category 5: Safe Battery ADC Cache, Zero-DMA & 1 kHz Budget (Tests 21-24)
// =============================================================================

TEST_F(AriesRxCrsfTest, Category5_Test21_SafeBatteryAdcCacheAndScaling) {
    // Arrange: Default ARIES safe nominal cache {2048, 0, 1024, 0} & standard 11:1 divider config
    const voltageSensorADCConfig_t *cfg = voltageSensorADCConfig(0);
    ASSERT_EQ(110U, cfg->vbatscale);
    ASSERT_EQ(10U, cfg->vbatresdivval);
    ASSERT_EQ(1U, cfg->vbatresdivmultiplier);
    ASSERT_EQ(3300U, getVrefMv());

    // Assert: Safe stub adcGetValue returns 0 when USE_ADC is disabled (Finding 11)
    EXPECT_EQ(0U, adcGetValue(ADC_BATTERY));
    EXPECT_EQ(0U, adcGetValue(ADC_CURRENT));
    EXPECT_EQ(0U, adcGetValue(ADC_RSSI));

    // Act & Assert: Real voltageAdcToVoltage integer scaling across nominal, 3S (~12.6V), 0V, and full-scale 4095
    EXPECT_EQ(1815U, voltageAdcToVoltage(2048U, cfg)); // 18.15V
    EXPECT_EQ(1259U, voltageAdcToVoltage(1420U, cfg)); // 12.59V (3S nominal)
    EXPECT_EQ(0U,    voltageAdcToVoltage(0U, cfg));    // 0.00V
    EXPECT_EQ(3630U, voltageAdcToVoltage(4095U, cfg)); // 36.30V, zero 32-bit overflow
}

TEST_F(AriesRxCrsfTest, Category5_Test22_BatteryPresence_VoltageMeterNoneAntiLockout) {
    // Act 1: With VOLTAGE_METER_NONE, batteryUpdateVoltage() calls voltageMeterReset()
    // each tick, keeping voltageStableBits == 0 so state stays safely in BATTERY_INIT.
    batteryConfigMutable()->voltageMeterSource = VOLTAGE_METER_NONE;
    batteryConfigMutable()->currentMeterSource = CURRENT_METER_NONE;
    batteryInit();
    EXPECT_FALSE(isBatteryVoltageConfigured());
    EXPECT_EQ(BATTERY_INIT, getBatteryState());

    for (int i = 0; i < 15; i++) {
        g_mock.simTimeMs += VOLTAGE_STABLE_TICK_MS;
        g_mock.simTimeUs += VOLTAGE_STABLE_TICK_MS * 1000U;
        batteryUpdateVoltage(g_mock.simTimeUs);
        batteryUpdatePresence();
        batteryUpdateStates(g_mock.simTimeUs);
        EXPECT_NE(BATTERY_WARNING, getBatteryState());
        EXPECT_NE(BATTERY_CRITICAL, getBatteryState());
    }
    EXPECT_EQ(0U, getBatteryVoltage());
    EXPECT_EQ(BATTERY_INIT, getVoltageState());
    EXPECT_EQ(BATTERY_INIT, getBatteryState());

    // Act 2: Even if configured to VOLTAGE_METER_ADC while #undef USE_ADC is active,
    // stable 0V reading stabilizes across 10+ ticks and settles in BATTERY_NOT_PRESENT.
    batteryConfigMutable()->voltageMeterSource = VOLTAGE_METER_ADC;
    batteryInit();
    for (int i = 0; i < 15; i++) {
        g_mock.simTimeMs += VOLTAGE_STABLE_TICK_MS;
        g_mock.simTimeUs += VOLTAGE_STABLE_TICK_MS * 1000U;
        batteryUpdateVoltage(g_mock.simTimeUs);
        batteryUpdatePresence();
        batteryUpdateStates(g_mock.simTimeUs);
        EXPECT_NE(BATTERY_WARNING, getBatteryState());
        EXPECT_NE(BATTERY_CRITICAL, getBatteryState());
    }

    // Assert 2: Stabilizes cleanly at BATTERY_NOT_PRESENT (never warning/critical lockout)
    EXPECT_EQ(0U, getBatteryVoltage());
    EXPECT_EQ(BATTERY_NOT_PRESENT, getVoltageState());
    EXPECT_EQ(BATTERY_NOT_PRESENT, getBatteryState());
}

TEST_F(AriesRxCrsfTest, Category5_Test23_U9I2c2TimeoutCacheRetentionAndMmioBounds) {
    // Arrange: Verify THEJAS32 I2C (0x10000800) and ADC (0x10001000) hardware base bounds
    EXPECT_EQ(ARIES_I2C_MMIO_ADDR, (uint32_t)THEJAS32_I2C_BASE);
    EXPECT_EQ(ARIES_ADC_MMIO_ADDR, (uint32_t)THEJAS32_ADC_BASE);
    EXPECT_EQ(2048U, g_mock.ariesAdcCache[0]);

    // Act 1: Healthy response updates cached voltage cleanly
    const U9PollResult ok = simulateNonBlockingU9Poll(false, 64U);
    EXPECT_TRUE(ok.success);
    EXPECT_FALSE(ok.timedOut);
    EXPECT_EQ(1420U, ok.voltageRaw);

    // Act 2: Simulated U9 missing / I2C2 NACK / busy bus triggers bounded abort & preserves cache
    const U9PollResult fault = simulateNonBlockingU9Poll(true, 64U);
    EXPECT_FALSE(fault.success);
    EXPECT_TRUE(fault.timedOut);
    EXPECT_EQ(64U, fault.pollsExecuted);
    EXPECT_EQ(1420U, fault.voltageRaw); // Cache retained (no false drop to 0)
    // Safe stub adcGetValue must still return 0 until USE_ADC I2C2 driver is enabled
    EXPECT_EQ(0U, adcGetValue(ADC_BATTERY));
}

TEST_F(AriesRxCrsfTest, Category5_Test24_LoopHeadroom_1kHzPidSerialAndAdcBudget) {
    // Arrange: Full 1.0 kHz flight tick execution budget on 100 MHz RV32IM soft-float silicon
    constexpr uint32_t LOOP_PERIOD_US        = 1000U; // DEFAULT_PID_PROCESS_DENOM == 8 -> 1 kHz
    constexpr uint32_t SOFTFLOAT_PID_WCET_US = 290U;  // Ground Truth 4 measured WCET
    constexpr uint32_t IMU_POLLED_BURST_US   = 10U;   // 15B @ 12.5 MHz = 9.6 us
    constexpr uint32_t CRSF_RX_DECODE_US     = 25U;   // 16ch unpack + failsafe update
    constexpr uint32_t UART_TX_BOUNDED_US    = 2U;    // 16B bounded burst FIFO write
    constexpr uint32_t ADC_CACHE_READ_US     = 3U;    // Cached voltage read, zero DMA overhead

    // Act
    const uint32_t activeCpuUs = SOFTFLOAT_PID_WCET_US + IMU_POLLED_BURST_US +
                                 CRSF_RX_DECODE_US + UART_TX_BOUNDED_US + ADC_CACHE_READ_US;
    const uint32_t idleUs = LOOP_PERIOD_US - activeCpuUs;
    const double idlePct = ((double)idleUs / (double)LOOP_PERIOD_US) * 100.0;

    // Assert: <= 350 us worst-case loop load -> >= 65% idle headroom maintained
    EXPECT_LE(activeCpuUs, 350U);
    EXPECT_GE(idlePct, 65.0);
}

static thejas32_irq_handler_t g_hostCrsfIrqTable[32];

TEST_F(AriesRxCrsfTest, Category1_Test25_RealSerialUartAriesC_420kCrsfIsrIngest) {
    thejas32_host_reset_all();
    IOInitGlobal();

    serialPinConfig_t pinCfg = {};
    pinCfg.ioTagTx[0] = DEFIO_TAG_E__PA30;
    pinCfg.ioTagRx[0] = DEFIO_TAG_E__PA31;
    pinCfg.ioTagTx[1] = DEFIO_TAG_E__PA16;
    pinCfg.ioTagRx[1] = DEFIO_TAG_E__PA17;
    uartPinConfigure(&pinCfg);

    serialPort_t *port1 = uartOpen(SERIAL_PORT_USART1, crsfDataReceive, &rxRuntimeState,
                                   CRSF_BAUDRATE, CRSF_PORT_MODE, (portOptions_e)CRSF_PORT_OPTIONS);
    ASSERT_NE(nullptr, port1);
    ASSERT_NE(nullptr, g_hostCrsfIrqTable[THEJAS32_IRQ_UART1]);

    // Check real 16550 registers configured for 420k baud @ 100 MHz (Divisor 15), 8N1, FCR=0, RX-only IE
    EXPECT_EQ(0x03U, THEJAS32_UART(1).UART_LCR);
    EXPECT_EQ(0x00U, THEJAS32_UART(1).UART_IIR_FCR & 0xFFU);
    EXPECT_EQ(0x01U, THEJAS32_UART(1).UART_IE);
    EXPECT_NE(0U, THEJAS32_INTR_CTRL.INTR_EN & (1UL << THEJAS32_IRQ_UART1));

    // Re-read divisor LSB (DLL shares 0x00 with THR/RBR; offset 0x04 already holds IER=0x01 after DLAB cleared)
    THEJAS32_UART(1).UART_LCR = 0x83U;
    EXPECT_EQ(15U, THEJAS32_UART(1).UART_DR & 0xFFU);
    THEJAS32_UART(1).UART_LCR = 0x03U;

    // Stream a real 26-byte CRSF frame byte-by-byte through THEJAS32 UART1 ISR -> crsfDataReceive
    g_mock.simTimeUs += 5000U;
    uint16_t ch[CRSF_MAX_CHANNEL];
    makeDefaultChannels(ch, 992, 172, 1811, 500, 1500);
    const auto frame = buildRcChannelsFrame(CRSF_ADDRESS_FLIGHT_CONTROLLER, ch);
    for (uint8_t byte : frame) {
        THEJAS32_UART(1).UART_DR = byte;
        THEJAS32_UART(1).UART_LSR = 0x61U; // DR | THRE | TEMT
        g_hostCrsfIrqTable[THEJAS32_IRQ_UART1]();
        g_mock.simTimeUs += 21U;
    }

    EXPECT_TRUE(crsfFrameDone);
    EXPECT_EQ(RX_FRAME_COMPLETE, crsfFrameStatus(&rxRuntimeState));
    EXPECT_NEAR(1500.0f, crsfReadRawRC(&rxRuntimeState, 0), 1.0f);
    EXPECT_NEAR(988.5f, crsfReadRawRC(&rxRuntimeState, 1), 1.0f);
    EXPECT_NEAR(2012.5f, crsfReadRawRC(&rxRuntimeState, 2), 1.0f);
}

TEST_F(AriesRxCrsfTest, Category5_Test26_RealStubDriversC_PersistentObjectBef1InitAndRetention) {
    // Cold boot simulation: corrupt magic causes persistentObjectInit to wipe all slots & write 'Bef1'
    persistentObjectWrite(PERSISTENT_OBJECT_MAGIC, 0xBAD00001U);
    persistentObjectWrite(PERSISTENT_OBJECT_SERIALRX_BAUD, 99999U);
    persistentObjectInit();
    EXPECT_EQ(0x42656631U, persistentObjectRead(PERSISTENT_OBJECT_MAGIC));
    EXPECT_EQ(0U, persistentObjectRead(PERSISTENT_OBJECT_SERIALRX_BAUD));

    // Warm soft-reset simulation: valid 'Bef1' magic preserves stored CRSF baud rate slot
    persistentObjectWrite(PERSISTENT_OBJECT_SERIALRX_BAUD, 15U);
    persistentObjectInit();
    EXPECT_EQ(0x42656631U, persistentObjectRead(PERSISTENT_OBJECT_MAGIC));
    EXPECT_EQ(15U, persistentObjectRead(PERSISTENT_OBJECT_SERIALRX_BAUD));
}

TEST_F(AriesRxCrsfTest, Category1_Test27_RealSerialWriteBufBlocking_PolledTxDrainNoDeadlock) {
    thejas32_host_reset_all();
    IOInitGlobal();

    serialPinConfig_t pinCfg = {};
    pinCfg.ioTagTx[0] = DEFIO_TAG_E__PA30;
    pinCfg.ioTagRx[0] = DEFIO_TAG_E__PA31;
    uartPinConfigure(&pinCfg);

    serialPort_t *sp0 = uartOpen(SERIAL_PORT_UART0, nullptr, nullptr, 115200U, MODE_RXTX, SERIAL_NOT_INVERTED);
    ASSERT_NE(sp0, nullptr);

    // Pre-fill ring near full without THRE asserted, then verify serialWriteBufBlocking drains via uartPollTx
    THEJAS32_UART(0).UART_LSR = 0x00U;
    uint8_t prefill[240];
    memset(prefill, 'A', sizeof(prefill));
    serialWriteBuf(sp0, prefill, (int)sizeof(prefill));
    EXPECT_LT(serialTxBytesFree(sp0), 64U);

    THEJAS32_UART(0).UART_LSR = 0x60U;
    uint8_t block[64];
    for (size_t i = 0; i < sizeof(block); i++) {
        block[i] = (uint8_t)('0' + (i % 10U));
    }
    serialWriteBufBlockingShim(sp0, block, (int)sizeof(block));

    // Block enqueued after polling freed room, and flushing empties cleanly
    uartPollTx();
    while (!isSerialTransmitBufferEmpty(sp0)) {
        uartPollTx();
    }
    EXPECT_EQ(THEJAS32_UART(0).UART_DR & 0xFFU, (uint32_t)'3');
}

} // namespace

// =============================================================================
// External C Stubs for Unlinked Core / Target Symbols
// =============================================================================

extern "C" {

uint32_t SystemCoreClock = 100000000U;
int16_t debug[DEBUG16_VALUE_COUNT];
uint8_t debugMode = 0;

uint32_t micros(void) { return g_mock.simTimeUs; }
uint32_t microsISR(void) { return g_mock.simTimeUs; }
uint32_t millis(void) { return g_mock.simTimeMs; }

serialPort_t *openSerialPort(
    serialPortIdentifier_e identifier,
    serialPortFunction_e function,
    serialReceiveCallbackPtr rxCallback,
    void *rxCallbackData,
    uint32_t baudrate,
    portMode_e mode,
    portOptions_e options)
{
    if (identifier == SERIAL_PORT_NONE || g_mock.forceNullSerialPort) {
        return NULL;
    }
    g_mock.uart1.opened = true;
    g_mock.uart1.openedPortId = identifier;
    g_mock.uart1.openedFunction = function;
    g_mock.uart1.rxCallback = rxCallback;
    g_mock.uart1.rxCallbackData = rxCallbackData;
    g_mock.uart1.requestedBaud = baudrate;
    g_mock.uart1.openedMode = mode;
    g_mock.uart1.openedOptions = options;
    g_mock.uart1.configure16550(baudrate, options, mode);

    g_mock.mockSerialPort.vTable = &g_mockUart1VTable;
    g_mock.mockSerialPort.baudRate = baudrate;
    g_mock.mockSerialPort.mode = mode;
    g_mock.mockSerialPort.options = options;
    g_mock.mockSerialPort.rxCallback = rxCallback;
    g_mock.mockSerialPort.rxCallbackData = rxCallbackData;
    g_mock.mockSerialPort.identifier = (int8_t)identifier;
    return &g_mock.mockSerialPort;
}

void thejas32_interrupt_register(uint8_t irq_number, thejas32_irq_handler_t handler)
{
    if (irq_number < 32U) {
        g_hostCrsfIrqTable[irq_number] = handler;
    }
}

void thejas32_interrupt_enable(uint8_t irq_number)
{
    if (irq_number < 32U) {
        THEJAS32_INTR_CTRL.INTR_EN |= (1UL << irq_number);
    }
}

void thejas32_interrupt_disable(uint8_t irq_number)
{
    if (irq_number < 32U) {
        THEJAS32_INTR_CTRL.INTR_EN &= ~(1UL << irq_number);
    }
}

uint16_t getVrefMv(void) { return 3300U; }

bool telemetryCheckRxPortShared(serialPortIdentifier_e, SerialRXType) { return false; }
serialPort_t *telemetrySharedPort = NULL;
void crsfScheduleDeviceInfoResponse(void) {}
void crsfScheduleMspResponse(uint8_t) {}
void crsfScheduleTelemetryResponse(void) {}
bool crsfTelemetryUpdateCheck(timeUs_t, timeDelta_t) { return false; }
void crsfHandleDeviceInfoResponse(uint8_t *) {}
void crsfProcessDisplayPortCmd(uint8_t *) {}
bool bufferCrsfMspFrame(uint8_t *, int) { return true; }
bool crsfBaudNegotiationInProgress(void) { return g_mock.crsfBaudNegInProgress; }
uint32_t getCrsfCachedBaudrate(void) { return CRSF_BAUDRATE; }
void setCrsfDefaultSpeed(void) { g_mock.setCrsfDefaultSpeedCalls++; }
void crsfProcessCommand(uint8_t *frameStart)
{
    g_mock.crsfCmdProcessCount++;
    if (frameStart) {
        memcpy(g_mock.lastProcessedCmd, frameStart, 2);
    }
}

bool isEepromWriteInProgress(void) { return g_mock.eepromWriting; }
void saveConfigAndNotify(void) {}
void changePidProfile(uint8_t) {}
void changePidProfileFromCellCount(uint8_t) {}

task_t *getTask(unsigned taskId)
{
#ifdef USE_TELEMETRY
    return (taskId == TASK_TELEMETRY) ? &g_mockTelemetryTask : NULL;
#else
    (void)taskId;
    return NULL;
#endif
}

bool taskUpdateRxMainInProgress(void) { return g_mock.taskRxInProgress; }
void schedulerIgnoreTaskStateTime(void) {}
void schedulerIgnoreTaskExecRate(void) {}
bool schedulerGetIgnoreTaskExecTime(void) { return false; }
void schedulerIgnoreTaskExecTime(void) {}
void schedulerSetNextStateTime(timeDelta_t) {}

bool failsafeIsActive(void) { return g_mock.failsafeActive; }
bool failsafeIsReceivingRxData(void) { return true; }
void failsafeOnValidDataReceived(void) { g_mock.failsafeValidCount++; }
void failsafeOnValidDataFailed(void) { g_mock.failsafeFailedCount++; }
void failsafeOnRxSuspend(uint32_t) {}
void failsafeOnRxResume(void) {}
uint32_t failsafeFailurePeriodMs(void) { return 400U; }

bool isPPMDataBeingReceived(void) { return false; }
bool isPWMDataBeingReceived(void) { return false; }
void resetPPMDataReceivedState(void) {}
bool rxMspFrameComplete(void) { return false; }

bool srxl2RxInit(const rxConfig_t *, rxRuntimeState_t *) { return false; }
bool spektrumInit(const rxConfig_t *, rxRuntimeState_t *) { return false; }
bool sbusInit(const rxConfig_t *, rxRuntimeState_t *) { return false; }
bool sumdInit(const rxConfig_t *, rxRuntimeState_t *) { return false; }
bool sumhInit(const rxConfig_t *, rxRuntimeState_t *) { return false; }
bool xBusInit(const rxConfig_t *, rxRuntimeState_t *) { return false; }
bool ibusInit(const rxConfig_t *, rxRuntimeState_t *) { return false; }
bool jetiExBusInit(const rxConfig_t *, rxRuntimeState_t *) { return false; }
bool ghstRxInit(const rxConfig_t *, rxRuntimeState_t *) { return false; }
bool fportRxInit(const rxConfig_t *, rxRuntimeState_t *) { return false; }
bool mavlinkRxInit(const rxConfig_t *, rxRuntimeState_t *) { return false; }
void rxMspInit(const rxConfig_t *, rxRuntimeState_t *) {}
void rxPwmInit(const rxConfig_t *, rxRuntimeState_t *) {}

void beeper(beeperMode_e) {}
void beeperConfirmationBeeps(uint8_t) {}
void pinioBoxTaskControl(void) {}

throttleStatus_e calculateThrottleStatus(void) { return THROTTLE_LOW; }
float mixerGetThrottle(void) { return 0.0f; }
bool isMotorProtocolEnabled(void) { return true; }
bool isUpright(void) { return true; }

void currentMeterADCInit(void) {}
void currentMeterADCRefresh(int32_t) {}
void currentMeterADCRead(currentMeter_t *) {}
void currentMeterReset(currentMeter_t *) {}
void currentMeterVirtualInit(void) {}
void currentMeterVirtualRefresh(int32_t, bool, bool, int32_t) {}
void currentMeterVirtualRead(currentMeter_t *) {}

} // extern "C"
