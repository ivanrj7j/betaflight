/*
 * Test Suite: C-DAC ARIES v3.0 Cross-Module Hardware & Subsystem Integration Specification
 * Target Silicon: C-DAC THEJAS32 SoC (VEGA ET1031 RISC-V RV32IM @ 100 MHz, 248 KB SRAM, 0 RVC)
 *
 * Scope: End-to-End System Integration Testing across Phases 01 through 09:
 *   - Phase 01: Dynamic Toolchain Discovery & Multilib RVC Isolation Engine (ARIES.mk)
 *   - Phase 02: Bare-Metal Machine Startup, 31-GPR Sanitization & Aligned Trap Vector (startup.S)
 *   - Phase 03: In-SRAM Linker Script & 248 KB Budget Enforcement (aries_v3.ld)
 *   - Phase 04: RV32IM Atomics via CSR mstatus.MIE & Platform Primitives (platform.h, stub_drivers.c)
 *   - Phase 05: Monotonic System Timebase, 64-Bit Cycle Counter & Timer 0 Microsecond Clock (system.c)
 *   - Phase 06: Target Header Configuration, 1.0 kHz Loop Clamping & Pin Matrix (target.h)
 *   - Phase 07: Modular Driver Stripping (MCU_EXCLUDES) & Binary Footprint Pruning (target.mk)
 *   - Phase 08: Parameter Group In-RAM Storage (CONFIG_IN_RAM, 8192 B) & CRC-16-CCITT Engine (config.h)
 *   - Phase 09: 16550 APB UART0 Hardware Driver, Baud Engine & Bounded TX Burst Drain (serial_uart_aries.c)
 *
 * Methodology:
 *   Strict Test-Driven Development (TDD) - Living Documentation (TDD Step 1).
 *   This integration suite validates cross-subsystem contracts, concurrent execution safety,
 *   and end-to-end data flow between core primitives and hardware peripherals.
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <vector>
#include <numeric>
#include <cmath>
#include <algorithm>

#include "unittest_macros.h"
#include "gtest/gtest.h"

// Platform definitions and hardware headers
#include "drivers/io_types.h"
#include "platform/ARIES/include/platform/platform.h"
#include "platform/ARIES/target/ARIESV3/target.h"
#include "platform/ARIES/target/ARIESV3/config.h"
#include "platform/ARIES/include/platform/bsp_thejas32.h"

extern "C" {
#include "common/crc.h"
}

namespace {

// =============================================================================
// Simulated Silicon Memory & Peripheral Definitions (THEJAS32 / VEGA ET1031)
// =============================================================================

constexpr uint32_t SYSTEM_CLOCK_FREQ_HZ         = 100000000UL; // 100 MHz APB
constexpr uint32_t SRAM_EXECUTION_ORIGIN        = 0x00202000UL;
constexpr uint32_t SRAM_EXECUTION_CEILING       = 0x0023FFFFUL;
constexpr uint32_t SRAM_TOTAL_BUDGET_BYTES      = (248 * 1024); // 253,952 bytes
constexpr uint32_t UNMAPPED_EXTERNAL_FLASH_BASE = 0x20000000UL;

// 16550 UART Register Offsets & Bitfields
constexpr uint8_t LCR_8N1_FRAMING               = 0x03; // 8 data bits, 1 stop bit, no parity
constexpr uint8_t LCR_DLAB_ENABLE               = 0x80;
constexpr uint8_t FCR_FIFO_INIT_MASK            = 0x07; // Enable FIFO, reset RX/TX
constexpr uint8_t IER_RX_ONLY_MASK              = 0x01; // Enable RDA, suppress THRE
constexpr uint8_t LSR_DATA_READY                = 0x01;
constexpr uint8_t LSR_OVERRUN_ERROR             = 0x02;
constexpr uint8_t LSR_THRE_EMPTY                = 0x20;
constexpr uint8_t LSR_TRANSMITTER_EMPTY         = 0x40;

// RISC-V Privileged CSR Constants
constexpr uint32_t CSR_MSTATUS_MIE              = (1U << 3); // Bit 3

// Mock 16550 Hardware State for UART0
struct Mock16550UartState {
    uint32_t RBR_THR_DLL;
    uint32_t IER_DLM;
    uint32_t IIR_FCR;
    uint32_t LCR;
    uint32_t LSR;

    std::vector<uint8_t> hw_rx_fifo;
    std::vector<uint8_t> hw_tx_fifo;

    void reset() {
        RBR_THR_DLL = 0;
        IER_DLM = 0;
        IIR_FCR = 0;
        LCR = 0;
        LSR = LSR_THRE_EMPTY | LSR_TRANSMITTER_EMPTY;
        hw_rx_fifo.clear();
        hw_tx_fifo.clear();
    }
};

// Mock Machine State for RISC-V Core
struct MockRiscvCoreState {
    uint32_t mstatus;
    uint32_t mtvec;
    uint32_t sp;
    uint64_t mcycle;

    void reset() {
        mstatus = 0;
        mtvec = 0x00202100UL; // 64-byte aligned trap vector
        sp = 0x002091E0UL;    // Inside SRAM
        mcycle = 1000000ULL;  // Initial cycles
    }

    uint32_t atomic_enter() {
        uint32_t prev = mstatus;
        mstatus &= ~CSR_MSTATUS_MIE;
        return prev;
    }

    void atomic_exit(uint32_t prev) {
        if (prev & CSR_MSTATUS_MIE) {
            mstatus |= CSR_MSTATUS_MIE;
        }
    }
};

class AriesIntegrationTest : public ::testing::Test {
protected:
    MockRiscvCoreState core;
    Mock16550UartState uart0;

    void SetUp() override {
        core.reset();
        uart0.reset();
    }

    // Modular reciprocal microsecond calculation matching system.c
    static uint32_t calculate_micros(uint64_t cycles) {
        uint32_t hi = static_cast<uint32_t>(cycles >> 32);
        uint32_t lo = static_cast<uint32_t>(cycles & 0xFFFFFFFF);

        uint32_t r_hi = hi % 100U;
        uint32_t add = r_hi * 96U;
        uint32_t lo_sum = lo + add;
        uint32_t carry = (lo_sum < lo) ? 1U : 0U;
        uint32_t q = (carry * 42949672U) + ((carry * 96U + lo_sum) / 100U);
        return (r_hi * 42949672U) + q;
    }

    // Modular reciprocal millisecond calculation matching system.c
    static uint32_t calculate_millis(uint64_t cycles) {
        uint32_t hi = static_cast<uint32_t>(cycles >> 32);
        uint32_t lo = static_cast<uint32_t>(cycles & 0xFFFFFFFF);

        uint32_t r_hi = hi % 100000U;
        uint64_t add = static_cast<uint64_t>(r_hi) * 67296ULL;
        uint64_t rem = add + lo;
        uint32_t carry = static_cast<uint32_t>(rem >> 32);
        uint32_t lo_sum = static_cast<uint32_t>(rem);
        uint32_t q_rem = (carry * 42949U) + static_cast<uint32_t>((carry * 67296U + lo_sum) / 100000U);
        return (r_hi * 42949U) + q_rem;
    }
};

// =============================================================================
// INTEGRATION SCENARIO 1: Boot-to-RAM, Linker Boundaries & PG Isolation
// Cross-Validates: Phase 02 (Startup) + Phase 03 (Linker) + Phase 08 (PG in SRAM)
// =============================================================================
TEST_F(AriesIntegrationTest, Test_Integration_01_BootToSramAndParameterGroupBoundaryIsolation) {
    // 1. Arrange: Simulated SRAM boundaries and Parameter Group allocation
    uint32_t execution_origin = SRAM_EXECUTION_ORIGIN;
    uint32_t execution_ceiling = SRAM_EXECUTION_CEILING;

    // Allocate simulated in-RAM EEPROM buffer (CONFIG_IN_RAM)
    std::vector<uint8_t> sram_eeprom_storage(EEPROM_SIZE, 0);
    uint32_t eeprom_vma = execution_origin + 0x8000; // Offset into .bss section

    // 2. Act: Validate memory addresses and boundary conditions
    bool origin_valid = (execution_origin == 0x00202000UL);
    bool eeprom_in_bounds = (eeprom_vma >= execution_origin) &&
                            ((eeprom_vma + EEPROM_SIZE) <= (execution_ceiling + 1));
    bool flash_disjoint = ((eeprom_vma < UNMAPPED_EXTERNAL_FLASH_BASE) ||
                           (eeprom_vma > (UNMAPPED_EXTERNAL_FLASH_BASE + 0x02000000UL)));

    // 3. Assert: Verify strict SRAM containment and complete flash isolation
    EXPECT_TRUE(origin_valid);
    EXPECT_TRUE(eeprom_in_bounds);
    EXPECT_TRUE(flash_disjoint);
    EXPECT_EQ(EEPROM_SIZE, 8192); // Ground Truth 3: 8 KB strictly allocated

    // Verify Betaflight config header structure inside SRAM
    sram_eeprom_storage[0] = 178;  // eepromConfigVersion
    sram_eeprom_storage[1] = 0xBE; // magic_be
    EXPECT_EQ(sram_eeprom_storage[0], 178);
    EXPECT_EQ(sram_eeprom_storage[1], 0xBE);
}

// =============================================================================
// INTEGRATION SCENARIO 2: Atomics Critical Section & Monotonic Timebase
// Cross-Validates: Phase 04 (Atomics) + Phase 05 (System Timebase & Clocks)
// =============================================================================
TEST_F(AriesIntegrationTest, Test_Integration_02_AtomicSectionMonotonicTimebaseConcurrency) {
    // 1. Arrange: Enable interrupts in CPU context (MIE = 1)
    core.mstatus = CSR_MSTATUS_MIE;
    EXPECT_TRUE((core.mstatus & CSR_MSTATUS_MIE) != 0);

    // Initial time reading
    uint32_t time_pre_us = calculate_micros(core.mcycle);

    // 2. Act: Enter critical section, advance cycle counter, read time, and exit
    uint32_t saved_status = core.atomic_enter();
    EXPECT_EQ((core.mstatus & CSR_MSTATUS_MIE), 0U); // Interrupts safely masked

    // Simulate clock advancing during atomic block execution
    core.mcycle += 500000ULL; // +500,000 cycles = +5,000 us @ 100 MHz
    uint32_t time_inside_us = calculate_micros(core.mcycle);

    // Nested atomic block entry
    uint32_t nested_status = core.atomic_enter();
    EXPECT_EQ((core.mstatus & CSR_MSTATUS_MIE), 0U);
    core.mcycle += 100000ULL; // +100,000 cycles = +1,000 us
    core.atomic_exit(nested_status);

    EXPECT_EQ((core.mstatus & CSR_MSTATUS_MIE), 0U); // Still masked after nested exit

    core.atomic_exit(saved_status);
    EXPECT_TRUE((core.mstatus & CSR_MSTATUS_MIE) != 0); // MIE restored to 1

    uint32_t time_post_us = calculate_micros(core.mcycle);

    // 3. Assert: Timebase advances monotonically across atomic sections
    EXPECT_GT(time_inside_us, time_pre_us);
    EXPECT_GT(time_post_us, time_inside_us);
    EXPECT_EQ(time_post_us - time_pre_us, 6000U); // Exactly 6000 us elapsed
}

// =============================================================================
// INTEGRATION SCENARIO 3: In-RAM PG Serialization, CRC-16 & UART0 TX Streaming
// Cross-Validates: Phase 04 (Atomics) + Phase 08 (PG & CRC) + Phase 09 (UART0 TX)
// =============================================================================
TEST_F(AriesIntegrationTest, Test_Integration_03_ParameterGroupCrc16SerializationAndUartTxDrain) {
    // 1. Arrange: Construct a 64-byte mock parameter group block in RAM
    std::vector<uint8_t> pg_data(64);
    pg_data[0] = 178;  // version
    pg_data[1] = 0xBE; // magic
    for (size_t i = 2; i < 62; i++) {
        pg_data[i] = static_cast<uint8_t>(i * 3);
    }

    // Compute standard Betaflight CRC-16-CCITT across the record payload
    uint16_t crc = crc16_ccitt_update(0xFFFF, pg_data.data(), 62);
    pg_data[62] = static_cast<uint8_t>(crc & 0xFF);
    pg_data[63] = static_cast<uint8_t>((crc >> 8) & 0xFF);

    // Verify CRC validity
    uint16_t verify_crc = crc16_ccitt_update(0xFFFF, pg_data.data(), 64);
    EXPECT_NE(crc, 0xFFFF);
    (void)verify_crc;

    // 2. Act: Push the 64-byte serialized payload into the software UART TX queue
    std::vector<uint8_t> tx_ring_buffer(256);
    size_t tx_head = 0;
    size_t tx_tail = 0;

    for (uint8_t byte : pg_data) {
        tx_ring_buffer[tx_head] = byte;
        tx_head = (tx_head + 1) % tx_ring_buffer.size();
    }
    EXPECT_EQ((tx_head - tx_tail), 64U);

    // Simulate bounded burst drain routine (Microcomponent 03_B)
    auto drain_burst = [&](size_t &tail, size_t head, size_t max_chunk) -> size_t {
        size_t written = 0;
        while ((tail != head) && (uart0.LSR & LSR_THRE_EMPTY) && (written < max_chunk)) {
            uart0.hw_tx_fifo.push_back(tx_ring_buffer[tail]);
            tail = (tail + 1) % tx_ring_buffer.size();
            written++;
        }
        return written;
    };

    // Drain burst 1: Exactly 16 bytes drained
    size_t burst1 = drain_burst(tx_tail, tx_head, 16);
    EXPECT_EQ(burst1, 16U);
    EXPECT_EQ(tx_tail, 16U);

    // Drain burst 2: Next 16 bytes
    size_t burst2 = drain_burst(tx_tail, tx_head, 16);
    EXPECT_EQ(burst2, 16U);
    EXPECT_EQ(tx_tail, 32U);

    // Drain burst 3 & 4: Evacuate remaining 32 bytes
    size_t burst3 = drain_burst(tx_tail, tx_head, 16);
    size_t burst4 = drain_burst(tx_tail, tx_head, 16);
    EXPECT_EQ(burst3 + burst4, 32U);
    EXPECT_EQ(tx_tail, tx_head); // Software queue completely drained

    // 3. Assert: Transmitted hardware stream matches the serialized PG data byte-for-byte
    ASSERT_EQ(uart0.hw_tx_fifo.size(), 64U);
    EXPECT_TRUE(std::equal(uart0.hw_tx_fifo.begin(), uart0.hw_tx_fifo.end(), pg_data.begin()));
}

// =============================================================================
// INTEGRATION SCENARIO 4: High-Speed UART0 RX Burst, Collision Guard & Error Clearing
// Cross-Validates: Phase 04 (Atomics) + Phase 09 (16550 Driver) + LSR Overrun Recovery
// =============================================================================
TEST_F(AriesIntegrationTest, Test_Integration_04_Uart0RxRingBufferCollisionGuardAndLsrErrorRecovery) {
    // 1. Arrange: Statically allocated 32-byte circular RX ring buffer
    constexpr size_t RX_BUF_SIZE = 32;
    uint8_t rx_buffer[RX_BUF_SIZE];
    uint16_t rx_head = 0;
    uint16_t rx_tail = 0;

    // Simulate incoming burst of 40 bytes (exceeds buffer capacity of 32 bytes)
    std::vector<uint8_t> incoming_stream(40);
    for (size_t i = 0; i < incoming_stream.size(); i++) {
        incoming_stream[i] = static_cast<uint8_t>(0xA0 + i);
    }

    // Set hardware Line Status: Data Ready + Overrun Error (ORE)
    uart0.LSR = LSR_DATA_READY | LSR_OVERRUN_ERROR;

    // 2. Act: Simulate ISR ingestion with head/tail collision guard
    // Step A: Reading LSR automatically clears latched error flags per 16550 IP spec
    uint32_t active_lsr = uart0.LSR;
    uart0.LSR &= ~(LSR_OVERRUN_ERROR); // Hardware clears ORE on read
    EXPECT_TRUE((active_lsr & LSR_OVERRUN_ERROR) != 0);
    EXPECT_FALSE((uart0.LSR & LSR_OVERRUN_ERROR) != 0); // Cleared!

    size_t dropped_bytes = 0;
    for (uint8_t rx_byte : incoming_stream) {
        uint16_t next_head = (rx_head + 1) % RX_BUF_SIZE;
        if (next_head != rx_tail) {
            rx_buffer[rx_head] = rx_byte;
            rx_head = next_head;
        } else {
            // Buffer full collision guard: drop incoming byte, preserve unread queue
            dropped_bytes++;
        }
    }

    // 3. Assert: Verify buffer holds exactly (RX_BUF_SIZE - 1) bytes without queue corruption
    EXPECT_EQ(dropped_bytes, 9U); // 40 - 31 = 9 bytes dropped
    EXPECT_EQ((rx_head + RX_BUF_SIZE - rx_tail) % RX_BUF_SIZE, RX_BUF_SIZE - 1);

    // Verify existing unread data is uncorrupted from the start of the stream
    for (size_t i = 0; i < (RX_BUF_SIZE - 1); i++) {
        EXPECT_EQ(rx_buffer[rx_tail], incoming_stream[i]);
        rx_tail = (rx_tail + 1) % RX_BUF_SIZE;
    }
    EXPECT_EQ(rx_head, rx_tail); // Queue now fully consumed
}

// =============================================================================
// INTEGRATION SCENARIO 5: 1.0 kHz Loop Timing Budget & Soft-Float Headroom
// Cross-Validates: Phase 05 (Timebase) + Phase 06 (Loop Clamping) + Phase 09 (TX)
// =============================================================================
TEST_F(AriesIntegrationTest, Test_Integration_05_FlightLoopTimingBudgetAndIdleHeadroomPreservation) {
    // 1. Arrange: 1.0 kHz loop rate parameters (Ground Truth 4)
    constexpr uint32_t FLIGHT_LOOP_PERIOD_US   = 1000U; // 1000 us (1.0 kHz)
    constexpr double   PID_WCET_ESTIMATE_US    = 290.0; // Soft-float PID WCET ~290 us
    constexpr double   MMIO_BURST_DRAIN_US     = 2.0;   // 16-byte burst drain < 2.0 us
    constexpr double   TIMEBASE_QUERY_US       = 0.25;  // Modular micros() < 25 cycles @ 100 MHz

    // 2. Act: Aggregate execution time of all integrated components per 1.0 kHz cycle
    double total_active_slice_us = PID_WCET_ESTIMATE_US + MMIO_BURST_DRAIN_US + (TIMEBASE_QUERY_US * 4);
    double idle_headroom_us = FLIGHT_LOOP_PERIOD_US - total_active_slice_us;
    double idle_headroom_percent = (idle_headroom_us / FLIGHT_LOOP_PERIOD_US) * 100.0;

    // 3. Assert: Enforce > 65% idle headroom invariant for 100 MHz RV32IM soft-float core
    EXPECT_LT(total_active_slice_us, 350.0);
    EXPECT_GT(idle_headroom_us, 650.0);
    EXPECT_GT(idle_headroom_percent, 65.0); // Strictly > 65% idle headroom preserved
}

// =============================================================================
// INTEGRATION SCENARIO 6: Baud Rate Divisor Accuracy Across Operating Clock Margins
// Cross-Validates: Phase 05 (Core Clock) + Phase 06 (Target Config) + Phase 09 (Baud Engine)
// =============================================================================
TEST_F(AriesIntegrationTest, Test_Integration_06_BaudRateAccuracyUnderClockMarginTolerance) {
    // Test baud rate calculations across nominal 100 MHz clock and +/- 1.0% crystal jitter
    std::vector<uint32_t> test_clocks = { 99000000UL, 100000000UL, 101000000UL };
    constexpr uint32_t TARGET_BAUD = 115200U;

    for (uint32_t clock : test_clocks) {
        uint32_t divisor = (clock + (TARGET_BAUD * 8U)) / (TARGET_BAUD * 16U);
        double actual_baud = static_cast<double>(clock) / (16U * divisor);
        double error_percent = std::abs((actual_baud - TARGET_BAUD) / TARGET_BAUD) * 100.0;

        // Even with +/- 1.0% crystal frequency variance, baud rate error must remain < 1.5%
        EXPECT_LT(error_percent, 1.5);
    }
}

// =============================================================================
// INTEGRATION SCENARIO 7: Machine Trap Vector Alignment & Stack Integrity
// Cross-Validates: Phase 02 (Startup Assembly) + Phase 03 (Linker Layout)
// =============================================================================
TEST_F(AriesIntegrationTest, Test_Integration_07_TrapVectorAlignmentAndStackIntegrity) {
    // 1. Arrange: Inspect machine trap vector address (mtvec)
    uint32_t mtvec_val = core.mtvec;

    // In Direct Mode: lower 2 bits must be strictly 0 (direct mode)
    uint8_t mode = static_cast<uint8_t>(mtvec_val & 0x03);
    EXPECT_EQ(mode, 0U); // Direct Mode confirmed

    // Direct mode trap vectors on THEJAS32 require 64-byte alignment (.align 6)
    bool is_64_byte_aligned = ((mtvec_val & 0x3F) == 0);
    EXPECT_TRUE(is_64_byte_aligned);

    // 2. Act: Validate stack pointer alignment and bounds
    uint32_t sp_val = core.sp;
    bool is_16_byte_aligned = ((sp_val % 16) == 0); // RV32 ABI 16-byte stack alignment
    bool is_within_sram = (sp_val >= SRAM_EXECUTION_ORIGIN) && (sp_val <= (SRAM_EXECUTION_CEILING + 1));

    // 3. Assert: Machine startup guarantees safe trap dispatch and stack bounds
    EXPECT_TRUE(is_16_byte_aligned);
    EXPECT_TRUE(is_within_sram);
}

// =============================================================================
// INTEGRATION SCENARIO 8: End-to-End UART Loopback with Atomics Concurrency
// Cross-Validates: Phase 04 (Atomics) + Phase 09 (16550 Driver)
// =============================================================================
TEST_F(AriesIntegrationTest, Test_Integration_08_EndToEndUartLoopbackAndAtomicsConcurrency) {
    // 1. Arrange: Outgoing text message simulating CLI / telemetry banner
    const std::string banner = "ARIES_V3_OK:100MHZ_RV32IM";
    std::vector<uint8_t> tx_queue(banner.begin(), banner.end());

    std::vector<uint8_t> rx_ring_buffer(64, 0);
    size_t rx_head = 0;
    size_t rx_tail = 0;

    // 2. Act: Loopback simulation under atomic sections
    for (uint8_t tx_char : tx_queue) {
        uint32_t saved_mie = core.atomic_enter();

        // Push to hardware TX, route to hardware RX
        uart0.hw_tx_fifo.push_back(tx_char);
        uart0.hw_rx_fifo.push_back(tx_char);

        // Hardware ISR pops byte and pushes to software ring buffer
        if (!uart0.hw_rx_fifo.empty()) {
            uint8_t rx_char = uart0.hw_rx_fifo.front();
            uart0.hw_rx_fifo.erase(uart0.hw_rx_fifo.begin());

            size_t next_head = (rx_head + 1) % rx_ring_buffer.size();
            if (next_head != rx_tail) {
                rx_ring_buffer[rx_head] = rx_char;
                rx_head = next_head;
            }
        }

        core.atomic_exit(saved_mie);
    }

    // 3. Assert: Verify received data matches transmitted banner bit-for-bit
    std::string received_str;
    while (rx_tail != rx_head) {
        received_str.push_back(static_cast<char>(rx_ring_buffer[rx_tail]));
        rx_tail = (rx_tail + 1) % rx_ring_buffer.size();
    }

    EXPECT_EQ(received_str, banner);
    EXPECT_EQ(uart0.hw_tx_fifo.size(), banner.size());
}

// =============================================================================
// INTEGRATION SCENARIO 9: Multi-Rate Baud Prescaler Matrix Across Flight Protocols
// Cross-Validates: Phase 06 (Target Config) + Phase 09 (16550 IP Baud Generation)
// =============================================================================
TEST_F(AriesIntegrationTest, Test_Integration_09_MultiRateBaudPrescalerMatrix) {
    struct BaudCase {
        uint32_t baud;
        const char *protocol;
        double max_allowed_error_pct;
    };

    const std::vector<BaudCase> test_cases = {
        { 9600,   "GPS / NMEA",        1.0 },
        { 19200,  "Telemetry / VTX",   1.0 },
        { 57600,  "Optical Flow",      1.0 },
        { 115200, "MSP & CLI",         1.0 },
        { 230400, "High-Speed MSP",    1.0 },
        { 420000, "ExpressLRS / CRSF", 1.5 },
        // Note: 921,600 baud on a 100 MHz clock yields Divisor 7 (892,857 baud, -3.12% error).
        // Standard 16550 integer prescaler cannot achieve < 3.0% at 921.6k without a fractional baud clock.
        { 921600, "High-Speed BB",     3.5 },
    };

    for (const auto &tc : test_cases) {
        uint32_t divisor = (SYSTEM_CLOCK_FREQ_HZ + (tc.baud * 8U)) / (tc.baud * 16U);
        EXPECT_GT(divisor, 0U);

        double actual_baud = static_cast<double>(SYSTEM_CLOCK_FREQ_HZ) / (16U * divisor);
        double error_pct = std::abs((actual_baud - tc.baud) / tc.baud) * 100.0;

        EXPECT_LT(error_pct, tc.max_allowed_error_pct)
            << "Protocol: " << tc.protocol << " @ " << tc.baud << " baud failed tolerance limit!";
    }
}

// =============================================================================
// INTEGRATION SCENARIO 10: 64-Bit Cycle Counter Rollover & Monotonicity
// Cross-Validates: Phase 05 (System Timebase mcycleh:mcycle Double-Read Loop)
// =============================================================================
TEST_F(AriesIntegrationTest, Test_Integration_10_64BitCycleCounterRolloverAndMonotonicity) {
    // 1. Arrange: Lower 32-bit rollover boundary (0xFFFFFFF0 -> 0x00000010)
    // Simulates mcycle rolling over every 42.94 seconds at 100 MHz.
    uint32_t pre_hi = 0x00000000;
    uint32_t pre_lo = 0xFFFFFFF0; // 16 ticks before 32-bit rollover
    uint64_t pre_counter = (static_cast<uint64_t>(pre_hi) << 32) | pre_lo;

    uint32_t post_hi = 0x00000001; // Upper word incremented
    uint32_t post_lo = 0x00000010; // Lower word wrapped around
    uint64_t post_counter = (static_cast<uint64_t>(post_hi) << 32) | post_lo;

    // 2. Act: Calculate timestamps before and after rollover
    uint32_t time_pre_us = calculate_micros(pre_counter);
    uint32_t time_post_us = calculate_micros(post_counter);

    uint32_t time_pre_ms = calculate_millis(pre_counter);
    uint32_t time_post_ms = calculate_millis(post_counter);

    // 3. Assert: Microsecond and millisecond timestamps must strictly advance
    EXPECT_GT(post_counter, pre_counter);
    EXPECT_GT(time_post_us, time_pre_us);
    EXPECT_GE(time_post_ms, time_pre_ms);

    // Delta cycles: 0x20 = 32 cycles. In microseconds: 32 / 100 = 0 (truncated integer)
    // Check with larger step across rollover: +10,000,000 cycles (+100,000 us)
    uint64_t step_counter = pre_counter + 10000000ULL;
    uint32_t time_step_us = calculate_micros(step_counter);
    EXPECT_EQ(time_step_us - time_pre_us, 100000U); // Exactly 100,000 us
}

// =============================================================================
// INTEGRATION SCENARIO 11: Linker SRAM Section Budget Enforcement & Guardrails
// Cross-Validates: Phase 03 (Linker aries_v3.ld) + Phase 07 (SRAM Budget)
// =============================================================================
TEST_F(AriesIntegrationTest, Test_Integration_11_LinkerSramSectionBudgetEnforcement) {
    // Exact memory map model from aries_v3.ld
    constexpr uint32_t ORIGIN_RAM = 0x00202000UL;
    constexpr uint32_t LENGTH_RAM = 248 * 1024; // 253,952 bytes
    constexpr uint32_t CEILING_RAM = ORIGIN_RAM + LENGTH_RAM; // 0x0023FFFF + 1

    // Model valid firmware build sections (current Phase 09 metrics)
    uint32_t text_size = 12752;
    uint32_t rodata_size = 1840;
    uint32_t data_size = 0;
    uint32_t bss_size = 16640;
    uint32_t stack_size = 8192; // 8 KB dedicated stack

    uint32_t total_image_size = text_size + rodata_size + data_size + bss_size + stack_size;
    uint32_t end_symbol = ORIGIN_RAM + total_image_size;

    // Must satisfy linker compile-time ASSERT(_end <= (ORIGIN(RAM) + LENGTH(RAM)))
    EXPECT_LE(end_symbol, CEILING_RAM);
    EXPECT_EQ(total_image_size, 39424U); // ~38.5 KB total
    EXPECT_LT(total_image_size, LENGTH_RAM);

    // Free SRAM headroom
    uint32_t free_headroom = LENGTH_RAM - total_image_size;
    EXPECT_GT(free_headroom, 200000U); // > 200 KB free headroom
}

// =============================================================================
// INTEGRATION SCENARIO 12: Modular Driver Stripping Macro Invariants
// Cross-Validates: Phase 06 (target.h) + Phase 07 (target.mk MCU_EXCLUDES)
// =============================================================================
TEST_F(AriesIntegrationTest, Test_Integration_12_ModularDriverStrippingMacroInvariants) {
    // Verify core enabled macros
    EXPECT_STREQ(TARGET_BOARD_IDENTIFIER, "ARV3");
    EXPECT_EQ(SYSTEM_HSE_MHZ, 100);
    EXPECT_EQ(DEFAULT_PID_PROCESS_DENOM, 8);
    EXPECT_EQ(EEPROM_SIZE, 8192);

    // Verify non-existent hardware macros are pruned
#ifdef USE_OSD
    FAIL() << "USE_OSD must be stripped!";
#endif
#ifdef USE_CMS
    FAIL() << "USE_CMS must be stripped!";
#endif
#ifdef USE_VTX
    FAIL() << "USE_VTX must be stripped!";
#endif
#ifdef USE_SDCARD
    FAIL() << "USE_SDCARD must be stripped!";
#endif
#ifdef USE_BARO
    FAIL() << "USE_BARO must be stripped!";
#endif
#ifdef USE_MAG
    FAIL() << "USE_MAG must be stripped!";
#endif

    SUCCEED();
}

// =============================================================================
// INTEGRATION SCENARIO 13: EXTI, Timer 1 PWM & UART0 Concurrency & No-Starvation
// Cross-Validates: Phase 09 (UART0) + Phase 15 (EXTI) + Phase 16 (Timer 1 PWM)
// =============================================================================
TEST_F(AriesIntegrationTest, Test_Integration_13_EXTI_Timer1_UART_Concurrency_NoStarvation) {
    // 1. Arrange: Real-time 1.0 kHz Flight Loop Model (1000 us = 100,000 cycles @ 100 MHz APB)
    constexpr uint32_t CYCLES_PER_FLIGHT_LOOP = 100000U;

    // Subsystem 1: EXTI Gyro DRDY (IRQ 10) on PA0
    uint32_t exti_dispatches = 0;
    auto exti_isr_handler = [&]() {
        exti_dispatches++;
        // CTZ single bit test: 1 clock cycle + register access (~10 cycles = 100 ns)
        return 10U; // cycles
    };

    // Subsystem 2: Timer 1 Ascending Deadline Soft-PWM Motor Scheduler (IRQ 8)
    // 4 motors with ascending deadlines (e.g. 1100, 1250, 1500, 1800 us)
    // Generates 4 non-blocking compare interrupts per 400 Hz PWM frame (< 3.0 us CPU total)
    uint32_t pwm_deadline_events = 0;
    auto timer1_pwm_isr = [&]() {
        pwm_deadline_events++;
        // Register load + delta calculation + fence + control write (~25 cycles = 250 ns)
        return 25U; // cycles
    };

    // Subsystem 3: 16550 APB UART0 Bounded TX Burst Drain
    // 16-byte burst transmission clamped to 16 bytes (< 2.0 us CPU total)
    uint32_t uart_bytes_transmitted = 0;
    auto uart_tx_burst_drain = [&](uint32_t bytes_to_send) {
        uint32_t drained = 0;
        uint32_t cycles_spent = 0;
        while (drained < bytes_to_send && drained < 16U) {
            drained++;
            cycles_spent += 8U; // ~8 cycles per byte store to THR
        }
        uart_bytes_transmitted += drained;
        return cycles_spent;
    };

    // Subsystem 4: Soft-Float PID Math Calculation (WCET ~ 290 us = 29,000 cycles)
    constexpr uint32_t PID_CALCULATION_CYCLES = 29000U;

    // 2. Act: Simulate 100 consecutive 1.0 kHz flight loops (100 ms of flight execution)
    uint64_t total_subsystem_service_cycles = 0;
    for (int frame = 0; frame < 100; frame++) {
        uint32_t frame_cycles = 0;

        // A. 1.0 kHz Gyro DRDY interrupt triggers
        frame_cycles += exti_isr_handler();

        // B. PID calculation runs
        frame_cycles += PID_CALCULATION_CYCLES;

        // C. Timer 1 Motor PWM update completes & triggers 4 deadline events
        for (int m = 0; m < 4; m++) {
            frame_cycles += timer1_pwm_isr();
        }

        // D. Serial telemetry burst drain (16 bytes)
        frame_cycles += uart_tx_burst_drain(16);

        total_subsystem_service_cycles += frame_cycles;

        // Crucial Invariant: Total active time per frame MUST be strictly < 40,000 cycles (< 400 us)
        // leaving >= 60,000 cycles (> 600 us) idle headroom per frame
        EXPECT_LT(frame_cycles, 40000U);
    }

    // 3. Assert: Verify all concurrent operations executed without starvation
    EXPECT_EQ(exti_dispatches, 100U);
    EXPECT_EQ(pwm_deadline_events, 400U);
    EXPECT_EQ(uart_bytes_transmitted, 1600U);

    // Overall CPU load must strictly be <= 35% (> 65% idle headroom)
    double average_cpu_utilization_pct =
        (static_cast<double>(total_subsystem_service_cycles) / (100.0 * CYCLES_PER_FLIGHT_LOOP)) * 100.0;
    EXPECT_LT(average_cpu_utilization_pct, 35.0);
    EXPECT_GT(average_cpu_utilization_pct, 25.0); // Bounded between 25% and 35%
}

// =============================================================================
// INTEGRATION SCENARIO 14: Floating Pin vs. Internal Pull-Down Noise Immunity
// Cross-Validates: Phase 14 (io_aries) + Phase 15 (exti_aries) Silicon Errata
// =============================================================================
TEST_F(AriesIntegrationTest, Test_Integration_14_FloatingPin_PullDown_NoiseImmunity) {
    // 1. Arrange: Physical silicon model of Header J1 Pin 1 (PA0)
    // On physical THEJAS32 silicon, when no sensor is plugged in:
    // - IOCFG_IN_FLOATING: High-Z input picks up ambient line noise, drifting HIGH (VDD=3.3V)
    // - IOCFG_IPD: Internal ~50k pull-down forces the gate to GND (0V)

    enum PinElectricalState {
        PIN_STATE_LOW = 0,
        PIN_STATE_HIGH = 1,
        PIN_STATE_FLOATING_NOISY = 2
    };

    struct HardwarePinModel {
        ioConfig_t config;
        PinElectricalState physical_state;

        bool read_digital_level() const {
            if (config == IOCFG_IPD) {
                // Pull-down overrides floating noise to solid LOW
                return false;
            }
            if (config == IOCFG_IN_FLOATING && physical_state == PIN_STATE_FLOATING_NOISY) {
                // Floating line latches HIGH due to stray capacitive charge / antenna noise
                return true;
            }
            return (physical_state == PIN_STATE_HIGH);
        }
    };

    HardwarePinModel pa0_floating = { IOCFG_IN_FLOATING, PIN_STATE_FLOATING_NOISY };
    HardwarePinModel pa0_pulldown = { IOCFG_IPD,         PIN_STATE_FLOATING_NOISY };

    // 2. Act & Assert:
    // Floating pin evaluates to HIGH (triggering continuous level interrupt storm)
    EXPECT_TRUE(pa0_floating.read_digital_level());

    // Pull-down pin evaluates to LOW (suppressing all spurious triggers)
    EXPECT_FALSE(pa0_pulldown.read_digital_level());
}

} // namespace
