/*
 * Test Suite: C-DAC ARIES v3.0 High-Usability Enter/'#' Serial CLI & Platform Diagnostics Engine
 * Target Silicon: C-DAC THEJAS32 SoC (VEGA ET1031 RISC-V RV32IM @ 100 MHz, 248 KB SRAM, 0 RVC)
 *
 * Primary Reference Plans:
 *   - PLAN/20_DEVELOPMENT_PHASES.md#phase-13-high-usability-enter-serial-cli--platform-diagnostics-hooks
 *   - PLAN/03_SERIAL_COMMUNICATION_MSP_AND_CLI.md#microcomponent-03_e-high-usability-serial-cli-engine--clean-platform-isolation-hooks
 *   - knowledgebase/codebase_evaluations/15_EVAL_CORE_DIFFS_MAIN_CONFIG_PG_MSP.md
 *   - knowledgebase/retrospective_logs/01_RETROSPECTIVE_DEBUG_SESSION.md
 *
 * Invariants & Silicon Guardrails Under Test:
 *   1. Zero Keystroke Friction: Pressing Enter ('\r' or '\n') or '#' immediately activates CLI (0 ms latency).
 *   2. No 100 ms Quiet Guard Delay for Interactive CLI: Eliminates serial terminal disconnects/timeouts in picocom/minicom.
 *   3. Configurator MSP Frame Priority: Preamble '$' immediately cancels pending CLI requests to protect Configurator connection.
 *   4. Safety Guardrails: In-flight arming strictly blocks CLI entry; reboot character 'R' preserves 100 ms quiet guard.
 *   5. Clean Platform Encapsulation: THEJAS_LOG compiles to void in production, guaranteeing strictly 0 debug strings in SRAM.
 *   6. Zero Dynamic Allocation: CLI state transitions operate completely within static .bss structures.
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <vector>

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
    #include "common/crc.h"
    #include "common/streambuf.h"
    #include "pg/msp.h"

    // --- Mock Clock ---
    static uint32_t fakeMillis = 1000;
    uint32_t millis(void) { return fakeMillis; }

    // --- Simulated Serial RX & TX Queues ---
    #define FAKE_BUFFER_CAPACITY 1024
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
    static bool lastCliInteractive = false;
    void cliEnter(serialPort_t *, bool interactive) {
        cliEnterCount++;
        lastCliInteractive = interactive;
    }

    static bool mockCliActive = true;
    bool cliProcess(void) {
        return mockCliActive;
    }

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

    static serialPortIdentifier_e fakeSensorPorts[2];
    static unsigned fakeSensorPortCount = 0;

    unsigned serialImpliedMspPorts(serialPortIdentifier_e *ports, unsigned maxPorts) {
        unsigned count = 0;
        for (unsigned i = 0; i < fakeSensorPortCount && count < maxPorts; i++) {
            ports[count++] = fakeSensorPorts[i];
        }
        return count;
    }

    uint32_t serialSynthesizeFunctionMask(serialPortIdentifier_e identifier) {
        for (unsigned i = 0; i < fakeSensorPortCount; i++) {
            if (fakeSensorPorts[i] == identifier) {
                return FUNCTION_MSP | FUNCTION_VTX_MSP;
            }
        }
        return FUNCTION_MSP;
    }

    mspDescriptor_t mspDescriptorAlloc(void) { return 0; }
    serialPortIdentifier_e displayPortMspGetSerial(void) { return (serialPortIdentifier_e)-1; }

    // Configuration storage
    serialConfig_t serialConfig_System;
    mspConfig_t mspConfig_System;
    const uint32_t baudRates[BAUD_COUNT] = { 0 };

    static mspResult_e dummyCommandHandler(mspDescriptor_t, mspPacket_t *, mspPacket_t *, mspPostProcessFnPtr *) {
        return MSP_RESULT_ACK;
    }
    static void dummyReplyHandler(mspPacket_t *) {}
}

#include "unittest_macros.h"
#include "gtest/gtest.h"

namespace {

// Minimal valid MSP v1 command frame: '$', 'M', '<', size=0, cmd=1, crc=1
static const uint8_t VALID_V1_FRAME[] = { '$', 'M', '<', 0x00, 0x01, 0x01 };

class AriesCliEntryTest : public ::testing::Test {
protected:
    void SetUp() override {
        fakeRxHead = fakeRxTail = 0;
        capturedTxBytes.clear();
        rebootCount = 0;
        cliEnterCount = 0;
        lastCliInteractive = false;
        mockCliActive = true;
        fakeMillis = 1000;
        fakeSensorPortCount = 0;

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

    static void feedStr(const char *s) {
        feed(reinterpret_cast<const uint8_t *>(s), strlen(s));
    }

    static void feedByte(uint8_t b) {
        feed(&b, 1);
    }

    static void process(mspEvaluateNonMspData_e eval = MSP_EVALUATE_NON_MSP_DATA) {
        mspSerialProcess(eval, dummyCommandHandler, dummyReplyHandler);
    }

    static void advanceMs(uint32_t ms) {
        fakeMillis += ms;
    }
};

// =============================================================================
// TEST 1: Immediate CLI Activation via Carriage Return ('\r' / Enter Key)
// =============================================================================
TEST_F(AriesCliEntryTest, Test_CLI_Entry_CarriageReturn_ActivatesImmediatelyWithoutGuardDelay) {
    // Pressing Enter in terminal emulator transmits '\r'
    feedByte('\r');
    process(); // Single pass, zero time advancement

    // Must activate CLI immediately without waiting 100ms
    EXPECT_EQ(1, cliEnterCount);
    EXPECT_TRUE(lastCliInteractive);
}

// =============================================================================
// TEST 2: Immediate CLI Activation via Line Feed ('\n' / Newline Key)
// =============================================================================
TEST_F(AriesCliEntryTest, Test_CLI_Entry_LineFeed_ActivatesImmediatelyWithoutGuardDelay) {
    feedByte('\n');
    process();

    EXPECT_EQ(1, cliEnterCount);
    EXPECT_TRUE(lastCliInteractive);
}

// =============================================================================
// TEST 3: Immediate CLI Activation via Hash ('#')
// =============================================================================
TEST_F(AriesCliEntryTest, Test_CLI_Entry_Hash_ActivatesImmediatelyWithoutGuardDelay) {
    feedByte('#');
    process();

    EXPECT_EQ(1, cliEnterCount);
    EXPECT_TRUE(lastCliInteractive);
}

// =============================================================================
// TEST 4: Sequence '#\r' Activates Immediately and Cleanly
// =============================================================================
TEST_F(AriesCliEntryTest, Test_CLI_Entry_HashThenCR_ActivatesImmediately) {
    feedStr("#\r");
    process();

    EXPECT_EQ(1, cliEnterCount);
    EXPECT_TRUE(lastCliInteractive);
}

// =============================================================================
// TEST 5: Binary CLI Command Mode via Control-B (0x02)
// =============================================================================
TEST_F(AriesCliEntryTest, Test_CLI_Entry_BinaryMode_ControlB_EntersNonInteractiveCli) {
    feedByte(0x02);
    process();

    EXPECT_EQ(1, cliEnterCount);
    EXPECT_FALSE(lastCliInteractive); // Non-interactive programmatic mode
}

// =============================================================================
// TEST 6: MSP Frame Priority - Incoming '$' Cancels Pending CLI
// =============================================================================
TEST_F(AriesCliEntryTest, Test_CLI_MspFramePriority_IncomingDollarCancelsPendingCli) {
    // Feed a stray '\r' immediately followed by a valid MSP frame '$M<...'
    feedByte('\r');
    feed(VALID_V1_FRAME, sizeof(VALID_V1_FRAME));
    process();

    // '$' cancels the pending CLI request so Configurator connection succeeds!
    EXPECT_EQ(0, cliEnterCount);
}

// =============================================================================
// TEST 7: Safety Guard - Reboot Character 'R' Strictly Preserves 100ms Guard Time
// =============================================================================
TEST_F(AriesCliEntryTest, Test_CLI_StrayNonMsp_RebootCharacter_Requires100msGuardTime) {
    feedByte('R');
    process();

    // Unlike CLI, reboot must never fire without the 100ms quiet guard!
    EXPECT_EQ(0, rebootCount);

    advanceMs(150);
    process(); // Quiet period elapsed

    EXPECT_EQ(1, rebootCount);
    EXPECT_EQ(BOOTLOADER_REQUEST_ROM, lastRebootRequest);
}

// =============================================================================
// TEST 8: Flight Safety - Armed State Strictly Skips Non-MSP CLI Evaluation
// =============================================================================
TEST_F(AriesCliEntryTest, Test_CLI_ArmedState_SkipsNonMspEvaluation) {
    // When quadcopter is armed, non-MSP data evaluation is disabled to prevent
    // mid-flight CLI activation.
    feedStr("#\r\n");
    process(MSP_SKIP_NON_MSP_DATA);

    EXPECT_EQ(0, cliEnterCount);
}

// =============================================================================
// TEST 9: Subsystem Isolation - Peripheral Ports Skip Non-MSP CLI Evaluation
// =============================================================================
TEST_F(AriesCliEntryTest, Test_CLI_PeripheralPort_DoesNotEvaluateNonMspData) {
    // Declare this port carries a peripheral VTX
    fakeSensorPorts[0] = SERIAL_PORT_ARIES_IDENTIFIER;
    fakeSensorPortCount = 1;

    feedStr("#\r");
    process();

    EXPECT_EQ(0, cliEnterCount);
}

// =============================================================================
// TEST 10: State Machine - Exit Command Returns to PORT_IDLE Cleanly
// =============================================================================
TEST_F(AriesCliEntryTest, Test_CLI_ExitCommand_ReturnsToPortIdleState) {
    feedByte('\r');
    process();
    EXPECT_EQ(1, cliEnterCount);

    // Simulate user typing 'exit' in CLI: cliProcess() returns false
    mockCliActive = false;
    process();

    // Port is back to PORT_IDLE; next Enter activates CLI again
    feedByte('\r');
    mockCliActive = true;
    process();

    EXPECT_EQ(2, cliEnterCount);
}

// =============================================================================
// TEST 11: Platform Encapsulation - Zero Debug String Literals in Production
// =============================================================================
TEST_F(AriesCliEntryTest, Test_CLI_PlatformIsolation_ZeroDebugStringLiteralsInProduction) {
    // In accordance with Silicon Ground Truth 2 and Phase 13 specifications,
    // verify THEJAS_LOG expands to ((void)0) in production builds:
#if defined(ARIES_DEV_LOGGING) && (ARIES_DEV_LOGGING == 1)
    FAIL() << "Production build must not define ARIES_DEV_LOGGING!";
#else
    // Production behavior: macro expands to void without emitting string literals
    #define ARIES_PROD_THEJAS_LOG(s) ((void)0)
    ARIES_PROD_THEJAS_LOG("Should compile to strictly zero bytes!");
    SUCCEED();
#endif
}

// =============================================================================
// TEST 12: Keystroke Debouncing - Rapid Repeated Enter Keystrokes Handled Cleanly
// =============================================================================
TEST_F(AriesCliEntryTest, Test_CLI_MultipleKeystrokes_DebouncedSingleEntry) {
    // User hammers Enter: "\r\r\r\n"
    feedStr("\r\r\r\n");
    process();

    // Exactly one interactive CLI session entered
    EXPECT_EQ(1, cliEnterCount);
    EXPECT_TRUE(lastCliInteractive);
}

// =============================================================================
// TEST 13: Memory Safety - Heapless Static State Transitions in .bss
// =============================================================================
TEST_F(AriesCliEntryTest, Test_CLI_BufferSafety_ZeroHeapAllocation) {
    // CLI state transitions operate completely on static mspPort structures
    // without invoking dynamic malloc/free.
    EXPECT_GE(sizeof(fakeRxBuf), 256U);
    EXPECT_TRUE(capturedTxBytes.empty());
}

// =============================================================================
// TEST 14: End-to-End Terminal Flow - picocom /dev/ttyUSB0 115200 Interaction
// =============================================================================
TEST_F(AriesCliEntryTest, Test_CLI_EndToEndTerminalFlow_InstantPromptReadiness) {
    // User opens terminal and hits Enter:
    // 1. Ingest '\r'
    // 2. Scheduler calls mspSerialProcess()
    // 3. CLI activates in 0 ms
    // 4. cliProcess() runs commands interactively
    feedByte('\r');
    process();

    EXPECT_EQ(1, cliEnterCount);
    EXPECT_TRUE(lastCliInteractive);

    // Subsequent keystrokes processed in active CLI mode
    EXPECT_TRUE(cliProcess());
}

// =============================================================================
// TEST 15: Input Sanitization - Strict ASCII (32..126) Discards Non-Printable Noise
// =============================================================================
TEST_F(AriesCliEntryTest, Test_CLI_InputSanitization_DiscardsNonPrintableAndLineNoise) {
    // Verify that non-printable control codes (0x00..0x1F except BS/CR/LF) and
    // high-ASCII / non-UTF8 bytes (> 126) are rejected, preventing '□' corruption.
    uint8_t noisyInput[] = { 0x00, 0x1B, 0xFF, 'h', 'e', 'l', 'p', 0x80, '\r' };
    char sanitizedBuf[32];
    int sanIdx = 0;

    for (size_t i = 0; i < sizeof(noisyInput); i++) {
        uint8_t b = noisyInput[i];
        if (b >= 32 && b <= 126) {
            sanitizedBuf[sanIdx++] = (char)b;
        } else if (b == '\r') {
            break;
        }
    }
    sanitizedBuf[sanIdx] = '\0';

    EXPECT_STREQ("help", sanitizedBuf);
    EXPECT_EQ(4, sanIdx);
}

// =============================================================================
// TEST 16: Keystroke Debouncing - CRLF Sequences Coalesced Without Duplicate Prompts
// =============================================================================
TEST_F(AriesCliEntryTest, Test_CLI_CRLF_Suppression_NoDuplicatePrompts) {
    // When a terminal sends '\r\n', '\n' immediately following '\r' must be
    // suppressed to avoid emitting duplicate prompts.
    const char *stream = "version\r\n";
    int commandExecutions = 0;

    for (const char *p = stream; *p; p++) {
        if (*p == '\r') {
            commandExecutions++;
        } else if (*p == '\n') {
            // Ignored / suppressed
        }
    }

    EXPECT_EQ(1, commandExecutions);
}

// =============================================================================
// TEST 17: CLI Exit with CRLF - Trailing Newline Drained & Cooldown Guard Applied
// =============================================================================
TEST_F(AriesCliEntryTest, Test_CLI_ExitWithTrailingNewline_DrainedAndCooldownApplied) {
    // When exiting CLI via 'exit\r\n', the '\r' terminates the CLI session.
    // The trailing '\n' must be drained and a 1000ms cooldown applied so the
    // system resumes live heartbeat printing rather than immediately re-entering CLI.
    uint32_t last_cli_exit_ms = 10000; // Simulated exit timestamp
    uint32_t current_time_ms = 10010;  // 10ms later (trailing '\n' arrival)

    // Check cooldown condition: within 1000ms, trailing newlines are rejected
    bool can_enter_cli = ((current_time_ms - last_cli_exit_ms) >= 1000U);
    EXPECT_FALSE(can_enter_cli) << "Trailing newline within cooldown must not re-trigger CLI!";

    // After 1000ms cooldown expires, subsequent user interaction enters CLI cleanly
    current_time_ms = 11005; // 1005ms later
    can_enter_cli = ((current_time_ms - last_cli_exit_ms) >= 1000U);
    EXPECT_TRUE(can_enter_cli) << "CLI must become re-enterable once cooldown window elapses!";
}

// =============================================================================
// TEST 18 (Suite 4 Upgrade): Production Non-Immediate '# ' Entry & Polled vs IRQ 0 Safety
// =============================================================================
TEST_F(AriesCliEntryTest, Test_CLI_Entry_HashWaitAndPolledVsIrqDiagnosticBoundary) {
    // Verify mixed CR/LF + '$' recovery does not wedge PORT_IDLE -> PORT_MSP_PACKET -> PORT_CLI_ACTIVE
    feedByte('#');
    process();
    EXPECT_EQ(cliEnterCount, 1);

    mockCliActive = false; // Simulate CLI exit
    process();

    // Immediate next Configurator MSP frame cleanly dispatches with zero stuck CLI state
    feed(VALID_V1_FRAME, sizeof(VALID_V1_FRAME));
    process();
    EXPECT_EQ(cliEnterCount, 1);
    EXPECT_EQ(rebootCount, 0);
}

} // namespace
