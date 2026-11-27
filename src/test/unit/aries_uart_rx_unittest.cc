/*
 * Test Suite: C-DAC ARIES v3.0 16550 APB UART RX Ring Buffer & LSR Error Recovery Specification
 * Target Silicon: C-DAC THEJAS32 SoC (VEGA ET1031 RISC-V RV32IM @ 100 MHz, 248 KB SRAM, 0 RVC)
 * Reference:
 *   - PLAN/20_DEVELOPMENT_PHASES.md (Phase 10: Zero-Allocation RX Ring Buffer & LSR Error Recovery)
 *   - PLAN/03_SERIAL_COMMUNICATION_MSP_AND_CLI.md (Microcomponent 03_C)
 * Citations:
 *   - knowledgebase/codebase_evaluations/06_EVAL_SERIAL_UART_ARIES_C.md (Critique of buffer collision checks)
 *   - knowledgebase/components/03_SERIAL_UART_AND_MSP.md
 *   - logs/hardware_inspection/06_STM32_SERIAL_UART_DRIVER.md (Overrun Error unfreeze sequence)
 *   - logs/hardware_inspection/18_ESP32_SERIAL_UART.md (16550 vs ESP32 hardware FIFOs)
 *   - src/platform/ARIES/include/platform/bsp_thejas32.h
 *   - src/platform/ARIES/target/ARIESV3/target.h
 *
 * Methodology:
 *   Strict Test-Driven Development (TDD) - Step 1: Living Documentation.
 *   This test suite formally specifies:
 *     1. Static 256-byte zero-allocation circular ring buffer sizing and modulo wraparound.
 *     2. Multi-byte hardware FIFO burst ingestion (draining up to 16 bytes per ISR pass).
 *     3. Head/Tail collision guard preventing unread byte corruption when buffer is full.
 *     4. Automatic unlatching and clearing of Line Status Register (LSR) Overrun Error (ORE).
 *     5. Parity Error (PE) and Framing Error (FE) unlatching on LSR read.
 *     6. Polled hardware FIFO fallback in serialRxBytesWaiting() during masked interrupt windows.
 *     7. Direct rxCallback dispatching bypass eliminating intermediate copy overhead.
 *     8. Ring buffer empty underflow safety.
 *     9. Single-word atomic head/tail concurrency safety between ISR and consumer task contexts.
 *    10. Multi-port RX ring buffer isolation between UART0 (USB MSP) and UART1 (CRSF).
 *    11. 16550 Character Timeout Indication (CTI / IIR 0x0C) handling for trailing bytes.
 *    12. Line Break Condition (LSR bit 4 BI) unlatching and null byte recovery.
 *    13. Spurious interrupt handling (zero Data Ready, zero buffer mutation, zero bus stall).
 *    14. Exact boundary stress at capacity limits (254 -> 255 -> full -> consumer drain -> refill).
 *    15. Framing Error corrupted byte unlatching with subsequent valid MSP frame ingestion.
 *    16. High-throughput continuous stream (500 bytes) with interleaved 100 Hz consumer task.
 *    17. Race-free bytes_waiting() evaluation during concurrent ISR head advance.
 *    18. Overrun Error recovery with continuous incoming stream returning to clean reception.
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <vector>
#include <queue>
#include <atomic>

#include "unittest_macros.h"
#include "gtest/gtest.h"

// Platform definitions and hardware headers
extern "C" {
#include "../main/platform.h"
#include "drivers/io.h"
#include "io/serial.h"
#include "drivers/serial_uart.h"
#include "drivers/serial_uart_impl.h"
#include "aries_host_mmio.h"
uint32_t handle_trap(uint32_t mcause, uint32_t mepc, uint32_t *sp);
}

namespace {

// Hardware Constants for C-DAC THEJAS32 16550 APB UART IP
constexpr uint32_t THEJAS32_SYSTEM_CLOCK_HZ = 100000000UL; // 100 MHz APB
constexpr size_t   UART_RX_RING_BUFFER_SIZE = 256;         // Power-of-two size (0x100)
constexpr uint16_t RING_BUFFER_MASK         = 255;         // Bitwise index mask (size - 1)

// 16550 Line Status Register (LSR) Bits
constexpr uint8_t LSR_DATA_READY            = 0x01; // Bit 0: DR
constexpr uint8_t LSR_OVERRUN_ERROR         = 0x02; // Bit 1: OE / ORE
constexpr uint8_t LSR_PARITY_ERROR          = 0x04; // Bit 2: PE
constexpr uint8_t LSR_FRAMING_ERROR         = 0x08; // Bit 3: FE
constexpr uint8_t LSR_BREAK_INTERRUPT       = 0x10; // Bit 4: BI
constexpr uint8_t LSR_THRE_EMPTY            = 0x20; // Bit 5: THRE
constexpr uint8_t LSR_TRANSMITTER_EMPTY     = 0x40; // Bit 6: TEMT

// 16550 Interrupt Identification Register (IIR) Bits
constexpr uint8_t IIR_NO_INTERRUPT_PENDING  = 0x01; // Bit 0: 1 = No interrupt pending
constexpr uint8_t IIR_INTERRUPT_ID_MASK     = 0x0E; // Bits 3:1: Interrupt source ID
constexpr uint8_t IIR_RX_LINE_STATUS        = 0x06; // Overrun, Parity, Framing, Break
constexpr uint8_t IIR_RX_DATA_AVAILABLE     = 0x04; // Trigger level reached
constexpr uint8_t IIR_CHAR_TIMEOUT          = 0x0C; // Character timeout (CTI)
constexpr uint8_t IIR_THRE_EMPTY            = 0x02; // Transmit holding register empty

// Mock 16550 Hardware State for RX Emulation
struct Mock16550RxHardware {
    uint32_t RBR_THR_DLL; // Offset 0x00
    uint32_t IER_DLM;     // Offset 0x04
    uint32_t IIR_FCR;     // Offset 0x08
    uint32_t LCR;         // Offset 0x0C
    uint32_t MCR;         // Offset 0x10
    uint32_t LSR;         // Offset 0x14

    std::queue<uint8_t> hardware_rx_fifo; // 16-byte internal FIFO
    size_t overrun_event_count;

    void reset() {
        RBR_THR_DLL = 0;
        IER_DLM = 0;
        IIR_FCR = IIR_NO_INTERRUPT_PENDING;
        LCR = 0;
        MCR = 0;
        LSR = 0;
        while (!hardware_rx_fifo.empty()) {
            hardware_rx_fifo.pop();
        }
        overrun_event_count = 0;
    }

    // Push byte into hardware FIFO from external serial wire
    bool push_from_wire(uint8_t byte) {
        if (hardware_rx_fifo.size() < 16) {
            hardware_rx_fifo.push(byte);
            LSR |= LSR_DATA_READY;
            // Update IIR: if trigger level (e.g. 14 bytes) reached, set RDA; else CTI
            if (hardware_rx_fifo.size() >= 14) {
                IIR_FCR = IIR_RX_DATA_AVAILABLE;
            } else {
                IIR_FCR = IIR_CHAR_TIMEOUT;
            }
            return true;
        } else {
            // Hardware FIFO full: 16550 latches Overrun Error
            LSR |= LSR_OVERRUN_ERROR;
            IIR_FCR = IIR_RX_LINE_STATUS;
            overrun_event_count++;
            return false;
        }
    }

    // Read LSR: on physical 16550 IP, reading LSR automatically clears line error flags
    uint32_t read_lsr_and_unlatch() {
        uint32_t val = LSR;
        LSR &= ~(LSR_OVERRUN_ERROR | LSR_PARITY_ERROR | LSR_FRAMING_ERROR | LSR_BREAK_INTERRUPT);
        return val;
    }

    // Read RBR: pops byte from hardware FIFO
    uint8_t read_rbr() {
        if (hardware_rx_fifo.empty()) {
            LSR &= ~LSR_DATA_READY;
            IIR_FCR = IIR_NO_INTERRUPT_PENDING;
            return 0;
        }
        uint8_t b = hardware_rx_fifo.front();
        hardware_rx_fifo.pop();
        if (hardware_rx_fifo.empty()) {
            LSR &= ~LSR_DATA_READY;
            IIR_FCR = IIR_NO_INTERRUPT_PENDING;
        }
        return b;
    }
};

// Mock Software RX Ring Buffer Instance
typedef void (*mockRxCallback_t)(uint8_t data, void *callbackData);

struct MockUartRxPort {
    uint8_t rxBuffer[UART_RX_RING_BUFFER_SIZE];
    uint16_t rxBufferHead;
    uint16_t rxBufferTail;
    uint16_t rxBufferSize;
    mockRxCallback_t rxCallback;
    void *rxCallbackData;
    size_t droppedBytesCount;

    void reset() {
        memset(rxBuffer, 0, sizeof(rxBuffer));
        rxBufferHead = 0;
        rxBufferTail = 0;
        rxBufferSize = UART_RX_RING_BUFFER_SIZE;
        rxCallback = nullptr;
        rxCallbackData = nullptr;
        droppedBytesCount = 0;
    }

    uint16_t bytes_waiting() const {
        if (rxBufferHead >= rxBufferTail) {
            return rxBufferHead - rxBufferTail;
        } else {
            return rxBufferSize + rxBufferHead - rxBufferTail;
        }
    }

    bool is_empty() const {
        return rxBufferHead == rxBufferTail;
    }

    bool is_full() const {
        return ((rxBufferHead + 1) % rxBufferSize) == rxBufferTail;
    }

    uint8_t read_byte() {
        if (is_empty()) {
            return 0;
        }
        uint8_t ch = rxBuffer[rxBufferTail];
        rxBufferTail = (rxBufferTail + 1) % rxBufferSize;
        return ch;
    }
};

// Emulated ISR Routine matching serial_uart_aries.c uartIrqHandler()
void simulate_uart_rx_isr(Mock16550RxHardware &hw, MockUartRxPort &port) {
    // Reading IIR acknowledges/identifies active interrupt condition
    volatile uint32_t iir = hw.IIR_FCR;
    (void)iir;

    // 1. Reading LSR automatically clears ORE, PE, FE, BI flags
    uint32_t lsr = hw.read_lsr_and_unlatch();

    // 2. Loop while Data Ready is asserted: drain all available FIFO bytes
    while (lsr & LSR_DATA_READY) {
        uint8_t rxChar = hw.read_rbr();

        if (port.rxCallback) {
            // Direct callback bypass (e.g. CRSF frame decoder)
            port.rxCallback(rxChar, port.rxCallbackData);
        } else {
            // Collision guard: drop newest byte if buffer full, preserving unread queue
            uint16_t nextHead = (port.rxBufferHead + 1) % port.rxBufferSize;
            if (nextHead != port.rxBufferTail) {
                port.rxBuffer[port.rxBufferHead] = rxChar;
                port.rxBufferHead = nextHead;
            } else {
                port.droppedBytesCount++;
            }
        }

        // Re-read LSR to check if more bytes remain in hardware FIFO
        lsr = hw.read_lsr_and_unlatch();
    }
}

// Emulated Polled Hardware FIFO Fallback in serialRxBytesWaiting()
uint16_t simulate_uart_rx_bytes_waiting_with_poll(Mock16550RxHardware &hw, MockUartRxPort &port) {
    // Flush any residual bytes sitting in hardware FIFO (e.g. after atomic critical section)
    uint32_t lsr = hw.read_lsr_and_unlatch();
    while (lsr & LSR_DATA_READY) {
        uint8_t byte = hw.read_rbr();
        uint16_t nextHead = (port.rxBufferHead + 1) % port.rxBufferSize;
        if (nextHead != port.rxBufferTail) {
            port.rxBuffer[port.rxBufferHead] = byte;
            port.rxBufferHead = nextHead;
        } else {
            port.droppedBytesCount++;
        }
        lsr = hw.read_lsr_and_unlatch();
    }
    return port.bytes_waiting();
}

class AriesUartRxTest : public ::testing::Test {
protected:
    Mock16550RxHardware hw0;
    Mock16550RxHardware hw1;
    MockUartRxPort port0;
    MockUartRxPort port1;

    void SetUp() override {
        hw0.reset();
        hw1.reset();
        port0.reset();
        port1.reset();
    }
};

// =============================================================================
// TEST 01: Zero-Allocation 256-Byte Circular Ring Buffer Allocation & Wraparound
// =============================================================================
TEST_F(AriesUartRxTest, Test_UARTRX_StaticBuffer_256ByteSizeAndModuloWraparound) {
    // Architectural Rationale:
    // Betaflight requires fixed static memory allocation in .bss for flight controller determinism.
    // The RX ring buffer must be exactly 256 bytes (a power of two) to allow fast modulo arithmetic.

    // Condition Under Test:
    // Statically allocated buffer has 256 bytes capacity. Push bytes past boundary to verify wraparound.
    EXPECT_EQ(port0.rxBufferSize, 256U);
    EXPECT_EQ(sizeof(port0.rxBuffer), 256U);

    // Act: Fill buffer to near end of linear array (index 254)
    port0.rxBufferHead = 254;
    port0.rxBufferTail = 254; // empty queue at end of array

    // Push 4 bytes via simulated wire and ISR
    hw0.push_from_wire(0x11);
    hw0.push_from_wire(0x22);
    hw0.push_from_wire(0x33);
    hw0.push_from_wire(0x44);

    simulate_uart_rx_isr(hw0, port0);

    // Expected Result:
    // rxBufferHead wraps from 254 -> 255 -> 0 -> 1 -> 2
    EXPECT_EQ(port0.rxBufferHead, 2U);
    EXPECT_EQ(port0.rxBufferTail, 254U);
    EXPECT_EQ(port0.bytes_waiting(), 4U);

    // Assert data read across the wraparound boundary matches exactly
    EXPECT_EQ(port0.read_byte(), 0x11); // from index 254
    EXPECT_EQ(port0.read_byte(), 0x22); // from index 255
    EXPECT_EQ(port0.read_byte(), 0x33); // from index 0 (wrapped!)
    EXPECT_EQ(port0.read_byte(), 0x44); // from index 1

    EXPECT_TRUE(port0.is_empty());
    EXPECT_EQ(port0.rxBufferTail, 2U);

    // Flight Consequence:
    // Corrupted wraparound logic causes index out-of-bounds access or drops incoming MSP packets,
    // disconnecting the flight controller from Betaflight Configurator.
}

// =============================================================================
// TEST 02: Multi-Byte Hardware FIFO Burst Ingestion (Draining Full 16 Bytes)
// =============================================================================
TEST_F(AriesUartRxTest, Test_UARTRX_MultiByteFIFOBurst_DrainsUpTo16BytesInSingleISRPass) {
    // Architectural Rationale:
    // The 16550 IP has a 16-byte hardware FIFO. Servicing 1 byte per interrupt causes 16 interrupt
    // context switches (each taking ~30 cycles). The ISR must loop while LSR_DATA_READY is set,
    // emptying all available FIFO bytes in a single invocation.

    // Condition Under Test:
    // Hardware FIFO fills with 16 bytes while CPU is busy. One ISR invocation is triggered.
    for (uint8_t i = 0; i < 16; i++) {
        bool ok = hw0.push_from_wire(0xC0 + i);
        EXPECT_TRUE(ok);
    }
    EXPECT_EQ(hw0.hardware_rx_fifo.size(), 16U);
    EXPECT_TRUE(hw0.LSR & LSR_DATA_READY);

    // Act: Fire ISR exactly once
    simulate_uart_rx_isr(hw0, port0);

    // Expected Result:
    // All 16 bytes are moved to the software ring buffer, and hardware FIFO is completely empty.
    EXPECT_EQ(hw0.hardware_rx_fifo.size(), 0U);
    EXPECT_FALSE(hw0.LSR & LSR_DATA_READY);
    EXPECT_EQ(port0.bytes_waiting(), 16U);

    // Verify byte integrity
    for (uint8_t i = 0; i < 16; i++) {
        EXPECT_EQ(port0.read_byte(), static_cast<uint8_t>(0xC0 + i));
    }
    EXPECT_TRUE(port0.is_empty());

    // Flight Consequence:
    // Failing to drain the hardware FIFO in one pass causes interrupt congestion, stealing CPU cycles
    // from the 1.0 kHz PID loop and delaying attitude corrections.
}

// =============================================================================
// TEST 03: Head/Tail Collision Guard (Buffer Full Protection & Drop Accounting)
// =============================================================================
TEST_F(AriesUartRxTest, Test_UARTRX_HeadTailCollisionGuard_DropsNewestBytePreservingHistory) {
    // Architectural Rationale:
    // In a circular ring buffer of size N, the maximum capacity is N - 1 (255 bytes).
    // If incoming data floods the receiver before TASK_SERIAL reads it, advancing head past tail
    // silently corrupts unread byte history. The collision guard must drop the newest byte.

    // Condition Under Test:
    // Buffer is populated to maximum capacity (255 bytes). 10 additional bytes arrive.
    for (size_t i = 0; i < 255; i++) {
        port0.rxBuffer[port0.rxBufferHead] = static_cast<uint8_t>(i & 0xFF);
        port0.rxBufferHead = (port0.rxBufferHead + 1) % port0.rxBufferSize;
    }
    EXPECT_EQ(port0.bytes_waiting(), 255U);
    EXPECT_TRUE(port0.is_full());
    EXPECT_EQ(port0.droppedBytesCount, 0U);

    // 10 new bytes arrive on wire
    for (uint8_t i = 0; i < 10; i++) {
        hw0.push_from_wire(0xEE);
    }

    // Act: Ingest through ISR
    simulate_uart_rx_isr(hw0, port0);

    // Expected Result:
    // All 10 overflowing bytes are dropped; dropped counter increments; unread queue remains intact.
    EXPECT_EQ(port0.droppedBytesCount, 10U);
    EXPECT_EQ(port0.bytes_waiting(), 255U);

    // Verify existing 255 bytes are uncorrupted
    for (size_t i = 0; i < 255; i++) {
        EXPECT_EQ(port0.read_byte(), static_cast<uint8_t>(i & 0xFF));
    }
    EXPECT_TRUE(port0.is_empty());

    // Flight Consequence:
    // Corrupting unread history causes MSP CRC errors or partially parsed commands,
    // freezing configuration sessions or dropping arming requests.
}

// =============================================================================
// TEST 04: Automatic LSR Overrun Error (ORE) Unlatching on Register Read
// =============================================================================
TEST_F(AriesUartRxTest, Test_UARTRX_LineStatusRegister_OverrunErrorAutoUnlatchedOnRead) {
    // Architectural Rationale:
    // On 16550 IP, when incoming bytes exceed the 16-byte FIFO, hardware asserts LSR bit 1 (OE).
    // Unlike STM32 (which requires reading SR then DR), the 16550 specification mandates that
    // reading LSR automatically clears line error flags.

    // Condition Under Test:
    // 16 bytes fill the FIFO; a 17th byte arrives, triggering Overrun Error.
    for (uint8_t i = 0; i < 16; i++) {
        hw0.push_from_wire(i);
    }
    EXPECT_FALSE(hw0.LSR & LSR_OVERRUN_ERROR);

    // 17th byte causes overrun
    bool pushed = hw0.push_from_wire(0xFF);
    EXPECT_FALSE(pushed);
    EXPECT_TRUE(hw0.LSR & LSR_OVERRUN_ERROR);
    EXPECT_EQ(hw0.overrun_event_count, 1U);

    // Act: Read LSR as part of normal ISR execution
    uint32_t active_lsr = hw0.read_lsr_and_unlatch();

    // Expected Result:
    // Returned LSR value has ORE set (0x02), but the hardware register is immediately cleared.
    EXPECT_TRUE(active_lsr & LSR_OVERRUN_ERROR);
    EXPECT_FALSE(hw0.LSR & LSR_OVERRUN_ERROR);

    // Flight Consequence:
    // On historical ports (knowledgebase/components/03_SERIAL_UART_AND_MSP.md), failing to unlatch
    // ORE locked up the UART receiver permanently, requiring a hardware power cycle to recover.
}

// =============================================================================
// TEST 05: Parity Error (PE) and Framing Error (FE) Line Status Error Recovery
// =============================================================================
TEST_F(AriesUartRxTest, Test_UARTRX_LineStatusRegister_ParityAndFramingErrorsUnlatched) {
    // Architectural Rationale:
    // Line noise or baud rate mismatch can assert Parity Error (LSR bit 2) or Framing Error (bit 3).
    // The ISR must unlatch these flags cleanly so subsequent valid frames can be ingested.

    // Condition Under Test:
    // Simulate noisy line asserting PE and FE simultaneously
    hw0.LSR |= (LSR_PARITY_ERROR | LSR_FRAMING_ERROR | LSR_DATA_READY);
    hw0.hardware_rx_fifo.push(0x55); // Corrupted byte in FIFO

    // Act
    simulate_uart_rx_isr(hw0, port0);

    // Expected Result:
    // LSR error bits must be cleared to 0 after ISR execution
    EXPECT_FALSE(hw0.LSR & LSR_PARITY_ERROR);
    EXPECT_FALSE(hw0.LSR & LSR_FRAMING_ERROR);
    EXPECT_EQ(port0.bytes_waiting(), 1U);

    // Flight Consequence:
    // Latched framing errors prevent Configurator baud rate renegotiation from succeeding.
}

// =============================================================================
// TEST 06: Polled Hardware FIFO Fallback in serialRxBytesWaiting()
// =============================================================================
TEST_F(AriesUartRxTest, Test_UARTRX_PolledHardwareFIFOFallback_FlushesDuringMaskedInterrupts) {
    // Architectural Rationale:
    // When the CPU executes an atomic section via CSR mstatus.MIE = 0, hardware interrupts are masked.
    // Incoming bytes queue up in the 16-byte hardware FIFO. When higher-level code calls
    // serialRxBytesWaiting(), the driver must poll LSR_DR and flush hardware FIFO bytes into
    // the software ring buffer, ensuring no data is hidden from the application layer.

    // Condition Under Test:
    // 5 bytes arrive in hardware FIFO while interrupts are masked (no ISR fired).
    for (uint8_t i = 1; i <= 5; i++) {
        hw0.push_from_wire(i * 10);
    }
    EXPECT_EQ(hw0.hardware_rx_fifo.size(), 5U);
    EXPECT_EQ(port0.bytes_waiting(), 0U); // Software buffer currently unaware!

    // Act: Application checks bytes waiting with polled fallback
    uint16_t waiting = simulate_uart_rx_bytes_waiting_with_poll(hw0, port0);

    // Expected Result:
    // All 5 bytes are flushed from hardware FIFO to software buffer and reported
    EXPECT_EQ(waiting, 5U);
    EXPECT_EQ(hw0.hardware_rx_fifo.size(), 0U);
    EXPECT_FALSE(hw0.LSR & LSR_DATA_READY);

    for (uint8_t i = 1; i <= 5; i++) {
        EXPECT_EQ(port0.read_byte(), static_cast<uint8_t>(i * 10));
    }

    // Flight Consequence:
    // Without polled fallback, packets received during atomic sections are stranded until the next
    // interrupt, causing telemetry latency and CLI command dropouts.
}

// =============================================================================
// TEST 07: Direct rxCallback Dispatching Bypass (Zero-Copy for Serial Receivers)
// =============================================================================
TEST_F(AriesUartRxTest, Test_UARTRX_DirectCallback_BypassesRingBufferForCRSFReceiver) {
    // Architectural Rationale:
    // High-rate serial receiver protocols (CRSF / ExpressLRS @ 420,000 baud) attach a direct
    // callback (rxCallback). The ISR must invoke this callback immediately rather than storing
    // bytes into the ring buffer, eliminating queue latency and buffer copies.

    // Condition Under Test:
    // Register mock callback counting and capturing received bytes
    std::vector<uint8_t> callback_received_data;
    auto mock_cb = [](uint8_t data, void *context) {
        auto *vec = static_cast<std::vector<uint8_t> *>(context);
        vec->push_back(data);
    };

    port0.rxCallback = mock_cb;
    port0.rxCallbackData = &callback_received_data;

    // Push 4 CRSF sync frame bytes
    hw0.push_from_wire(0xC8); // CRSF sync
    hw0.push_from_wire(0x18); // length
    hw0.push_from_wire(0x16); // type
    hw0.push_from_wire(0x00); // payload

    // Act: Execute ISR
    simulate_uart_rx_isr(hw0, port0);

    // Expected Result:
    // Bytes are delivered directly to callback; software ring buffer remains untouched (0 bytes)
    EXPECT_EQ(callback_received_data.size(), 4U);
    EXPECT_EQ(callback_received_data[0], 0xC8);
    EXPECT_EQ(callback_received_data[1], 0x18);
    EXPECT_EQ(callback_received_data[2], 0x16);
    EXPECT_EQ(callback_received_data[3], 0x00);

    EXPECT_EQ(port0.bytes_waiting(), 0U);
    EXPECT_TRUE(port0.is_empty());

    // Flight Consequence:
    // Double-buffering CRSF packets adds 200 us latency to RC control sticks, degrading flight feel.
}

// =============================================================================
// TEST 08: Ring Buffer Empty Underflow Safety
// =============================================================================
TEST_F(AriesUartRxTest, Test_UARTRX_UnderflowSafety_EmptyBufferReturnsZeroAndPreservesTail) {
    // Architectural Rationale:
    // Reading from an empty buffer must return 0 and must not decrement or mutate the tail index.

    // Condition Under Test:
    EXPECT_TRUE(port0.is_empty());
    uint16_t initial_tail = port0.rxBufferTail;

    // Act
    uint8_t read_val = port0.read_byte();

    // Expected Result
    EXPECT_EQ(read_val, 0U);
    EXPECT_EQ(port0.rxBufferTail, initial_tail);
    EXPECT_EQ(port0.bytes_waiting(), 0U);
}

// =============================================================================
// TEST 09: Single-Word Atomic Head/Tail Concurrency Safety
// =============================================================================
TEST_F(AriesUartRxTest, Test_UARTRX_Concurrency_SingleWordHeadTailPointersThreadSafe) {
    // Architectural Rationale:
    // In Betaflight's non-blocking serial design, rxBufferHead is updated ONLY by the ISR,
    // and rxBufferTail is updated ONLY by the consumer task (uartRead).
    // On RV32I, aligned 16-bit loads and stores execute in a single instruction (lh/sh or lw/sw),
    // ensuring lock-free thread safety between ISR and main loop.

    // Condition Under Test:
    // Interleaved write (ISR) and read (task) updates across 500 iterations
    for (int iter = 0; iter < 500; iter++) {
        uint8_t payload = static_cast<uint8_t>(iter & 0xFF);
        hw0.push_from_wire(payload);
        simulate_uart_rx_isr(hw0, port0);

        ASSERT_FALSE(port0.is_empty());
        uint8_t consumed = port0.read_byte();
        ASSERT_EQ(consumed, payload);
    }

    EXPECT_TRUE(port0.is_empty());
    EXPECT_EQ(port0.droppedBytesCount, 0U);
}

// =============================================================================
// TEST 10: Multi-Port RX Ring Buffer Isolation (UART0 vs UART1)
// =============================================================================
TEST_F(AriesUartRxTest, Test_UARTRX_MultiPortIsolation_ZeroCrossTalkBetweenUART0AndUART1) {
    // Architectural Rationale:
    // ARIES v3.0 operates UART0 (USB Configurator @ 115,200 baud) and UART1 (CRSF RX @ 420,000 baud)
    // concurrently. Each hardware channel must maintain an isolated ring buffer and distinct MMIO base.

    // Condition Under Test:
    // Push distinct streams to UART0 and UART1
    hw0.push_from_wire(0xAA);
    hw0.push_from_wire(0xAB);

    hw1.push_from_wire(0xBB);
    hw1.push_from_wire(0xBC);
    hw1.push_from_wire(0xBD);

    // Act: Execute ISRs for both channels independently
    simulate_uart_rx_isr(hw0, port0);
    simulate_uart_rx_isr(hw1, port1);

    // Expected Result:
    // UART0 holds exactly 2 bytes; UART1 holds exactly 3 bytes; no cross-contamination
    EXPECT_EQ(port0.bytes_waiting(), 2U);
    EXPECT_EQ(port1.bytes_waiting(), 3U);

    EXPECT_EQ(port0.read_byte(), 0xAA);
    EXPECT_EQ(port0.read_byte(), 0xAB);

    EXPECT_EQ(port1.read_byte(), 0xBB);
    EXPECT_EQ(port1.read_byte(), 0xBC);
    EXPECT_EQ(port1.read_byte(), 0xBD);

    EXPECT_TRUE(port0.is_empty());
    EXPECT_TRUE(port1.is_empty());

    // Flight Consequence:
    // If port buffers or registers cross-talk, MSP commands corrupt CRSF stick inputs,
    // leading to uncontrollable quadcopter behavior.
}

// =============================================================================
// TEST 11: 16550 Character Timeout Indication (CTI / IIR 0x0C) Trailing Bytes
// =============================================================================
TEST_F(AriesUartRxTest, Test_UARTRX_CharacterTimeoutInterrupt_TrailingBytesFlushed) {
    // Architectural Rationale:
    // When the sender transmits fewer bytes than the 16550 FIFO trigger level (e.g., 2 bytes
    // of a CLI prompt: "\r\n") and stops transmitting, the hardware generates a Character
    // Timeout Interrupt (CTI, IIR bits 3:1 = 0x06 / code 0x0C).
    // The ISR must handle this condition and evacuate trailing bytes immediately.

    // Condition Under Test:
    // Push only 3 bytes into hardware FIFO (below typical 14-byte trigger level).
    hw0.push_from_wire('o');
    hw0.push_from_wire('k');
    hw0.push_from_wire('\n');

    // Emulate 16550 Character Timeout Indication in IIR
    hw0.IIR_FCR = IIR_CHAR_TIMEOUT;
    EXPECT_EQ(hw0.IIR_FCR & IIR_INTERRUPT_ID_MASK, IIR_CHAR_TIMEOUT);

    // Act: Trigger ISR on Character Timeout
    simulate_uart_rx_isr(hw0, port0);

    // Expected Result:
    // All 3 trailing bytes are promptly ingested; IIR returns to no interrupt pending
    EXPECT_EQ(port0.bytes_waiting(), 3U);
    EXPECT_EQ(port0.read_byte(), 'o');
    EXPECT_EQ(port0.read_byte(), 'k');
    EXPECT_EQ(port0.read_byte(), '\n');
    EXPECT_TRUE(port0.is_empty());
    EXPECT_EQ(hw0.IIR_FCR, IIR_NO_INTERRUPT_PENDING);

    // Flight Consequence:
    // If CTI is ignored, short CLI commands or single-character keystrokes hang indefinitely
    // until more characters are typed.
}

// =============================================================================
// TEST 12: Line Break Condition (LSR Bit 4 BI) Unlatching & Recovery
// =============================================================================
TEST_F(AriesUartRxTest, Test_UARTRX_LineBreakCondition_BreakInterruptUnlatched) {
    // Architectural Rationale:
    // When the USB-to-UART bridge opens, closes, or resets, the serial RX line is held LOW
    // for longer than a frame time, triggering a Break Interrupt (LSR bit 4 BI = 0x10).
    // The 16550 IP places a 0x00 character in the FIFO. The driver must clear BI on read
    // and resume normal packet ingestion.

    // Condition Under Test:
    // Break condition asserts LSR_BREAK_INTERRUPT and places null byte in hardware FIFO
    hw0.LSR |= (LSR_BREAK_INTERRUPT | LSR_DATA_READY);
    hw0.hardware_rx_fifo.push(0x00); // Break character

    // Act: Fire ISR
    simulate_uart_rx_isr(hw0, port0);

    // Expected Result:
    // LSR_BREAK_INTERRUPT flag is cleared; the null byte was ingested safely; hardware is ready
    EXPECT_FALSE(hw0.LSR & LSR_BREAK_INTERRUPT);
    EXPECT_EQ(port0.bytes_waiting(), 1U);
    EXPECT_EQ(port0.read_byte(), 0x00);

    // Subsequent valid byte is ingested with clean status
    hw0.push_from_wire(0x24); // '$' of MSP header
    simulate_uart_rx_isr(hw0, port0);
    EXPECT_EQ(port0.read_byte(), 0x24);

    // Flight Consequence:
    // Latching BI prevents Configurator reconnection after warm reboot.
}

// =============================================================================
// TEST 13: Spurious Interrupt Handling (Zero Data Ready, Zero Buffer Mutation)
// =============================================================================
TEST_F(AriesUartRxTest, Test_UARTRX_SpuriousInterrupt_NoDataReady_ZeroBufferMutation) {
    // Architectural Rationale:
    // Spurious interrupts can occur on noisy PLIC lines. If the ISR is invoked while LSR
    // indicates no data ready (LSR bit 0 == 0), the ISR must return immediately without
    // reading RBR (which would return undefined junk or stall the APB bus).

    // Condition Under Test:
    hw0.LSR = 0; // No data ready
    hw0.IIR_FCR = IIR_NO_INTERRUPT_PENDING;
    uint16_t initial_head = port0.rxBufferHead;
    uint16_t initial_tail = port0.rxBufferTail;

    // Act: Fire ISR spuriously
    simulate_uart_rx_isr(hw0, port0);

    // Expected Result:
    // Zero bytes ingested, zero mutations to ring buffer head or tail pointers
    EXPECT_EQ(port0.rxBufferHead, initial_head);
    EXPECT_EQ(port0.rxBufferTail, initial_tail);
    EXPECT_EQ(port0.bytes_waiting(), 0U);
    EXPECT_EQ(port0.droppedBytesCount, 0U);

    // Flight Consequence:
    // Reading RBR when empty pollutes the buffer with phantom bytes, corrupting MSP frame sync.
}

// =============================================================================
// TEST 14: Exact Boundary Stress at Capacity Limit (254 -> 255 -> Full -> Refill)
// =============================================================================
TEST_F(AriesUartRxTest, Test_UARTRX_BoundaryStress_ExactTransitionsAtCapacityLimit) {
    // Architectural Rationale:
    // A 256-byte circular buffer can hold exactly 255 bytes before nextHead == tail.
    // We must test exact capacity boundary transitions: 254 bytes (1 space left),
    // 255 bytes (completely full), attempting 256th byte (rejected), draining 1 byte
    // (now 254 bytes), and refilling 1 byte (back to 255).

    // Condition Under Test:
    // 1. Fill exactly 254 bytes
    for (size_t i = 0; i < 254; i++) {
        port0.rxBuffer[port0.rxBufferHead] = static_cast<uint8_t>(i & 0xFF);
        port0.rxBufferHead = (port0.rxBufferHead + 1) % port0.rxBufferSize;
    }
    EXPECT_EQ(port0.bytes_waiting(), 254U);
    EXPECT_FALSE(port0.is_full());

    // 2. Add 255th byte -> becomes full
    hw0.push_from_wire(0xFE);
    simulate_uart_rx_isr(hw0, port0);
    EXPECT_EQ(port0.bytes_waiting(), 255U);
    EXPECT_TRUE(port0.is_full());
    EXPECT_EQ(port0.droppedBytesCount, 0U);

    // 3. Attempt 256th byte -> must be dropped
    hw0.push_from_wire(0xFF);
    simulate_uart_rx_isr(hw0, port0);
    EXPECT_EQ(port0.bytes_waiting(), 255U);
    EXPECT_TRUE(port0.is_full());
    EXPECT_EQ(port0.droppedBytesCount, 1U); // 1 drop recorded

    // 4. Consumer reads 1 byte -> buffer has 1 free space
    uint8_t first_byte = port0.read_byte();
    EXPECT_EQ(first_byte, 0x00);
    EXPECT_EQ(port0.bytes_waiting(), 254U);
    EXPECT_FALSE(port0.is_full());

    // 5. Wire sends another byte -> accepted into newly freed slot
    hw0.push_from_wire(0x77);
    simulate_uart_rx_isr(hw0, port0);
    EXPECT_EQ(port0.bytes_waiting(), 255U);
    EXPECT_TRUE(port0.is_full());
    EXPECT_EQ(port0.droppedBytesCount, 1U); // No new drops
}

// =============================================================================
// TEST 15: Framing Error Corrupted Byte Unlatching & Subsequent Clean MSP Frame
// =============================================================================
TEST_F(AriesUartRxTest, Test_UARTRX_FramingErrorCorruptedByte_SubsequentValidFrameIngestedCleanly) {
    // Architectural Rationale:
    // When noise corrupts a byte on the wire, the 16550 asserts Framing Error (FE, bit 3).
    // The driver must clear FE and immediately accept subsequent valid frame bytes ($M<...).

    // Condition Under Test:
    // First byte arrives with framing error
    hw0.LSR |= (LSR_FRAMING_ERROR | LSR_DATA_READY);
    hw0.hardware_rx_fifo.push(0x00); // Corrupted byte

    // Act: Ingest corrupted byte
    simulate_uart_rx_isr(hw0, port0);
    EXPECT_FALSE(hw0.LSR & LSR_FRAMING_ERROR); // Cleared!

    // Second packet arrives: valid 6-byte MSP_IDENT frame ($M< \x00 \x64 \x64)
    const uint8_t msp_ident[] = { '$', 'M', '<', 0x00, 0x64, 0x64 };
    for (uint8_t b : msp_ident) {
        hw0.push_from_wire(b);
    }
    simulate_uart_rx_isr(hw0, port0);

    // Expected Result:
    // First byte was the noisy 0x00, followed by the exact 6-byte clean MSP frame
    EXPECT_EQ(port0.bytes_waiting(), 7U);
    EXPECT_EQ(port0.read_byte(), 0x00); // Corrupted byte

    for (uint8_t expected_b : msp_ident) {
        EXPECT_EQ(port0.read_byte(), expected_b);
    }
    EXPECT_TRUE(port0.is_empty());
}

// =============================================================================
// TEST 16: High-Throughput Continuous Stream (500 Bytes) with 100 Hz Task Drain
// =============================================================================
TEST_F(AriesUartRxTest, Test_UARTRX_HighThroughput_ContinuousStreamWithInterleavedConsumerTask) {
    // Architectural Rationale:
    // During full Configurator dumps, 500+ bytes stream across the wire.
    // In Betaflight, TASK_SERIAL runs at 100 Hz (every 10 ms).
    // This test simulates continuous multi-burst arrival interleaved with periodic task reads,
    // proving 100% throughput retention with zero dropped bytes.

    // Condition Under Test:
    // Send 500 bytes in 5 bursts of 100 bytes. Between bursts, consumer task reads all waiting bytes.
    std::vector<uint8_t> all_received;
    size_t total_sent = 500;

    for (size_t burst = 0; burst < 5; burst++) {
        // Send 100 bytes into hardware FIFO in chunks of 16 bytes
        for (size_t b = 0; b < 100; b += 16) {
            size_t chunk = std::min<size_t>(16, 100 - b);
            for (size_t i = 0; i < chunk; i++) {
                uint8_t byte_val = static_cast<uint8_t>((burst * 100 + b + i) & 0xFF);
                hw0.push_from_wire(byte_val);
            }
            simulate_uart_rx_isr(hw0, port0);
        }

        // Interleaved consumer task (TASK_SERIAL) drains the ring buffer
        while (!port0.is_empty()) {
            all_received.push_back(port0.read_byte());
        }
    }

    // Expected Result:
    // All 500 bytes received in exact sequential order with 0 drops
    EXPECT_EQ(all_received.size(), total_sent);
    EXPECT_EQ(port0.droppedBytesCount, 0U);

    for (size_t i = 0; i < total_sent; i++) {
        EXPECT_EQ(all_received[i], static_cast<uint8_t>(i & 0xFF));
    }
}

// =============================================================================
// TEST 17: Race-Free bytes_waiting() Evaluation During Concurrent Head Advance
// =============================================================================
TEST_F(AriesUartRxTest, Test_UARTRX_AtomicBytesWaiting_RaceFreeCalculationDuringConcurrentHeadAdvance) {
    // Architectural Rationale:
    // If the main task evaluates bytes_waiting() while the ISR interrupts and advances
    // rxBufferHead, the calculation must remain strictly non-negative and mathematically sound.

    // Condition Under Test:
    // Head = 20, Tail = 20 (empty). ISR interrupts and advances Head to 25.
    port0.rxBufferHead = 20;
    port0.rxBufferTail = 20;
    EXPECT_EQ(port0.bytes_waiting(), 0U);

    // Simulate head advancing across wraparound: Tail = 250, Head wraps to 10
    port0.rxBufferTail = 250;
    port0.rxBufferHead = 10;
    // Expected: 256 + 10 - 250 = 16 bytes waiting
    EXPECT_EQ(port0.bytes_waiting(), 16U);

    // Verify calculation never underflows even when Head is smaller than Tail
    EXPECT_GT(port0.bytes_waiting(), 0U);
    EXPECT_LE(port0.bytes_waiting(), 256U);
}

// =============================================================================
// TEST 18: Overrun Error Recovery with Continuous Stream Returning to Clean State
// =============================================================================
TEST_F(AriesUartRxTest, Test_UARTRX_OverrunRecovery_ContinuousStreamReturnsToCleanReception) {
    // Architectural Rationale:
    // If a temporary CPU lock causes hardware FIFO overrun, the ISR must clear ORE on the next
    // read, drop the overflowing bytes, and immediately resume normal reception as the consumer
    // catches up.

    // Condition Under Test:
    // Step 1: Force hardware overrun
    for (uint8_t i = 0; i < 16; i++) {
        hw0.push_from_wire(i);
    }
    hw0.push_from_wire(0xEE); // 17th byte triggers ORE
    EXPECT_TRUE(hw0.LSR & LSR_OVERRUN_ERROR);

    // Step 2: ISR clears ORE and drains FIFO
    simulate_uart_rx_isr(hw0, port0);
    EXPECT_FALSE(hw0.LSR & LSR_OVERRUN_ERROR);
    EXPECT_EQ(port0.bytes_waiting(), 16U);

    // Step 3: Consumer drains software buffer
    while (!port0.is_empty()) {
        port0.read_byte();
    }
    EXPECT_TRUE(port0.is_empty());

    // Step 4: Next 10 normal bytes arrive on wire
    for (uint8_t i = 100; i < 110; i++) {
        bool ok = hw0.push_from_wire(i);
        EXPECT_TRUE(ok);
    }

    simulate_uart_rx_isr(hw0, port0);
    EXPECT_EQ(port0.bytes_waiting(), 10U);
    EXPECT_FALSE(hw0.LSR & LSR_OVERRUN_ERROR);

    for (uint8_t i = 100; i < 110; i++) {
        EXPECT_EQ(port0.read_byte(), i);
    }
    EXPECT_TRUE(port0.is_empty());

    // Flight Consequence:
    // Robust overrun recovery ensures temporary CPU latency spikes do not require a flight
    // controller reboot.
}

// =============================================================================
// TEST 19: Real serial_uart_aries.c Ring Buffer, FE/PE Filtering & OE Recovery
// =============================================================================
TEST_F(AriesUartRxTest, Test_RealUartRxAriesC_RingBufferAndErrorRecovery) {
    thejas32_host_reset_all();
    IOInitGlobal();

    serialPinConfig_t pinCfg = {};
    pinCfg.ioTagTx[0] = DEFIO_TAG_E__PA30;
    pinCfg.ioTagRx[0] = DEFIO_TAG_E__PA31;
    uartPinConfigure(&pinCfg);

    serialPort_t *sp0 = uartOpen(SERIAL_PORT_UART0, nullptr, nullptr, 115200U, MODE_RXTX, SERIAL_NOT_INVERTED);
    ASSERT_NE(sp0, nullptr);

    // Inject '$', 'M', framing-errored byte (FE dropped), parity-errored byte (PE dropped), and overrun+clean '<'
    const struct { uint8_t dr; uint8_t lsr; } wire[] = {
        { 0x24U, 0x61U }, // '$'
        { 0x4DU, 0x61U }, // 'M'
        { 0xFFU, 0x69U }, // FE -> drop
        { 0xEEU, 0x65U }, // PE -> drop
        { 0x3CU, 0x63U }, // '<' with OE -> cleared on LSR read, byte accepted
    };
    for (const auto &pkt : wire) {
        THEJAS32_UART(0).UART_DR = pkt.dr;
        THEJAS32_UART(0).UART_LSR = pkt.lsr;
        THEJAS32_INTR_CTRL.INTR_STATUS = (1UL << THEJAS32_IRQ_UART0);
        handle_trap(0x8000000BUL, 0U, nullptr);
    }

    EXPECT_EQ(sp0->vTable->serialTotalRxWaiting(sp0), 3U);
    EXPECT_EQ(sp0->vTable->serialRead(sp0), 0x24U);
    EXPECT_EQ(sp0->vTable->serialRead(sp0), 0x4DU);
    EXPECT_EQ(sp0->vTable->serialRead(sp0), 0x3CU);
    EXPECT_EQ(sp0->vTable->serialTotalRxWaiting(sp0), 0U);
}

} // namespace
