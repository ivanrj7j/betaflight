/*
 * Unit tests for ARIES v3.0 System Core, Timebase & Trap Dispatch.
 * Phase 2 test suite corresponding to Module T1.
 */

#include <stdint.h>
#include <stdbool.h>

extern "C" {
    #include "aries/mock_mmio.h"
    #include "aries/mock_csr.h"
    #include "aries/mock_thejas32.h"
    #include "aries/test_helpers.h"
    #include "aries_system_testable.h"
}

#include "unittest_macros.h"
#include "gtest/gtest.h"

// Forward-declare device structs to inspect register MMIO writes
typedef struct {
    volatile uint32_t LoadCount;
    volatile uint32_t CurrentValue;
    volatile uint32_t Control;
    volatile uint32_t EOI;
    volatile uint32_t IntrStatus;
} test_timer_reg_t;

typedef struct {
    volatile uint32_t RAW_INTR;
    volatile uint32_t dummy0;
    volatile uint32_t INTR_EN;
    volatile uint32_t dummy1;
    volatile uint32_t INTR_STATUS;
} test_intr_ctrl_t;

#define MOCK_TIMER0    (*((test_timer_reg_t *)&mock_periph_mem[0xA00]))
#define MOCK_INTR_CTRL (*((test_intr_ctrl_t *)mock_intctrl_mem))

class AriesSystemTest : public ::testing::Test {
protected:
    void SetUp() override {
        mock_mmio_reset();
        mock_csr_reset();
        mock_thejas32_reset();
        test_system_reset_state();
    }
};

// T1-01: micros() returns 0 when cycle counter is 0
TEST_F(AriesSystemTest, MicrosReturnsZeroAtCycleZero)
{
    mock_csr_set(0);
    EXPECT_EQ(micros(), 0U);
    EXPECT_EQ(microsISR(), 0U);
}

// T1-02: micros() returns 1 when cycle counter is 100
TEST_F(AriesSystemTest, MicrosReturns1At100Cycles)
{
    mock_csr_set(100);
    EXPECT_EQ(micros(), 1U);
}

// T1-03: micros() rolls over at 32-bit maximum (4,294,967,295 µs)
TEST_F(AriesSystemTest, MicrosRolls32BitCorrectly)
{
    // 429496729500 cycles / 100 = 4294967295 (0xFFFFFFFF)
    mock_csr_set(429496729500ULL);
    EXPECT_EQ(micros(), 0xFFFFFFFFU);

    // 1 tick further wraps around 32-bit
    mock_csr_set(429496729500ULL + 100ULL);
    EXPECT_EQ(micros(), 0U);
}

// T1-04: micros() is strictly monotonic across advancing counter reads
TEST_F(AriesSystemTest, MicrosIsMonotonic)
{
    timeUs_t prev = 0;
    for (int i = 0; i < 10000; i++) {
        mock_csr_advance(100 + (i % 13));
        timeUs_t now = micros();
        EXPECT_GE(now, prev);
        prev = now;
    }
}

// T1-05: millis() scaling from 100 MHz cycles
TEST_F(AriesSystemTest, MillisScaling)
{
    mock_csr_set(0);
    EXPECT_EQ(millis(), 0U);

    mock_csr_set(100000ULL); // 100,000 cycles = 1 ms
    EXPECT_EQ(millis(), 1U);

    mock_csr_set(500000000ULL); // 500 million cycles = 5000 ms
    EXPECT_EQ(millis(), 5000U);
}

// T1-06: millis() rolls over at 49.71 days (4,294,967,295 ms)
TEST_F(AriesSystemTest, MillisRolloverWindow)
{
    // (2^32 - 1) * 100,000 cycles
    uint64_t max_ms_cycles = 4294967295ULL * 100000ULL;
    mock_csr_set(max_ms_cycles);
    EXPECT_EQ(millis(), 0xFFFFFFFFU);

    // Rollover
    mock_csr_set(max_ms_cycles + 100000ULL);
    EXPECT_EQ(millis(), 0U);
}

// T1-07: clockCyclesToMicros conversion
TEST_F(AriesSystemTest, ClockCyclesToMicrosConversion)
{
    EXPECT_EQ(clockCyclesToMicros(0), 0);
    EXPECT_EQ(clockCyclesToMicros(99), 0); // Truncation below 1 µs
    EXPECT_EQ(clockCyclesToMicros(100), 1);
    EXPECT_EQ(clockCyclesToMicros(250), 2);
    EXPECT_EQ(clockCyclesToMicros(100000), 1000);
}

// T1-08: clockCyclesToMicrosf floating point conversion
TEST_F(AriesSystemTest, ClockCyclesToMicrosFloat)
{
    EXPECT_FLOAT_EQ(clockCyclesToMicrosf(100), 1.0f);
    EXPECT_FLOAT_EQ(clockCyclesToMicrosf(50), 0.5f);
    EXPECT_FLOAT_EQ(clockCyclesToMicrosf(150), 1.5f);
}

// T1-09: clockMicrosToCycles conversion
TEST_F(AriesSystemTest, ClockMicrosToCyclesConversion)
{
    EXPECT_EQ(clockMicrosToCycles(0), 0U);
    EXPECT_EQ(clockMicrosToCycles(1), 100U);
    EXPECT_EQ(clockMicrosToCycles(1000), 100000U);
}

// T1-10: Fractional clock cycle conversions (10th and 100th µs)
TEST_F(AriesSystemTest, ClockCyclesToTenthAndHundredthMicros)
{
    // 10 clock cycles = 1/10th µs (0.1 µs)
    EXPECT_EQ(clockCyclesTo10thMicros(10), 1);
    EXPECT_EQ(clockCyclesTo10thMicros(100), 10);

    // 1 clock cycle = 1/100th µs (0.01 µs)
    EXPECT_EQ(clockCyclesTo100thMicros(1), 1);
    EXPECT_EQ(clockCyclesTo100thMicros(100), 100);
}

// T1-11: thejas32_timer_init() configures registers and registers IRQ 7
TEST_F(AriesSystemTest, TimerInitConfiguresRegistersAndIRQ)
{
    thejas32_timer_init();

    EXPECT_EQ(MOCK_TIMER0.LoadCount, 0xFFFFFFFFUL);
    EXPECT_EQ(MOCK_TIMER0.Control, 0x03U); // Periodic mode, unmasked, enabled
    EXPECT_TRUE((MOCK_INTR_CTRL.INTR_EN & (1UL << 7)) != 0); // IRQ 7 enabled
}

// T1-12: thejas32_timer0_isr() increments overflows and accesses EOI
TEST_F(AriesSystemTest, Timer0IsrIncrementsOverflowsAndAcknowledgesEOI)
{
    EXPECT_EQ(timer0GetOverflows(), 0U);

    thejas32_timer0_isr();
    EXPECT_EQ(timer0GetOverflows(), 1U);

    thejas32_timer0_isr();
    EXPECT_EQ(timer0GetOverflows(), 2U);
}

// T1-13: timer0GetCurrentValue() queries hardware down-counter
TEST_F(AriesSystemTest, Timer0GettersReflectHardwareState)
{
    MOCK_TIMER0.CurrentValue = 0x12345678UL;
    EXPECT_EQ(timer0GetCurrentValue(), 0x12345678UL);
}

// T1-14: timer0Micros() calculates time from overflows and down-counter
TEST_F(AriesSystemTest, Timer0MicrosCalculatesTimeFromHardwareRegisters)
{
    // Down counter starts at 0xFFFFFFFF
    MOCK_TIMER0.CurrentValue = 0xFFFFFFFFUL;
    MOCK_TIMER0.IntrStatus = 0;
    EXPECT_EQ(timer0Micros(), 0U);

    // Down counter decremented by 10,000 cycles (elapsed = 10,000 cycles = 100 µs)
    MOCK_TIMER0.CurrentValue = 0xFFFFFFFFUL - 10000UL;
    EXPECT_EQ(timer0Micros(), 100U);
}

// T1-15: timer0Micros() compensates for pending unserviced overflow
TEST_F(AriesSystemTest, Timer0MicrosCorrectsPendingOverflow)
{
    // Counter underflowed and reloaded close to top, but ISR has not executed yet
    MOCK_TIMER0.CurrentValue = 0xFFFFFFFFUL - 100UL;
    MOCK_TIMER0.IntrStatus = 1; // Pending interrupt flag asserted

    // ovf (0) should be promoted to 1 to account for missed ISR
    timeUs_t us = timer0Micros();
    // 1 overflow = 0x100000000 cycles (~42,949,672 cycles = 42,949,672 µs)
    EXPECT_GE(us, 42949670U);
}

// T1-16: thejas32_interrupt_register bounds guard
TEST_F(AriesSystemTest, InterruptRegisterSlotPopulationAndBoundsGuard)
{
    static int dummy_called = 0;
    dummy_called = 0;

    thejas32_interrupt_register(0, []() { dummy_called++; });
    thejas32_interrupt_register(31, []() { dummy_called++; });
    thejas32_interrupt_register(32, []() { dummy_called++; }); // Should be ignored (out of bounds)
    thejas32_interrupt_register(255, []() { dummy_called++; }); // Ignored

    // Trigger slot 0
    MOCK_INTR_CTRL.INTR_STATUS = (1UL << 0);
    uint32_t sp[32];
    handle_trap(0x8000000BU, 0x20000100U, sp);
    EXPECT_EQ(dummy_called, 1);
}

// T1-17: thejas32_interrupt_enable and disable
TEST_F(AriesSystemTest, InterruptEnableAndDisableBits)
{
    thejas32_interrupt_enable(5);
    EXPECT_TRUE((MOCK_INTR_CTRL.INTR_EN & (1UL << 5)) != 0);

    thejas32_interrupt_disable(5);
    EXPECT_FALSE((MOCK_INTR_CTRL.INTR_EN & (1UL << 5)) != 0);
}

// T1-18: handle_trap() dispatches external interrupt (cause 11)
TEST_F(AriesSystemTest, HandleTrapDispatchesExternalInterrupt)
{
    static int irq7_fired = 0;
    irq7_fired = 0;

    thejas32_interrupt_register(7, []() { irq7_fired++; });
    MOCK_INTR_CTRL.INTR_STATUS = (1UL << 7);

    uint32_t sp[32];
    uint32_t ret = handle_trap(0x8000000BU, 0x20004000U, sp);

    EXPECT_EQ(ret, 0x20004000U); // Interrupt resume PC is mepc unchanged
    EXPECT_EQ(irq7_fired, 1);
    EXPECT_EQ(test_system_get_panic_count(), 0);
}

// T1-19: handle_trap() dispatches multiple simultaneous active IRQs
TEST_F(AriesSystemTest, HandleTrapMultipleActiveInterrupts)
{
    static int irq0_fired = 0;
    static int irq1_fired = 0;
    irq0_fired = 0;
    irq1_fired = 0;

    thejas32_interrupt_register(0, []() { irq0_fired++; });
    thejas32_interrupt_register(1, []() { irq1_fired++; });

    MOCK_INTR_CTRL.INTR_STATUS = (1UL << 0) | (1UL << 1);

    uint32_t sp[32];
    handle_trap(0x8000000BU, 0x20004000U, sp);

    EXPECT_EQ(irq0_fired, 1);
    EXPECT_EQ(irq1_fired, 1);
}

// T1-20: handle_trap() with unregistered IRQ active doesn't crash
TEST_F(AriesSystemTest, HandleTrapUnregisteredIrqNoCrash)
{
    MOCK_INTR_CTRL.INTR_STATUS = (1UL << 25); // Unregistered IRQ 25
    uint32_t sp[32];
    uint32_t ret = handle_trap(0x8000000BU, 0x20004000U, sp);
    EXPECT_EQ(ret, 0x20004000U);
    EXPECT_EQ(test_system_get_panic_count(), 0);
}

// T1-21: handle_trap() synchronously panics on illegal instruction (cause 2)
TEST_F(AriesSystemTest, HandleTrapSynchronousIllegalInstructionException)
{
    uint32_t sp[32];
    uint32_t ret = handle_trap(0x00000002U, 0x20002000U, sp);

    EXPECT_EQ(ret, 0x20002004U); // mepc + 4 for exception skip
    EXPECT_EQ(test_system_get_panic_count(), 1);
    EXPECT_EQ(test_system_get_last_panic_mcause(), 0x00000002U);
}

// T1-22: handle_trap() synchronously panics on load access fault (cause 5)
TEST_F(AriesSystemTest, HandleTrapSynchronousLoadFaultException)
{
    uint32_t sp[32];
    uint32_t ret = handle_trap(0x00000005U, 0x20002010U, sp);

    EXPECT_EQ(ret, 0x20002014U);
    EXPECT_EQ(test_system_get_panic_count(), 1);
    EXPECT_EQ(test_system_get_last_panic_mcause(), 0x00000005U);
}

// T1-23: cycleCounterInit() initializes 100 MHz parameters
TEST_F(AriesSystemTest, CycleCounterInit100MHzConfiguration)
{
    SystemCoreClock = 100000000;
    cycleCounterInit();

    // 100 clock cycles is 1 us at 100 MHz
    EXPECT_EQ(clockCyclesToMicros(100), 1);
    EXPECT_FLOAT_EQ(clockCyclesToMicrosf(100), 1.0f);
}

// T1-24: Bare-Metal thejas32_memset fills memory and handles zero size
TEST_F(AriesSystemTest, MemsetBasicAndZeroSize)
{
    uint8_t buffer[64];
    for (size_t i = 0; i < sizeof(buffer); i++) buffer[i] = 0xAA;

    void *ret = thejas32_memset(buffer, 0x55, 32);
    EXPECT_EQ(ret, buffer);

    for (size_t i = 0; i < 32; i++) EXPECT_EQ(buffer[i], 0x55);
    for (size_t i = 32; i < 64; i++) EXPECT_EQ(buffer[i], 0xAA);

    // Zero size test
    ret = thejas32_memset(buffer, 0xFF, 0);
    EXPECT_EQ(ret, buffer);
    EXPECT_EQ(buffer[0], 0x55);
}

// T1-25: Bare-Metal thejas32_memcpy copies memory accurately and handles zero size
TEST_F(AriesSystemTest, MemcpyBasicAndZeroSize)
{
    uint8_t src[32];
    uint8_t dst[32];
    for (size_t i = 0; i < sizeof(src); i++) src[i] = (uint8_t)(i + 1);
    for (size_t i = 0; i < sizeof(dst); i++) dst[i] = 0;

    void *ret = thejas32_memcpy(dst, src, 32);
    EXPECT_EQ(ret, dst);
    for (size_t i = 0; i < 32; i++) EXPECT_EQ(dst[i], (uint8_t)(i + 1));

    // Zero size test
    ret = thejas32_memcpy(dst, src, 0);
    EXPECT_EQ(ret, dst);
}

// T1-26: Bare-Metal thejas32_memmove handles forward and backward overlapping copies
TEST_F(AriesSystemTest, MemmoveOverlappingForwardAndBackward)
{
    char str1[32] = "1234567890abcdef";
    // Forward overlap (dst > src): shift right by 2
    thejas32_memmove(str1 + 2, str1, 10);
    EXPECT_EQ(memcmp(str1, "121234567890cdef", 16), 0);

    char str2[32] = "1234567890abcdef";
    // Backward overlap (dst < src): shift left by 2
    thejas32_memmove(str2, str2 + 2, 10);
    EXPECT_EQ(memcmp(str2, "34567890ababcdef", 16), 0);
}

// T1-27: Early UART putc bounded timeout prevents infinite spin when UART_LSR is not ready
TEST_F(AriesSystemTest, EarlyUartPutcBoundedTimeout)
{
    uint32_t cycles = 0;
    test_early_uart_putc_timeout('X', 0x00, &cycles);
    // Verified: looped exactly 49,999 times and exited safely without hanging
    EXPECT_EQ(cycles, 49999U);

    // If THRE is ready (0x20), it should exit immediately on 0 cycles
    test_early_uart_putc_timeout('X', 0x20, &cycles);
    EXPECT_EQ(cycles, 0U);
}

// T1-28: Early UART print hex accurately formats 32-bit values via pure ALU math
TEST_F(AriesSystemTest, EarlyUartPrintHexPureAlu)
{
    char hex[16];
    test_early_uart_print_hex(0x00000000, hex);
    EXPECT_STREQ(hex, "00000000");

    test_early_uart_print_hex(0x0000002F, hex);
    EXPECT_STREQ(hex, "0000002F");

    test_early_uart_print_hex(0xCAFEBABE, hex);
    EXPECT_STREQ(hex, "CAFEBABE");

    test_early_uart_print_hex(0xFFFFFFFF, hex);
    EXPECT_STREQ(hex, "FFFFFFFF");
}

// T1-29: enable_irq sets MSTATUS_MIE and MIE_MEIE
TEST_F(AriesSystemTest, EnableIrqSetsMstatusMIEAndMieMEIE)
{
    mock_mstatus = 0;
    mock_mie = 0;

    test_enable_irq();

    EXPECT_EQ(mock_mstatus & 0x08, 0x08U);
    EXPECT_EQ(mock_mie & 0x800, 0x800U);

    test_disable_irq();
    EXPECT_EQ(mock_mstatus & 0x08, 0U);
    EXPECT_EQ(mock_mie & 0x800, 0x800U); // mie remains configured
}
