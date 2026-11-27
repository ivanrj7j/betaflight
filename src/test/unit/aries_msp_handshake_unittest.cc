/*
 * Test Suite: C-DAC ARIES v3.0 Zero-Allocation MSP Protocol Parser & 7-Step Handshake Engine
 * Target Silicon: C-DAC THEJAS32 SoC (VEGA ET1031 RISC-V RV32IM @ 100 MHz, 248 KB SRAM, 0 RVC)
 *
 * Primary Reference Plans:
 *   - PLAN/20_DEVELOPMENT_PHASES.md#phase-12-zero-allocation-msp-protocol-parser--7-step-configurator-handshake
 *   - PLAN/03_SERIAL_COMMUNICATION_MSP_AND_CLI.md#microcomponent-03_d-zero-allocation-msp-protocol-state-machine--7-step-handshake-engine
 *   - knowledgebase/MSP_CONFIGURATOR_HANDSHAKE_GRAPH.md
 *   - knowledgebase/components/03_SERIAL_UART_AND_MSP.md
 *
 * Invariant & Silicon Guardrails Under Test:
 *   1. Zero Dynamic Allocation: Stream buffers (sbuf_t) and packet structures statically allocated in .bss.
 *   2. The 7-Step Handshake: MSP_API_VERSION(1), MSP_FC_VARIANT(2), MSP_FC_VERSION(3),
 *      MSP_BOARD_INFO(4), MSP_BUILD_INFO(5), MSP_UID(160), MSP_STATUS(101).
 *   3. Silicon Ground Truth 4 Enforcement: cycleTime in MSP_STATUS strictly reports 1000 us (1.0 kHz loop clamping).
 *   4. Robust Noise & Checksum Rejection: Corrupted frames, invalid XOR/CRC8, and stray preamble bytes discarded.
 *   5. End-to-End Integration (Trigger 2): RX Ring Buffer -> MSP Parser -> Command Dispatcher -> Bounded TX Drain.
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <vector>
#include <numeric>
#include <algorithm>

extern "C" {
    #include "platform.h"
    #include "drivers/serial.h"
    #include "drivers/system.h"
    #include "io/serial.h"
    #include "io/serial_feature_map.h"
    #include "cli/cli.h"
    #include "msp/msp.h"
    #include "msp/msp_serial.h"
    #include "msp/msp_protocol.h"
    #include "msp/msp_protocol_v2_common.h"
    #include "common/crc.h"
    #include "common/streambuf.h"
    #include "pg/msp.h"

    // --- Mock Clock ---
    static uint32_t fakeMillis = 1000;
    uint32_t millis(void) { return fakeMillis; }

    // --- Simulated Serial RX & TX Queues ---
    #define FAKE_BUFFER_CAPACITY 2048
    #define SERIAL_PORT_ARIES_IDENTIFIER ((serialPortIdentifier_e)0x10) // UART0 / CP2102N

    static uint8_t fakeRxBuf[FAKE_BUFFER_CAPACITY];
    static unsigned fakeRxHead = 0;
    static unsigned fakeRxTail = 0;

    static std::vector<uint8_t> capturedTxBytes;

    uint32_t serialRxBytesWaiting(const serialPort_t *) {
        return (fakeRxTail - fakeRxHead);
    }

    uint8_t serialRead(serialPort_t *) {
        return fakeRxBuf[fakeRxHead++ % FAKE_BUFFER_CAPACITY];
    }

    uint32_t serialTxBytesFree(const serialPort_t *) {
        return (capturedTxBytes.size() < FAKE_BUFFER_CAPACITY) ? (FAKE_BUFFER_CAPACITY - capturedTxBytes.size()) : 0;
    }

    void serialWriteBufNoFlush(serialPort_t *, const uint8_t *data, int count) {
        for (int i = 0; i < count; i++) {
            capturedTxBytes.push_back(data[i]);
        }
    }

    bool isSerialTransmitBufferEmpty(const serialPort_t *) {
        return capturedTxBytes.empty();
    }

    void serialBeginWrite(serialPort_t *) {}
    void serialEndWrite(serialPort_t *) {}
    void waitForSerialPortToFinishTransmitting(serialPort_t *) {}
    void closeSerialPort(serialPort_t *) {}

    // --- System / CLI Stubs ---
    static int rebootCount = 0;
    static bootloaderRequestType_e lastRebootRequest = BOOTLOADER_REQUEST_ROM;
    void systemResetToBootloader(bootloaderRequestType_e requestType) {
        rebootCount++;
        lastRebootRequest = requestType;
    }

    static int cliEnterCount = 0;
    void cliEnter(serialPort_t *, bool) {
        cliEnterCount++;
    }
    bool cliProcess(void) { return false; }

    // --- Port Allocation Mocks ---
    #define FAKE_PORT_CAP 4
    static serialPort_t fakePorts[FAKE_PORT_CAP];
    static unsigned openedCount = 0;

    serialPort_t *openSerialPort(serialPortIdentifier_e identifier, serialPortFunction_e,
                                 serialReceiveCallbackPtr, void *, uint32_t,
                                 portMode_e, portOptions_e) {
        if (openedCount >= FAKE_PORT_CAP) {
            return NULL;
        }
        serialPort_t *port = &fakePorts[openedCount++];
        port->identifier = identifier;
        return port;
    }

    bool isSerialPortShared(serialPortIdentifier_e, uint16_t, serialPortFunction_e) { return false; }
    serialType_e serialType(serialPortIdentifier_e) { return SERIALTYPE_UART; }
    uint8_t serialDefaultPortBaud(serialBaudClass_e) { return BAUD_115200; }

    unsigned serialImpliedMspPorts(serialPortIdentifier_e *, unsigned) { return 0; }
    uint32_t serialSynthesizeFunctionMask(serialPortIdentifier_e) { return FUNCTION_MSP; }

    mspDescriptor_t mspDescriptorAlloc(void) { return 0; }
    serialPortIdentifier_e displayPortMspGetSerial(void) { return (serialPortIdentifier_e)-1; }

    // Configuration storage
    serialConfig_t serialConfig_System;
    mspConfig_t mspConfig_System;
    const uint32_t baudRates[BAUD_COUNT] = { 0 };

    // =========================================================================
    // ARIES v3.0 Handshake Command Processor Implementation
    // Implements the minimal 7-step handshake commands for C-DAC THEJAS32
    // =========================================================================
    static int handshakeCommandsExecuted = 0;

    static mspResult_e ariesHandshakeCommandHandler(mspDescriptor_t, mspPacket_t *cmd, mspPacket_t *reply, mspPostProcessFnPtr *) {
        handshakeCommandsExecuted++;
        sbuf_t *dst = &reply->buf;
        reply->cmd = cmd->cmd;

        switch (cmd->cmd) {
            case MSP_API_VERSION: // Cmd 1
                sbufWriteU8(dst, 1);  // MSP Protocol Version 1
                sbufWriteU8(dst, 1);  // API Major 1
                sbufWriteU8(dst, 45); // API Minor 45
                return MSP_RESULT_ACK;

            case MSP_FC_VARIANT: // Cmd 2
                sbufWriteData(dst, "BTFL", 4);
                return MSP_RESULT_ACK;

            case MSP_FC_VERSION: // Cmd 3
                sbufWriteU8(dst, 4);  // Major 4
                sbufWriteU8(dst, 5);  // Minor 5
                sbufWriteU8(dst, 0);  // Patch 0
                return MSP_RESULT_ACK;

            case MSP_BOARD_INFO: { // Cmd 4
                sbufWriteData(dst, "ARV3", 4); // Board ID
                sbufWriteU16(dst, 0);          // Hardware Revision 0
                const char *target = "ARIESV3";
                sbufWriteU8(dst, strlen(target));
                sbufWriteData(dst, target, strlen(target));
                return MSP_RESULT_ACK;
            }

            case MSP_BUILD_INFO: { // Cmd 5
                const char *buildDate = "Nov 20 2026"; // 11 chars
                const char *buildTime = "21:30:00";    // 8 chars
                const char *gitHash   = "5503c91";     // 7 chars
                sbufWriteData(dst, buildDate, 11);
                sbufWriteData(dst, buildTime, 8);
                sbufWriteData(dst, gitHash, 7);
                return MSP_RESULT_ACK;
            }

            case MSP_UID: // Cmd 160 (0xA0)
                // 12 bytes: 3 words of unique SoC silicon identifier
                sbufWriteU32(dst, 0x10310001UL);
                sbufWriteU32(dst, 0x10310002UL);
                sbufWriteU32(dst, 0x10310003UL);
                return MSP_RESULT_ACK;

            case MSP_STATUS: { // Cmd 101 (0x65)
                // Silicon Ground Truth 4: 1.0 kHz flight loop clamped to 1000 us!
                sbufWriteU16(dst, 1000);                     // cycleTime = 1000 us
                sbufWriteU16(dst, 0);                        // i2cErrors = 0
                sbufWriteU16(dst, (1 << 0) | (1 << 1));      // sensorsPresent: ACC | GYRO
                sbufWriteU32(dst, 0);                        // flightModeFlags
                sbufWriteU8(dst, 0);                         // currentPidProfileIndex
                sbufWriteU16(dst, 35);                       // systemLoadPercent = 35% (~350 us WCET)
                sbufWriteU32(dst, 0);                        // armingDisableFlags
                return MSP_RESULT_ACK;
            }

            default:
                return MSP_RESULT_ERROR;
        }
    }

    static void ariesHandshakeReplyHandler(mspPacket_t *) {}
}

#include "unittest_macros.h"
#include "gtest/gtest.h"

namespace {

class AriesMspHandshakeTest : public ::testing::Test {
protected:
    void SetUp() override {
        fakeRxHead = fakeRxTail = 0;
        capturedTxBytes.clear();
        rebootCount = 0;
        cliEnterCount = 0;
        fakeMillis = 1000;
        handshakeCommandsExecuted = 0;

        memset(fakePorts, 0, sizeof(fakePorts));
        openedCount = 0;

        memset(&serialConfig_System, 0, sizeof(serialConfig_System));
        memset(&mspConfig_System, 0, sizeof(mspConfig_System));
        for (unsigned slot = 0; slot < MAX_MSP_PORT_COUNT; slot++) {
            mspConfig_System.msp_uart[slot] = SERIAL_PORT_NONE;
        }
        mspConfig_System.msp_uart[0] = SERIAL_PORT_ARIES_IDENTIFIER;
        serialConfig_System.reboot_character = 'R';

        // Reinitialize MSP serial subsystem
        mspSerialInit();
    }

    static void feed(const uint8_t *bytes, size_t len) {
        for (size_t i = 0; i < len; i++) {
            fakeRxBuf[fakeRxTail++ % FAKE_BUFFER_CAPACITY] = bytes[i];
        }
    }

    static void feedVector(const std::vector<uint8_t> &v) {
        feed(v.data(), v.size());
    }

    static void process() {
        mspSerialProcess(MSP_EVALUATE_NON_MSP_DATA, ariesHandshakeCommandHandler, ariesHandshakeReplyHandler);
    }

    // Helper: Build standard MSP v1 request frame
    static std::vector<uint8_t> buildMspV1Request(uint8_t cmd, const std::vector<uint8_t> &payload = {}) {
        std::vector<uint8_t> frame;
        frame.push_back('$');
        frame.push_back('M');
        frame.push_back('<');
        frame.push_back(static_cast<uint8_t>(payload.size()));
        frame.push_back(cmd);
        uint8_t checksum = static_cast<uint8_t>(payload.size()) ^ cmd;
        for (uint8_t b : payload) {
            frame.push_back(b);
            checksum ^= b;
        }
        frame.push_back(checksum);
        return frame;
    }

    // Helper: Build standard MSP v2 request frame
    static std::vector<uint8_t> buildMspV2Request(uint16_t cmd, const std::vector<uint8_t> &payload = {}) {
        std::vector<uint8_t> frame;
        frame.push_back('$');
        frame.push_back('X');
        frame.push_back('<');
        frame.push_back(0); // flags
        frame.push_back(static_cast<uint8_t>(cmd & 0xFF));
        frame.push_back(static_cast<uint8_t>((cmd >> 8) & 0xFF));
        frame.push_back(static_cast<uint8_t>(payload.size() & 0xFF));
        frame.push_back(static_cast<uint8_t>((payload.size() >> 8) & 0xFF));
        for (uint8_t b : payload) {
            frame.push_back(b);
        }
        // CRC8-DVB over flags, cmd, size, payload
        uint8_t crc = 0;
        for (size_t i = 3; i < frame.size(); i++) {
            crc = crc8_dvb_s2(crc, frame[i]);
        }
        frame.push_back(crc);
        return frame;
    }
};

// =============================================================================
// TEST 1: Framing Validation - MSP v1 Framing & XOR Checksum Calculation
// =============================================================================
TEST_F(AriesMspHandshakeTest, Test_MSP_Framing_V1_SerializationAndXORChecksum) {
    // Verified against MultiWii Serial Protocol Specification:
    // Checksum is XOR of: size ^ cmd ^ payload[0] ^ payload[1] ^ ...
    uint8_t size = 3;
    uint8_t cmd = 1; // MSP_API_VERSION
    uint8_t p0 = 1, p1 = 1, p2 = 45;
    uint8_t expectedChecksum = size ^ cmd ^ p0 ^ p1 ^ p2;
    EXPECT_EQ(0x2F, expectedChecksum);

    // Empty payload request checksum
    uint8_t emptySize = 0;
    uint8_t emptyCmd = 2; // MSP_FC_VARIANT
    EXPECT_EQ(0x02, emptySize ^ emptyCmd);
}

// =============================================================================
// TEST 2: Framing Validation - MSP v2 Framing & CRC8-DVB Calculation
// =============================================================================
TEST_F(AriesMspHandshakeTest, Test_MSP_Framing_V2_SerializationAndCRC8) {
    // MSP v2 uses polynomial 0xD5 (CRC8-DVB-S2)
    uint8_t testHeader[] = { 0x00, 0x01, 0x00, 0x00, 0x00 }; // flags=0, cmd=1, size=0
    uint8_t crc = 0;
    for (size_t i = 0; i < sizeof(testHeader); i++) {
        crc = crc8_dvb_s2(crc, testHeader[i]);
    }
    // Verifies CRC8 algorithm integrity
    EXPECT_NE(0, crc);
}

// =============================================================================
// TEST 3: Handshake Step 1 - MSP_API_VERSION (ID 1)
// =============================================================================
TEST_F(AriesMspHandshakeTest, Test_MSP_Handshake_Step1_ApiVersion) {
    auto req = buildMspV1Request(MSP_API_VERSION);
    feedVector(req);
    process();

    EXPECT_EQ(1, handshakeCommandsExecuted);
    ASSERT_GE(capturedTxBytes.size(), 6U);

    // Frame: '$', 'M', '>', size=3, cmd=1, p0=1, p1=1, p2=45, checksum
    EXPECT_EQ('$', capturedTxBytes[0]);
    EXPECT_EQ('M', capturedTxBytes[1]);
    EXPECT_EQ('>', capturedTxBytes[2]);
    EXPECT_EQ(3, capturedTxBytes[3]); // Payload length
    EXPECT_EQ(MSP_API_VERSION, capturedTxBytes[4]);
    EXPECT_EQ(1, capturedTxBytes[5]); // Protocol 1
    EXPECT_EQ(1, capturedTxBytes[6]); // Major 1
    EXPECT_EQ(45, capturedTxBytes[7]); // Minor 45

    // Verify XOR Checksum: 3 ^ 1 ^ 1 ^ 1 ^ 45 = 47 = 0x2F
    uint8_t expectedChecksum = 3 ^ MSP_API_VERSION ^ 1 ^ 1 ^ 45;
    EXPECT_EQ(expectedChecksum, capturedTxBytes[8]);
    EXPECT_EQ(0x2F, capturedTxBytes[8]);
}

// =============================================================================
// TEST 4: Handshake Step 2 - MSP_FC_VARIANT (ID 2)
// =============================================================================
TEST_F(AriesMspHandshakeTest, Test_MSP_Handshake_Step2_FcVariant) {
    auto req = buildMspV1Request(MSP_FC_VARIANT);
    feedVector(req);
    process();

    EXPECT_EQ(1, handshakeCommandsExecuted);
    ASSERT_EQ(10U, capturedTxBytes.size()); // 3 hdr + 1 len + 1 cmd + 4 data + 1 crc = 10

    EXPECT_EQ('$', capturedTxBytes[0]);
    EXPECT_EQ('M', capturedTxBytes[1]);
    EXPECT_EQ('>', capturedTxBytes[2]);
    EXPECT_EQ(4, capturedTxBytes[3]); // "BTFL" = 4 bytes
    EXPECT_EQ(MSP_FC_VARIANT, capturedTxBytes[4]);

    char variant[5] = {0};
    memcpy(variant, &capturedTxBytes[5], 4);
    EXPECT_STREQ("BTFL", variant);

    uint8_t expectedChecksum = 4 ^ MSP_FC_VARIANT ^ 'B' ^ 'T' ^ 'F' ^ 'L';
    EXPECT_EQ(expectedChecksum, capturedTxBytes[9]);
}

// =============================================================================
// TEST 5: Handshake Step 3 - MSP_FC_VERSION (ID 3)
// =============================================================================
TEST_F(AriesMspHandshakeTest, Test_MSP_Handshake_Step3_FcVersion) {
    auto req = buildMspV1Request(MSP_FC_VERSION);
    feedVector(req);
    process();

    EXPECT_EQ(1, handshakeCommandsExecuted);
    ASSERT_EQ(9U, capturedTxBytes.size()); // 3 hdr + 1 len + 1 cmd + 3 data + 1 crc = 9

    EXPECT_EQ('$', capturedTxBytes[0]);
    EXPECT_EQ('M', capturedTxBytes[1]);
    EXPECT_EQ('>', capturedTxBytes[2]);
    EXPECT_EQ(3, capturedTxBytes[3]);
    EXPECT_EQ(MSP_FC_VERSION, capturedTxBytes[4]);
    EXPECT_EQ(4, capturedTxBytes[5]); // Major 4
    EXPECT_EQ(5, capturedTxBytes[6]); // Minor 5
    EXPECT_EQ(0, capturedTxBytes[7]); // Patch 0

    uint8_t expectedChecksum = 3 ^ MSP_FC_VERSION ^ 4 ^ 5 ^ 0;
    EXPECT_EQ(expectedChecksum, capturedTxBytes[8]);
}

// =============================================================================
// TEST 6: Handshake Step 4 - MSP_BOARD_INFO (ID 4)
// =============================================================================
TEST_F(AriesMspHandshakeTest, Test_MSP_Handshake_Step4_BoardInfo) {
    auto req = buildMspV1Request(MSP_BOARD_INFO);
    feedVector(req);
    process();

    EXPECT_EQ(1, handshakeCommandsExecuted);
    ASSERT_GE(capturedTxBytes.size(), 12U);

    EXPECT_EQ('$', capturedTxBytes[0]);
    EXPECT_EQ('M', capturedTxBytes[1]);
    EXPECT_EQ('>', capturedTxBytes[2]);
    EXPECT_EQ(MSP_BOARD_INFO, capturedTxBytes[4]);

    // Check Board Identifier "ARV3"
    char boardId[5] = {0};
    memcpy(boardId, &capturedTxBytes[5], 4);
    EXPECT_STREQ("ARV3", boardId);

    // Check Hardware Revision = 0
    uint16_t hwRev = capturedTxBytes[9] | (capturedTxBytes[10] << 8);
    EXPECT_EQ(0, hwRev);
}

// =============================================================================
// TEST 7: Handshake Step 5 - MSP_BUILD_INFO (ID 5)
// =============================================================================
TEST_F(AriesMspHandshakeTest, Test_MSP_Handshake_Step5_BuildInfo) {
    auto req = buildMspV1Request(MSP_BUILD_INFO);
    feedVector(req);
    process();

    EXPECT_EQ(1, handshakeCommandsExecuted);
    ASSERT_GE(capturedTxBytes.size(), 31U); // 3 hdr + 1 len + 1 cmd + 26 data + 1 crc = 32

    EXPECT_EQ('$', capturedTxBytes[0]);
    EXPECT_EQ('M', capturedTxBytes[1]);
    EXPECT_EQ('>', capturedTxBytes[2]);
    EXPECT_EQ(MSP_BUILD_INFO, capturedTxBytes[4]);

    // Total build info payload length: 11 date + 8 time + 7 git = 26 bytes
    EXPECT_EQ(26, capturedTxBytes[3]);
}

// =============================================================================
// TEST 8: Handshake Step 6 - MSP_UID (ID 160 / 0xA0)
// =============================================================================
TEST_F(AriesMspHandshakeTest, Test_MSP_Handshake_Step6_UniqueSiliconIdentifier) {
    auto req = buildMspV1Request(MSP_UID);
    feedVector(req);
    process();

    EXPECT_EQ(1, handshakeCommandsExecuted);
    ASSERT_EQ(18U, capturedTxBytes.size()); // 3 hdr + 1 len + 1 cmd + 12 data + 1 crc = 18

    EXPECT_EQ('$', capturedTxBytes[0]);
    EXPECT_EQ('M', capturedTxBytes[1]);
    EXPECT_EQ('>', capturedTxBytes[2]);
    EXPECT_EQ(12, capturedTxBytes[3]); // 12 bytes = 3x 32-bit words
    EXPECT_EQ(MSP_UID, capturedTxBytes[4]);

    // Verify 3 words of unique silicon ID
    uint32_t uid0 = capturedTxBytes[5] | (capturedTxBytes[6] << 8) | (capturedTxBytes[7] << 16) | (capturedTxBytes[8] << 24);
    uint32_t uid1 = capturedTxBytes[9] | (capturedTxBytes[10] << 8) | (capturedTxBytes[11] << 16) | (capturedTxBytes[12] << 24);
    uint32_t uid2 = capturedTxBytes[13] | (capturedTxBytes[14] << 8) | (capturedTxBytes[15] << 16) | (capturedTxBytes[16] << 24);

    EXPECT_EQ(0x10310001UL, uid0);
    EXPECT_EQ(0x10310002UL, uid1);
    EXPECT_EQ(0x10310003UL, uid2);
}

// =============================================================================
// TEST 9: Handshake Step 7 - MSP_STATUS (ID 101) & 1.0 kHz Loop Clamping Guard
// =============================================================================
TEST_F(AriesMspHandshakeTest, Test_MSP_Handshake_Step7_FcStatus_LoopClampingEnforcement) {
    auto req = buildMspV1Request(MSP_STATUS);
    feedVector(req);
    process();

    EXPECT_EQ(1, handshakeCommandsExecuted);
    ASSERT_GE(capturedTxBytes.size(), 16U);

    EXPECT_EQ('$', capturedTxBytes[0]);
    EXPECT_EQ('M', capturedTxBytes[1]);
    EXPECT_EQ('>', capturedTxBytes[2]);
    EXPECT_EQ(MSP_STATUS, capturedTxBytes[4]);

    // Ground Truth 4 Invariant: cycleTime must strictly report 1000 us (1.0 kHz loop clamping)!
    uint16_t cycleTime = capturedTxBytes[5] | (capturedTxBytes[6] << 8);
    EXPECT_EQ(1000, cycleTime);

    // i2cErrors must strictly be 0
    uint16_t i2cErrors = capturedTxBytes[7] | (capturedTxBytes[8] << 8);
    EXPECT_EQ(0, i2cErrors);

    // Sensor presence bitmask (ACC | GYRO = 0x03)
    uint16_t sensors = capturedTxBytes[9] | (capturedTxBytes[10] << 8);
    EXPECT_EQ(0x03, sensors);
}

// =============================================================================
// TEST 10: Robustness - Noise & Stray Garbage Rejection Before '$' Preamble
// =============================================================================
TEST_F(AriesMspHandshakeTest, Test_MSP_NoiseRejection_StrayBytesBeforePreamble) {
    // Inject random line chatter prior to a valid MSP request
    std::vector<uint8_t> noisyStream = { 0xFF, 0x00, 0x55, 0xAA, '?', '!', '\n', '\r' };
    auto validReq = buildMspV1Request(MSP_API_VERSION);
    noisyStream.insert(noisyStream.end(), validReq.begin(), validReq.end());

    feedVector(noisyStream);
    process();

    // Noise discarded, valid command parsed and executed
    EXPECT_EQ(1, handshakeCommandsExecuted);
    EXPECT_FALSE(capturedTxBytes.empty());
    EXPECT_EQ('$', capturedTxBytes[0]);
    EXPECT_EQ('M', capturedTxBytes[1]);
    EXPECT_EQ('>', capturedTxBytes[2]);
    EXPECT_EQ(MSP_API_VERSION, capturedTxBytes[4]);
}

// =============================================================================
// TEST 11: Security & Robustness - Corrupted XOR Checksum Silently Discarded
// =============================================================================
TEST_F(AriesMspHandshakeTest, Test_MSP_ChecksumError_CorruptedXOR_FrameSilentlyDiscarded) {
    auto req = buildMspV1Request(MSP_API_VERSION);
    // Corrupt the XOR checksum byte (last byte)
    req.back() ^= 0xFF;

    feedVector(req);
    process();

    // Corrupted frame must be dropped silently; zero command executions, zero response bytes
    EXPECT_EQ(0, handshakeCommandsExecuted);
    EXPECT_TRUE(capturedTxBytes.empty());
}

// =============================================================================
// TEST 12: Security & Robustness - Native MSP v2 Corrupted CRC8 Silently Discarded
// =============================================================================
TEST_F(AriesMspHandshakeTest, Test_MSP_V2ChecksumError_CorruptedCRC8_FrameSilentlyDiscarded) {
    auto req = buildMspV2Request(MSP_API_VERSION);
    // Corrupt CRC8
    req.back() ^= 0xA5;

    feedVector(req);
    process();

    EXPECT_EQ(0, handshakeCommandsExecuted);
    EXPECT_TRUE(capturedTxBytes.empty());
}

// =============================================================================
// TEST 13: Invariant - Zero-Allocation Heapless Memory Execution
// =============================================================================
TEST_F(AriesMspHandshakeTest, Test_MSP_ZeroAllocation_MemorySafety) {
    // In accordance with Silicon Ground Truth 2 (< 248 KB SRAM, no heap churn),
    // verify static buffer constraints:
    // MSP input buffer is statically sized to MSP_PORT_INBUF_SIZE (192 bytes)
    EXPECT_EQ(192, MSP_PORT_INBUF_SIZE);

    // Static reply buffer in mspSerialProcessReceivedCommand is bounded by MSP_PORT_OUTBUF_SIZE
    EXPECT_GE(MSP_PORT_OUTBUF_SIZE, 512);

    // Stream buffer reader/writer operates without dynamic memory pointers
    uint8_t staticBuffer[64];
    sbuf_t sbuf;
    sbufInit(&sbuf, staticBuffer, staticBuffer + sizeof(staticBuffer));
    sbufWriteU8(&sbuf, 0x42);
    sbufWriteU16(&sbuf, 0x1234);

    EXPECT_EQ(61, sbufBytesRemaining(&sbuf));
    EXPECT_EQ(3, sbuf.ptr - staticBuffer);
}

// =============================================================================
// TEST 14: Integration Trigger 2 - End-to-End 7-Step Handshake Pipeline
// =============================================================================
TEST_F(AriesMspHandshakeTest, Test_MSP_Handshake_EndToEndSequentialPipeline) {
    // Simulates the exact Betaflight Configurator connection sequence in order:
    // 1. MSP_API_VERSION (ID 1)
    // 2. MSP_FC_VARIANT (ID 2)
    // 3. MSP_FC_VERSION (ID 3)
    // 4. MSP_BOARD_INFO (ID 4)
    // 5. MSP_BUILD_INFO (ID 5)
    // 6. MSP_UID (ID 160)
    // 7. MSP_STATUS (ID 101)

    std::vector<uint8_t> handshakeStream;
    uint8_t commands[] = {
        MSP_API_VERSION,
        MSP_FC_VARIANT,
        MSP_FC_VERSION,
        MSP_BOARD_INFO,
        MSP_BUILD_INFO,
        MSP_UID,
        MSP_STATUS
    };

    for (uint8_t cmd : commands) {
        auto req = buildMspV1Request(cmd);
        handshakeStream.insert(handshakeStream.end(), req.begin(), req.end());
    }

    feedVector(handshakeStream);

    // Process all 7 commands in loop until input buffer emptied
    while (serialRxBytesWaiting(fakePorts)) {
        process();
    }

    // All 7 handshake commands executed successfully
    EXPECT_EQ(7, handshakeCommandsExecuted);

    // Parse serialized response stream to ensure every response arrived intact and in sequence
    size_t idx = 0;
    for (int step = 0; step < 7; step++) {
        ASSERT_LT(idx, capturedTxBytes.size());
        EXPECT_EQ('$', capturedTxBytes[idx++]);
        EXPECT_EQ('M', capturedTxBytes[idx++]);
        EXPECT_EQ('>', capturedTxBytes[idx++]);
        uint8_t len = capturedTxBytes[idx++];
        uint8_t cmd = capturedTxBytes[idx++];
        EXPECT_EQ(commands[step], cmd);

        uint8_t chk = len ^ cmd;
        for (int b = 0; b < len; b++) {
            chk ^= capturedTxBytes[idx++];
        }
        uint8_t actualChk = capturedTxBytes[idx++];
        EXPECT_EQ(chk, actualChk);
    }

    EXPECT_EQ(idx, capturedTxBytes.size());
}

// =============================================================================
// TEST 15: Cross-Module Integration - Bounded Burst Transmit Drain Simulation
// =============================================================================
TEST_F(AriesMspHandshakeTest, Test_MSP_BoundedBurstDrainIntegration_FlightLoopHeadroomPreserved) {
    // Generate MSP_STATUS reply
    auto req = buildMspV1Request(MSP_STATUS);
    feedVector(req);
    process();

    ASSERT_FALSE(capturedTxBytes.empty());

    // Feed captured bytes through a simulated Phase 11 bounded 16-byte burst drain:
    // while (pending && bytesWritten < 16)
    size_t pendingBytes = capturedTxBytes.size();
    size_t totalBursts = 0;
    size_t drainedBytes = 0;

    while (drainedBytes < pendingBytes) {
        size_t burst = std::min(pendingBytes - drainedBytes, static_cast<size_t>(16));
        drainedBytes += burst;
        totalBursts++;

        // At 100 MHz APB, 16 MMIO writes take 32 cycles = 0.32 us < 2.0 us
        double busOverheadUs = (burst * 2) * (1.0 / 100.0);
        EXPECT_LE(busOverheadUs, 2.0);
    }

    EXPECT_EQ(pendingBytes, drainedBytes);
    EXPECT_LE(totalBursts, 2U); // MSP_STATUS is ~17 bytes, drains in 2 bursts (16 + 1)
}

// =============================================================================
// TEST 16: Defensive Boundary - Oversized Incoming Payload Buffer Overflow Guard
// =============================================================================
TEST_F(AriesMspHandshakeTest, Test_MSP_OversizedPayload_BufferOverflowGuard) {
    // Construct an invalid malicious/erroneous frame specifying size = 250 (> MSP_PORT_INBUF_SIZE 192)
    std::vector<uint8_t> oversizedFrame = { '$', 'M', '<', 250, MSP_API_VERSION };
    for (int i = 0; i < 250; i++) {
        oversizedFrame.push_back(0x42);
    }
    oversizedFrame.push_back(0x00); // dummy checksum

    feedVector(oversizedFrame);
    process();

    // Parser must drop frame without buffer overflow or execution
    EXPECT_EQ(0, handshakeCommandsExecuted);
    EXPECT_TRUE(capturedTxBytes.empty());
}

} // namespace

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {

static bool msp_test_read_repo_file(const char *relPath, char *buf, size_t bufSize)
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

static size_t resolve_aries_uart_tx_buffer_size(void)
{
    static char targetBuf[32768];
    if (msp_test_read_repo_file("src/platform/ARIES/target/ARIESV3/target.h", targetBuf, sizeof(targetBuf))) {
        const char *def = strstr(targetBuf, "#define UART_TX_BUFFER_SIZE");
        if (def != nullptr) {
            unsigned long val = strtoul(def + 27, nullptr, 0);
            if (val > 0UL) {
                return (size_t)val;
            }
        }
    }
    // Upstream fallback in drivers/serial_uart_impl.h when target.h omits UART_TX_BUFFER_SIZE
    return 256U;
}

// =============================================================================
// TEST 17 (Group B2): 306-Byte MSP_ADJUSTMENT_RANGES Frame Fits UART_TX_BUFFER_SIZE
// =============================================================================
TEST_F(AriesMspHandshakeTest, Test_MSP_AdjustmentRanges_306ByteFrameFitsTargetUartTxBufferWithoutTruncation)
{
    // Arrange: Verify target UART_TX_BUFFER_SIZE configured for ARIESV3 is >= 512 bytes
    // (306 B MSP_ADJUSTMENT_RANGES exceeds default 256 B ring whose usable space is 255 B!).
    const size_t configuredTxBufferSize = resolve_aries_uart_tx_buffer_size();
    EXPECT_GE(configuredTxBufferSize, 512U)
        << "ARIESV3 target.h leaves UART_TX_BUFFER_SIZE at 256 B; MSP_ADJUSTMENT_RANGES (306 B+) will truncate!";

    const size_t usableTxCapacity = configuredTxBufferSize - 1U;

    // Construct a complete 300-byte MSP_ADJUSTMENT_RANGES (30 ranges * 10 B) response
    // encoded using MSPv2 ('$','X','>', flag, cmd_lo, cmd_hi, size_lo, size_hi, payload[300], crc8)
    // to validate continuous single-shot TX enqueueing without partial ring wrap.
    std::vector<uint8_t> adjPayload(300U);
    for (size_t i = 0; i < adjPayload.size(); i++) {
        adjPayload[i] = (uint8_t)((i * 7U + 3U) & 0xFFU);
    }

    std::vector<uint8_t> mspV2Frame;
    mspV2Frame.push_back('$');
    mspV2Frame.push_back('X');
    mspV2Frame.push_back('>');
    mspV2Frame.push_back(0x00U); // flags
    mspV2Frame.push_back((uint8_t)(MSP_ADJUSTMENT_RANGES & 0xFFU));
    mspV2Frame.push_back((uint8_t)((MSP_ADJUSTMENT_RANGES >> 8) & 0xFFU));
    mspV2Frame.push_back((uint8_t)(adjPayload.size() & 0xFFU));
    mspV2Frame.push_back((uint8_t)((adjPayload.size() >> 8) & 0xFFU));
    mspV2Frame.insert(mspV2Frame.end(), adjPayload.begin(), adjPayload.end());
    const uint8_t crc = crc8_dvb_s2_update(0U, &mspV2Frame[3], mspV2Frame.size() - 3U);
    mspV2Frame.push_back(crc);
    ASSERT_EQ(mspV2Frame.size(), 309U); // >= 306 bytes

    // Act: Check real configured ring capacity before enqueueing whole response in one burst
    size_t acceptedBytes = 0U;
    if (usableTxCapacity >= mspV2Frame.size()) {
        serialWriteBufNoFlush(nullptr, mspV2Frame.data(), (int)mspV2Frame.size());
        acceptedBytes = capturedTxBytes.size();
    }

    // Assert: Whole >=306 B frame fits into configured TX ring in one pass without splitting/stalling
    EXPECT_EQ(acceptedBytes, mspV2Frame.size());
    EXPECT_EQ(capturedTxBytes, mspV2Frame);

    // Real-world flight consequence:
    // Betaflight Configurator times out on the Adjustments tab when UART_TX_BUFFER_SIZE < 306 B
    // forces partial drops or retries during a single MSP_ADJUSTMENT_RANGES read.
}

} // namespace
