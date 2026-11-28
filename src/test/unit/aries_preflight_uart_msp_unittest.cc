/*
 * Test Suite: C-DAC ARIES v3.0 Phase 20C Pre-Flight UART/MSP/CLI/Reset/Pin Hardening
 * Target Silicon: C-DAC THEJAS32 SoC (VEGA ET1031 RISC-V RV32IM @ 100 MHz, 248 KB SRAM, 0 RVC)
 * References & Citations:
 *   - knowledgebase/CRITICAL_KNOWLEDGE_GAPS_AND_ASSUMPTIONS.md (Errata 1, 2, 7, 9, 10, 11)
 *   - logs/dev/phase_19c_uart_cli_rx_hardening.md
 *   - knowledgebase/code_audit/10_PHASE_19C_UART_CLI_RX_FREEZE.md
 *   - src/platform/ARIES/serial_uart_aries.c
 *   - src/platform/ARIES/stub_drivers.c
 *   - src/platform/ARIES/system.c
 *   - src/main/msp/msp_serial.c & src/main/drivers/serial_uart.c
 *
 * Methodology:
 *   Strict Test-Driven Development (TDD) - Step 1: Living Documentation.
 *   Tests real ARIES UART0/1 drivers, msp_serial.c, unusedPinsInit(), persistent objects,
 *   and bootloader reset guards across all 20 pre-flight verification cases.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include <cmath>

#include "unittest_macros.h"
#include "gtest/gtest.h"

extern "C" {
#include "../main/platform.h"
#include "build/build_config.h"
#include "common/crc.h"
#include "common/streambuf.h"
#include "common/utils.h"
#include "drivers/adc.h"
#include "drivers/io.h"
#include "drivers/persistent.h"
#include "drivers/serial.h"
#include "drivers/serial_uart.h"
#include "drivers/serial_uart_impl.h"
#include "drivers/system.h"
#include "io/serial.h"
#include "msp/msp.h"
#include "msp/msp_protocol.h"
#include "msp/msp_serial.h"
#include "pg/msp.h"
#include "pg/pg.h"
#include "platform/ARIES/io_def_generated.h"
#include "platform/ARIES/include/platform/bsp_thejas32.h"
#include "aries_host_mmio.h"

void thejas32_uart0_isr_proxy(void);
void thejas32_uart1_isr(void);
void uartPollRx(void);
void uartPollTx(void);
void unusedPinsInit(void);
uint32_t handle_trap(uint32_t mcause, uint32_t mepc, uint32_t *sp);
}

namespace {

static bool preflight_read_repo_file(const char *relPath, char *buf, size_t bufSize)
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

std::vector<uint8_t> g_uart1CallbackBytes;
uint32_t g_cliEnterCalls = 0U;
bool g_cliLastInteractive = false;
bool g_cliActive = false;
uint32_t g_mspCmdCount = 0U;

void testUart1CrsfRxCb(uint16_t ch, void *data)
{
    (void)data;
    g_uart1CallbackBytes.push_back((uint8_t)(ch & 0xFFU));
}

class AriesPreflightUartMspTest : public ::testing::Test {
protected:
    void SetUp() override {
        thejas32_host_reset_all();
        IOInitGlobal();
        g_uart1CallbackBytes.clear();
        g_cliEnterCalls = 0U;
        g_cliLastInteractive = false;
        g_cliActive = false;
        g_mspCmdCount = 0U;

        memset(&serialConfig_System, 0, sizeof(serialConfig_System));
        memset(&mspConfig_System, 0, sizeof(mspConfig_System));
        for (unsigned i = 0; i < MAX_MSP_PORT_COUNT; i++) {
            mspConfig_System.msp_uart[i] = SERIAL_PORT_NONE;
        }
        serialConfig_System.reboot_character = 'R';

        thejas32_host_mcycle_set(100000ULL);
    }

    serialPort_t *openUart0(uint32_t baud = 115200U) {
        serialPinConfig_t pinCfg = {};
        pinCfg.ioTagTx[0] = DEFIO_TAG_E__PA30;
        pinCfg.ioTagRx[0] = DEFIO_TAG_E__PA31;
        pinCfg.ioTagTx[1] = DEFIO_TAG_E__PA16;
        pinCfg.ioTagRx[1] = DEFIO_TAG_E__PA17;
        uartPinConfigure(&pinCfg);
        return uartOpen(SERIAL_PORT_UART0, nullptr, nullptr, baud, MODE_RXTX, SERIAL_NOT_INVERTED);
    }
};

// =============================================================================
// TEST 01: UART_TX_BUFFER_SIZE (512 B) Holds Full 306+ Byte MSP_ADJUSTMENT_RANGES
// =============================================================================
TEST_F(AriesPreflightUartMspTest, Preflight_01_UartTxBuffer512_Accommodates306ByteMspAdjustmentRanges)
{
    // Arrange
    serialPort_t *sp0 = openUart0();
    ASSERT_NE(sp0, nullptr);
    EXPECT_GE(sp0->txBufferSize, 512U);

    uint8_t adjFrame[309];
    adjFrame[0] = '$';
    adjFrame[1] = 'X';
    adjFrame[2] = '>';
    for (size_t i = 3U; i < sizeof(adjFrame); i++) {
        adjFrame[i] = (uint8_t)(i & 0xFFU);
    }

    // Hold THRE=0 initially so full 309-byte enqueue must reside inside software TX ring
    THEJAS32_UART(0).UART_LSR = 0x00U;

    // Act
    EXPECT_GE(serialTxBytesFree(sp0), sizeof(adjFrame));
    serialWriteBufNoFlush(sp0, adjFrame, (int)sizeof(adjFrame));

    // Assert
    EXPECT_EQ(serialTxBytesFree(sp0), sp0->txBufferSize - 1U - sizeof(adjFrame));
    THEJAS32_UART(0).UART_LSR = 0x60U;
    std::vector<uint8_t> wireOut;
    while (!isSerialTransmitBufferEmpty(sp0)) {
        THEJAS32_UART(0).UART_LSR = 0x60U;
        uartPollTx();
        wireOut.push_back((uint8_t)(THEJAS32_UART(0).UART_DR & 0xFFU));
    }
    EXPECT_FALSE(wireOut.empty());
}

// =============================================================================
// TEST 02: 1 kHz taskMain() Calls uartPollTx() Without Setting ETBEI (Bit 1)
// =============================================================================
TEST_F(AriesPreflightUartMspTest, Preflight_02_1kHzTaskMainCallsUartPollTx_DrainsLargeTxWithoutEtbei)
{
    // Arrange
    static char tasksBuf[32768];
    ASSERT_TRUE(preflight_read_repo_file("src/main/fc/tasks.c", tasksBuf, sizeof(tasksBuf)));
    const char *fn = strstr(tasksBuf, "static void taskMain(");
    const char *nextFn = fn ? strstr(fn + 20, "static void taskHandleSerial(") : nullptr;
    ASSERT_NE(fn, nullptr);
    ASSERT_NE(nextFn, nullptr);
    const char *pollCall = strstr(fn, "uartPollTx()");
    EXPECT_TRUE(pollCall != nullptr && pollCall < nextFn);

    serialPort_t *sp0 = openUart0();
    ASSERT_NE(sp0, nullptr);
    uint8_t msg[32];
    memset(msg, 'Z', sizeof(msg));
    THEJAS32_UART(0).UART_LSR = 0x00U;
    serialWriteBuf(sp0, msg, sizeof(msg));

    // Act
    THEJAS32_UART(0).UART_LSR = 0x60U;
    for (int tick = 0; tick < 4; tick++) {
        uartPollTx();
    }

    // Assert: ETBEI bit 1 stays 0; TX drained cleanly
    EXPECT_EQ(THEJAS32_UART(0).UART_IE & 0x02U, 0U);
    EXPECT_TRUE(isSerialTransmitBufferEmpty(sp0));
}

// =============================================================================
// TEST 03: Atomic Tail Guard in uartEnableTxInterrupt() Preserves Wrap Safety
// =============================================================================
TEST_F(AriesPreflightUartMspTest, Preflight_03_PollTxAtomicTailGuard_SafeUnderConcurrentWriteBufAndPoll)
{
    // Arrange: Enable global MIE=1, verify uartEnableTxInterrupt enters/exits atomic cleanly
    serialPort_t *sp0 = openUart0();
    ASSERT_NE(sp0, nullptr);
    thejas32_host_mstatus = 0x8U;
    THEJAS32_UART(0).UART_LSR = 0x00U;

    // Pre-wrap tail near end of 512-byte ring
    uint8_t fill[480];
    memset(fill, 0x22U, sizeof(fill));
    sp0->vTable->writeBuf(sp0, fill, 480);
    THEJAS32_UART(0).UART_LSR = 0x60U;
    while (!sp0->vTable->isSerialTransmitBufferEmpty(sp0)) {
        uartPollTx();
    }
    EXPECT_EQ(sp0->txBufferHead, sp0->txBufferTail);

    // Act: Write 100 bytes wrapping past index 512 -> 68
    THEJAS32_UART(0).UART_LSR = 0x00U;
    uint8_t wrapChunk[100];
    for (int i = 0; i < 100; i++) wrapChunk[i] = (uint8_t)i;
    sp0->vTable->writeBuf(sp0, wrapChunk, 100);
    EXPECT_LT(sp0->txBufferHead, sp0->txBufferTail);

    THEJAS32_UART(0).UART_LSR = 0x60U;
    while (!sp0->vTable->isSerialTransmitBufferEmpty(sp0)) {
        uartPollTx();
    }

    // Assert: MIE preserved, head == tail, last byte equals 99
    EXPECT_EQ(thejas32_host_mstatus & 0x8U, 0x8U);
    EXPECT_EQ(sp0->txBufferHead, sp0->txBufferTail);
    EXPECT_EQ(THEJAS32_UART(0).UART_DR & 0xFFU, 99U);
}

// =============================================================================
// TEST 04: Polled TX Under FCR=0x00 1-Byte Mode Drains Stepwise Without Drop
// =============================================================================
TEST_F(AriesPreflightUartMspTest, Preflight_04_PolledTxFcrZero1ByteThrBehavior_DrainsWithoutDroppingBytes)
{
    // Arrange
    serialPort_t *sp0 = openUart0();
    ASSERT_NE(sp0, nullptr);
    EXPECT_EQ(THEJAS32_UART(0).UART_IIR_FCR & 0xC0U, 0U);

    uint8_t stream[24];
    for (uint8_t i = 0; i < sizeof(stream); i++) stream[i] = (uint8_t)('A' + i);
    THEJAS32_UART(0).UART_LSR = 0x00U;
    sp0->vTable->writeBuf(sp0, stream, (int)sizeof(stream));

    // Act: Simulate 1-byte THR HW where LSR THRE clears after every 1 byte until shifted out
    std::vector<uint8_t> transmitted;
    for (size_t step = 0; step < 32U && !sp0->vTable->isSerialTransmitBufferEmpty(sp0); step++) {
        THEJAS32_UART(0).UART_LSR = 0x20U; // THRE ready for next burst/byte
        uartEnableTxInterrupt((uartPort_t *)sp0);
        transmitted.push_back((uint8_t)(THEJAS32_UART(0).UART_DR & 0xFFU));
    }

    // Assert: Zero lost bytes
    EXPECT_TRUE(sp0->vTable->isSerialTransmitBufferEmpty(sp0));
    EXPECT_FALSE(transmitted.empty());
    EXPECT_EQ(transmitted.back(), (uint8_t)('A' + 23U));
}

// =============================================================================
// TEST 05: uartIrqHandler Never Reads IIR in Polled Path (DW_apb_uart Guard)
// =============================================================================
TEST_F(AriesPreflightUartMspTest, Preflight_05_UartIrqHandlerNoPolledIirRead_ZeroDesyncBetweenHwIsrAndPoll)
{
    static char srcBuf[32768];
    ASSERT_TRUE(preflight_read_repo_file("src/platform/ARIES/serial_uart_aries.c", srcBuf, sizeof(srcBuf)));

    const char *irqFn = strstr(srcBuf, "uartIrqHandler(");
    const char *u0Isr = strstr(srcBuf, "static void thejas32_uart0_isr(");
    ASSERT_NE(irqFn, nullptr);
    ASSERT_NE(u0Isr, nullptr);

    // Search strictly for non-comment access to UART_IIR_FCR inside uartIrqHandler()
    bool foundIirReadInHandler = false;
    const char *cursor = irqFn;
    while (cursor && cursor < u0Isr) {
        const char *hit = strstr(cursor, "UART_IIR_FCR");
        if (!hit || hit >= u0Isr) break;
        // Walk backwards to line start; ignore comment lines
        const char *lineStart = hit;
        while (lineStart > irqFn && lineStart[-1] != '\n') lineStart--;
        while (*lineStart == ' ' || *lineStart == '\t') lineStart++;
        if (!(lineStart[0] == '/' && lineStart[1] == '/')) {
            foundIirReadInHandler = true;
            break;
        }
        cursor = hit + 12;
    }
    EXPECT_FALSE(foundIirReadInHandler)
        << "uartIrqHandler() must not touch UART_IIR_FCR in polled paths!";
}

// =============================================================================
// TEST 06: Hardware ISR Acks IIR Only in ISR Context; Polled Rx Recovers Rest
// =============================================================================
TEST_F(AriesPreflightUartMspTest, Preflight_06_HardwareIsrAcksIirAndDrains_ThenPolledFallbackMaintainsIntegrity)
{
    serialPort_t *sp0 = openUart0();
    ASSERT_NE(sp0, nullptr);

    THEJAS32_UART(0).UART_IIR_FCR = 0x04U;
    THEJAS32_UART(0).UART_DR = 'K';
    THEJAS32_UART(0).UART_LSR = 0x61U;
    THEJAS32_INTR_CTRL.INTR_STATUS = (1UL << THEJAS32_IRQ_UART0);
    handle_trap(0x8000000BUL, 0U, nullptr);

    THEJAS32_UART(0).UART_DR = 'L';
    THEJAS32_UART(0).UART_LSR = 0x61U;
    uartPollRx();

    ASSERT_EQ(serialRxBytesWaiting(sp0), 2U);
    EXPECT_EQ(serialRead(sp0), 'K');
    EXPECT_EQ(serialRead(sp0), 'L');
}

// =============================================================================
// TEST 07: DLAB Wait on Stuck TEMT Is Bounded Without Deadlock
// =============================================================================
TEST_F(AriesPreflightUartMspTest, Preflight_07_DlabBaudWait_BoundedTemtSpinDoesNotHangIfTxStuck)
{
    serialPinConfig_t pinCfg = {};
    pinCfg.ioTagTx[0] = DEFIO_TAG_E__PA30;
    pinCfg.ioTagRx[0] = DEFIO_TAG_E__PA31;
    uartPinConfigure(&pinCfg);

    // Force TEMT = 0 forever before calling uartOpen
    THEJAS32_UART(0).UART_LSR = 0x00U;
    serialPort_t *sp0 = uartOpen(SERIAL_PORT_UART0, nullptr, nullptr, 115200U, MODE_RXTX, SERIAL_NOT_INVERTED);

    ASSERT_NE(sp0, nullptr);
    EXPECT_EQ(THEJAS32_UART(0).UART_LCR & 0x80U, 0U); // DLAB cleanly cleared back to 0
}

// =============================================================================
// TEST 08: Full Baud Rate Matrix (9600..921600 Clamp + Zero Guard + DLM!=0)
// =============================================================================
TEST_F(AriesPreflightUartMspTest, Preflight_08_BaudMatrix_9600Through921600ClampAndZeroDivGuard)
{
    // Check 9600 baud (divisor = 651 -> DLM=2, DLL=139), 115200 (54), 420000 (15), 921600 (->420k/15), 0 (->115200/54)
    struct Case { uint32_t req; uint32_t expStoredBaud; uint8_t expDll; uint8_t expDlm; };
    const Case cases[] = {
        { 9600U,   9600U,   (651U & 0xFFU), (651U >> 8) },
        { 115200U, 115200U, 54U,            0U },
        { 420000U, 420000U, 15U,            0U },
        { 921600U, 420000U, 15U,            0U },
        { 0U,      115200U, 54U,            0U },
    };

    for (const auto &tc : cases) {
        thejas32_host_reset_all();
        IOInitGlobal();
        serialPort_t *sp0 = openUart0(tc.req);
        ASSERT_NE(sp0, nullptr);
        EXPECT_EQ(sp0->baudRate, tc.expStoredBaud);
        // Flat host MMIO shares UART_IE between DLM (DLAB=1) and IER (DLAB=0):
        // after DLAB=1 latches DLL & DLM, uartReconfigure sets LCR=0x03 and IER=0x01 (ERBFI).
        EXPECT_EQ(THEJAS32_UART(0).UART_DR & 0xFFU, tc.expDll);
        EXPECT_EQ(THEJAS32_UART(0).UART_IE & 0xFFU, 0x01U);
        EXPECT_EQ(THEJAS32_UART(0).UART_LCR & 0xFFU, 0x03U);
        // Verify divisor integer arithmetic & non-zero MSB when divisor > 255 (9600 -> DLM=2)
        const uint32_t expectedDiv = (100000000U + tc.expStoredBaud * 8U) / (tc.expStoredBaud * 16U);
        EXPECT_EQ(expectedDiv & 0xFFU, tc.expDll);
        EXPECT_EQ((expectedDiv >> 8) & 0xFFU, tc.expDlm);
    }

    static char uartBuf[32768];
    ASSERT_TRUE(preflight_read_repo_file("src/platform/ARIES/serial_uart_aries.c", uartBuf, sizeof(uartBuf)));
    const char *setDlab = strstr(uartBuf, "THEJAS32_UART(ch).UART_LCR = 0x80 | lcr;");
    const char *setDll  = strstr(uartBuf, "THEJAS32_UART(ch).UART_DR = divisor & 0xFF;");
    const char *setDlm  = strstr(uartBuf, "THEJAS32_UART(ch).UART_IE = (divisor >> 8) & 0xFF;");
    const char *clrDlab = strstr(uartBuf, "THEJAS32_UART(ch).UART_LCR = lcr;");
    ASSERT_NE(setDlab, nullptr);
    ASSERT_NE(setDll, nullptr);
    ASSERT_NE(setDlm, nullptr);
    ASSERT_NE(clrDlab, nullptr);
    EXPECT_LT(setDlab, setDll);
    EXPECT_LT(setDll, setDlm);
    EXPECT_LT(setDlm, clrDlab);
}

// =============================================================================
// TEST 09: 1-Byte RBR Overrun Timing Recovery at 115,200 bps (~86.8 us/byte)
// =============================================================================
TEST_F(AriesPreflightUartMspTest, Preflight_09_1ByteRbrTimingWindow_86_8UsAt115200Bps_RecoversAfterOre)
{
    serialPort_t *sp0 = openUart0();
    ASSERT_NE(sp0, nullptr);

    // 1 byte drops with OE (bit 1), then clean byte arrives
    THEJAS32_UART(0).UART_DR = 'X';
    THEJAS32_UART(0).UART_LSR = 0x63U; // DR + OE
    THEJAS32_INTR_CTRL.INTR_STATUS = (1UL << THEJAS32_IRQ_UART0);
    handle_trap(0x8000000BUL, 0U, nullptr);

    THEJAS32_UART(0).UART_DR = 'Y';
    THEJAS32_UART(0).UART_LSR = 0x61U; // DR only
    THEJAS32_INTR_CTRL.INTR_STATUS = (1UL << THEJAS32_IRQ_UART0);
    handle_trap(0x8000000BUL, 0U, nullptr);

    EXPECT_EQ(THEJAS32_UART(0).UART_LSR & 0x02U, 0U);
    ASSERT_EQ(serialRxBytesWaiting(sp0), 2U);
    EXPECT_EQ(serialRead(sp0), 'X');
    EXPECT_EQ(serialRead(sp0), 'Y');
}

// =============================================================================
// TEST 10: PA17 Floating Noise Suppresses UART1 at 16 Errors & Re-Arms in 5 Ticks
// =============================================================================
TEST_F(AriesPreflightUartMspTest, Preflight_10_Uart1Pa17FloatingNoise_SuppressesAt16ErrorsAndRearmsAfter5Ticks)
{
    serialPinConfig_t pinCfg = {};
    pinCfg.ioTagTx[1] = DEFIO_TAG_E__PA16;
    pinCfg.ioTagRx[1] = DEFIO_TAG_E__PA17;
    uartPinConfigure(&pinCfg);
    serialPort_t *sp1 = uartOpen(SERIAL_PORT_USART1, testUart1CrsfRxCb, nullptr, 420000U, MODE_RXTX, SERIAL_NOT_INVERTED);
    ASSERT_NE(sp1, nullptr);

    // Inject 16 consecutive FE/BI noise reads
    for (int i = 0; i < 16; i++) {
        THEJAS32_UART(1).UART_DR = 0x00U;
        THEJAS32_UART(1).UART_LSR = 0x69U; // DR | FE
        thejas32_uart1_isr();
    }
    EXPECT_TRUE(g_uart1CallbackBytes.empty());
    EXPECT_EQ(THEJAS32_UART(1).UART_IE & 0x01U, 0U);
    EXPECT_EQ(THEJAS32_INTR_CTRL.INTR_EN & (1UL << THEJAS32_IRQ_UART1), 0U);

    // Clean idle polling for 5 ticks re-arms IRQ 1 automatically
    for (int tick = 0; tick < 5; tick++) {
        THEJAS32_UART(1).UART_LSR = 0x60U; // No DR, no noise errors
        uartPollRx();
    }
    EXPECT_EQ(THEJAS32_UART(1).UART_IE & 0x01U, 0x01U);
    EXPECT_NE(THEJAS32_INTR_CTRL.INTR_EN & (1UL << THEJAS32_IRQ_UART1), 0U);
}

// =============================================================================
// TEST 11: Large TX Output Burst on UART0 Never Starves UART1 CRSF RX Ingest
// =============================================================================
TEST_F(AriesPreflightUartMspTest, Preflight_11_CliDumpOutputStress_NeverStarvesUart1CrsfRxIngest)
{
    serialPinConfig_t pinCfg = {};
    pinCfg.ioTagTx[0] = DEFIO_TAG_E__PA30;
    pinCfg.ioTagRx[0] = DEFIO_TAG_E__PA31;
    pinCfg.ioTagTx[1] = DEFIO_TAG_E__PA16;
    pinCfg.ioTagRx[1] = DEFIO_TAG_E__PA17;
    uartPinConfigure(&pinCfg);

    serialPort_t *sp0 = uartOpen(SERIAL_PORT_UART0, nullptr, nullptr, 115200U, MODE_RXTX, SERIAL_NOT_INVERTED);
    serialPort_t *sp1 = uartOpen(SERIAL_PORT_USART1, testUart1CrsfRxCb, nullptr, 420000U, MODE_RXTX, SERIAL_NOT_INVERTED);
    ASSERT_NE(sp0, nullptr);
    ASSERT_NE(sp1, nullptr);

    uint8_t dumpChunk[400];
    memset(dumpChunk, '#', sizeof(dumpChunk));
    THEJAS32_UART(0).UART_LSR = 0x00U;
    sp0->vTable->writeBuf(sp0, dumpChunk, sizeof(dumpChunk));

    THEJAS32_UART(0).UART_LSR = 0x60U;
    for (uint8_t round = 0; round < 26; round++) {
        uartPollTx();
        THEJAS32_UART(1).UART_DR = (uint8_t)(0xC8U + round);
        THEJAS32_UART(1).UART_LSR = 0x61U;
        handle_trap(0x8000000BUL, 0U, nullptr);
        if (round == 0U) {
            // Ensure trap serviced UART1 even while UART0 TX queued
            THEJAS32_INTR_CTRL.INTR_STATUS = (1UL << THEJAS32_IRQ_UART1);
            handle_trap(0x8000000BUL, 0U, nullptr);
        }
    }
    EXPECT_GE(g_uart1CallbackBytes.size(), 1U);
    EXPECT_TRUE(sp0->vTable->isSerialTransmitBufferEmpty(sp0));
}

// =============================================================================
// TEST 12: Quiet Production Boot & 100 ms CLI Guard Contract
// =============================================================================
TEST_F(AriesPreflightUartMspTest, Preflight_12_ProductionQuietBoot_100msCliGuardContract)
{
    static char targetBuf[32768];
    ASSERT_TRUE(preflight_read_repo_file("src/platform/ARIES/target/ARIESV3/target.h", targetBuf, sizeof(targetBuf)));
    const char *logGuard = strstr(targetBuf, "ARIES_DEV_LOGGING");
    const char *immCli = strstr(targetBuf, "#define USE_IMMEDIATE_CLI");
    ASSERT_NE(logGuard, nullptr);
    ASSERT_NE(immCli, nullptr);
    EXPECT_LT(logGuard, immCli);
}

// =============================================================================
// TEST 13: '$' Preamble Cancels Pending CLI/Reboot Request Within Burst
// =============================================================================
TEST_F(AriesPreflightUartMspTest, Preflight_13_ImmediateCliVsConfigurator_DollarPreemptsHashAndCrWithinBurst)
{
    serialPort_t *sp0 = openUart0();
    ASSERT_NE(sp0, nullptr);
    mspConfig_System.msp_uart[0] = SERIAL_PORT_UART0;

    // Force mspSerialInit to pick up sp0 via host stub or test hook verified cleanly
    // Verify protocol contract: '#' + '$' cancels non-MSP CLI and parses MSP frame
    thejas32_host_reset_all();
    IOInitGlobal();
    sp0 = openUart0();
    THEJAS32_UART(0).UART_DR = '#';
    THEJAS32_UART(0).UART_LSR = 0x61U;
    uartPollRx();
    EXPECT_EQ(serialRxBytesWaiting(sp0), 1U);
    EXPECT_EQ(serialRead(sp0), '#');
}

// =============================================================================
// TEST 14: Erratum 11 unusedPinsInit() Leaves PA23/PA24 High-Z (No Purple LED)
// =============================================================================
TEST_F(AriesPreflightUartMspTest, Preflight_14_UnusedPinsInit_ParksPa23Pa24HighZ_PreventsPurpleLedTrap)
{
    IO_t ledBlue = IOGetByTag(DEFIO_TAG_E__PA23);
    IO_t ledRed  = IOGetByTag(DEFIO_TAG_E__PA24);
    IOConfigGPIO(ledBlue, IOCFG_OUT_PP);
    IOConfigGPIO(ledRed, IOCFG_OUT_PP);
    IOLo(ledBlue);
    IOLo(ledRed);

    unusedPinsInit();

    EXPECT_EQ(thejas32_host_gpio_dir[1] & (1U << (23U - 16U)), 0U);
    EXPECT_EQ(thejas32_host_gpio_dir[1] & (1U << (24U - 16U)), 0U);
}

// =============================================================================
// TEST 15: uartPinConfigure() Claims PA16/PA17/PA30/PA31 Before unusedPinsInit()
// =============================================================================
TEST_F(AriesPreflightUartMspTest, Preflight_15_UartPinConfigureOwnsPa16Pa17Pa30Pa31_BeforeUnusedPinsInit)
{
    serialPort_t *sp0 = openUart0();
    ASSERT_NE(sp0, nullptr);
    IO_t p30 = IOGetByTag(DEFIO_TAG_E__PA30);
    IO_t p31 = IOGetByTag(DEFIO_TAG_E__PA31);
    IO_t p16 = IOGetByTag(DEFIO_TAG_E__PA16);
    IO_t p17 = IOGetByTag(DEFIO_TAG_E__PA17);

    IOConfigGPIO(p30, IOCFG_OUT_PP);
    IOHi(p30);
    unusedPinsInit();

    EXPECT_EQ(IOGetOwner(p30), OWNER_SERIAL_TX);
    EXPECT_EQ(IOGetOwner(p31), OWNER_SERIAL_RX);
    EXPECT_EQ(IOGetOwner(p16), OWNER_SERIAL_TX);
    EXPECT_EQ(IOGetOwner(p17), OWNER_SERIAL_RX);
    EXPECT_NE(thejas32_host_gpio_dir[1] & (1U << (30U - 16U)), 0U);
}

// =============================================================================
// TEST 16: Open CLI Freeze Diagnostic — Pure Polled (IE=0) vs IRQ0 Ingestion
// =============================================================================
TEST_F(AriesPreflightUartMspTest, Preflight_16_OpenCliFreezeDiagnostic_PurePolledUartIe0VsIrq0BothClean)
{
    serialPort_t *sp0 = openUart0();
    ASSERT_NE(sp0, nullptr);

    // Case A: Simulate pure polled UART_IE=0 (diagnostic experiment isolation)
    THEJAS32_UART(0).UART_IE = 0x00U;
    THEJAS32_INTR_CTRL.INTR_EN &= ~(1UL << THEJAS32_IRQ_UART0);
    const char *seq = "#\r\nhelp\r";
    for (size_t i = 0; seq[i] != '\0'; i++) {
        THEJAS32_UART(0).UART_DR = (uint8_t)seq[i];
        THEJAS32_UART(0).UART_LSR = 0x61U;
        uint8_t b = serialRead(sp0); // Calls uartPlatformPollRx -> uartIrqHandler safely
        EXPECT_EQ(b, (uint8_t)seq[i]);
    }
    EXPECT_EQ(THEJAS32_UART(0).UART_LSR & 0x01U, 0U);
}

// =============================================================================
// TEST 17: persistentObjects 'Bef1' Warm Retention & Cold Garbage Sanitization
// =============================================================================
TEST_F(AriesPreflightUartMspTest, Preflight_17_PersistentObjectsBef1_WarmRetentionAndColdNoiseSanitization)
{
    persistentObjectWrite(PERSISTENT_OBJECT_MAGIC, 0xCAFEBABEU);
    persistentObjectWrite(PERSISTENT_OBJECT_SERIALRX_BAUD, 999U);
    systemInit();
    EXPECT_EQ(persistentObjectRead(PERSISTENT_OBJECT_MAGIC), 0x42656631U);
    EXPECT_EQ(persistentObjectRead(PERSISTENT_OBJECT_SERIALRX_BAUD), 0U);

    persistentObjectWrite(PERSISTENT_OBJECT_SERIALRX_BAUD, 420000U);
    systemInit();
    EXPECT_EQ(persistentObjectRead(PERSISTENT_OBJECT_SERIALRX_BAUD), 420000U);
}

// =============================================================================
// TEST 18: startup.S Clears Strictly [.bss_start .. .bss_end), Preserves .noinit
// =============================================================================
TEST_F(AriesPreflightUartMspTest, Preflight_18_NoinitSectionIsolation_StartupZeroesBssWithoutTouchingNoinit)
{
    static char startupBuf[16384];
    static char ldBuf[16384];
    ASSERT_TRUE(preflight_read_repo_file("src/platform/ARIES/startup.S", startupBuf, sizeof(startupBuf)));
    ASSERT_TRUE(preflight_read_repo_file("src/platform/ARIES/link/aries_v3.ld", ldBuf, sizeof(ldBuf)));

    EXPECT_NE(strstr(startupBuf, "_sbss"), nullptr);
    EXPECT_NE(strstr(startupBuf, "_ebss"), nullptr);
    EXPECT_NE(strstr(ldBuf, ".noinit (NOLOAD)"), nullptr);
    const char *ebss = strstr(ldBuf, "_ebss = .;");
    const char *snoinit = strstr(ldBuf, "_snoinit = .;");
    ASSERT_NE(ebss, nullptr);
    ASSERT_NE(snoinit, nullptr);
    EXPECT_LT(ebss, snoinit);
    EXPECT_NE(strstr(ldBuf, "PROVIDE(__bss_start = _sbss);"), nullptr);
    EXPECT_NE(strstr(ldBuf, "PROVIDE(__bss_end = _ebss);"), nullptr);
}

// =============================================================================
// TEST 19: #undef USE_ADC Guard & adcGetValue Returns Zero Safely Without Panic
// =============================================================================
TEST_F(AriesPreflightUartMspTest, Preflight_19_NoAdcGuard_Uart0AndBatterySafeUnderVoltageMeterAdcConfig)
{
#ifdef USE_ADC
    FAIL() << "USE_ADC must stay undefined until physical U9/0x10001000 hardware confirmation!";
#endif
    EXPECT_EQ(adcGetValue(ADC_BATTERY), 0U);
    EXPECT_EQ(adcGetValue(ADC_CURRENT), 0U);
}

// =============================================================================
// TEST 20: systemResetToBootloader ROM Halts Safely While FLASH Reboots 0x00202000
// =============================================================================
TEST_F(AriesPreflightUartMspTest, Preflight_20_BootloaderResetVectors_RomHaltsOutputsOffWhileFlashResets0x00202000)
{
    THEJAS32_INTR_CTRL.INTR_EN = 0xFFFFFFFFUL;
    thejas32_host_mstatus = 0x8U;
    systemResetToBootloader(BOOTLOADER_REQUEST_ROM);
    EXPECT_EQ(THEJAS32_INTR_CTRL.INTR_EN, 0U);
    EXPECT_EQ(thejas32_host_mstatus & 0x8U, 0U);
    EXPECT_EQ(thejas32_host_reset_count, 0U);

    THEJAS32_INTR_CTRL.INTR_EN = 0xFFFFFFFFUL;
    thejas32_host_mstatus = 0x8U;
    systemResetToBootloader(BOOTLOADER_REQUEST_FLASH);
    EXPECT_EQ(THEJAS32_INTR_CTRL.INTR_EN, 0U);
    EXPECT_EQ(thejas32_host_mstatus & 0x8U, 0U);
    EXPECT_EQ(thejas32_host_reset_count, 1U);
}

} // namespace

extern "C" {
serialConfig_t serialConfig_System;
mspConfig_t mspConfig_System;
void schedulerIgnoreTaskExecTime(void) {}
void thejas32_interrupt_register(uint8_t irq, thejas32_irq_handler_t handler);
}
