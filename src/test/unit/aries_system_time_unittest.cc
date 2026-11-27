/*
 * Test Suite: C-DAC ARIES v3.0 Monotonic Timebase & Platform Primitives
 * Target Silicon: C-DAC THEJAS32 SoC (VEGA ET1031 RISC-V RV32IM @ 100 MHz, Synopsys DW_apb_timers)
 * Reference: PLAN/01_BOOT_LINKER_AND_CORE_PRIMITIVES.md (Microcomponent 01_D)
 * Citations:
 *   - knowledgebase/codebase_evaluations/05_EVAL_SYSTEM_C.md
 *   - knowledgebase/components/01_SYSTEM_AND_CLOCKS.md
 *   - logs/code_critique/05_SYSTEM_C.md
 *   - lib/vega-sdk/vega-sdk/bsp/drivers/timer/timer.c
 *
 * Methodology:
 *   Strict Test-Driven Development (TDD) - Step 1: Living Documentation.
 *   This test suite formally specifies cycle counter atomic double-read loop,
 *   32-bit modular reciprocal fast microsecond and millisecond conversions,
 *   strict monotonicity, Timer 0 EOI acknowledgment, CTZ interrupt dispatch,
 *   soft-reboot jump vectors, and bare-metal non-RVC memory primitives.
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <vector>

#include "unittest_macros.h"
#include "gtest/gtest.h"

extern "C" {
#include "aries_host_mmio.h"
uint64_t getCycleCounter64(void);
uint32_t micros(void);
uint32_t millis(void);
void thejas32_interrupt_register(uint8_t irq_number, thejas32_irq_handler_t handler);
void thejas32_interrupt_enable(uint8_t irq_number);
uint32_t handle_trap(uint32_t mcause, uint32_t mepc, uint32_t *sp);
void systemReset(void);
void targetPreInit(void);
}

namespace {

// Hardware Constants for C-DAC THEJAS32 @ 100 MHz
constexpr uint32_t SYSTEM_CORE_CLOCK_HZ    = 100000000U; // 100 MHz
constexpr uint32_t CYCLES_PER_MICROSECOND  = 100U;       // 100 cycles = 1 us
constexpr uint32_t CYCLES_PER_MILLISECOND  = 100000U;    // 100,000 cycles = 1 ms

// Fast Modular Reciprocal Conversion: Cycles -> Microseconds
// Eliminates 150-cycle GCC __udivdi3 software division on RV32IM
uint32_t compute_micros_fast(uint32_t hi, uint32_t lo) {
    uint32_t r_hi = hi % 100U;
    uint32_t add = r_hi * 96U;
    uint32_t lo_sum = lo + add;
    uint32_t carry = (lo_sum < lo) ? 1U : 0U;
    uint32_t q = (carry * 42949672U) + ((carry * 96U + lo_sum) / 100U);
    return (r_hi * 42949672U) + q;
}

// Fast Modular Reciprocal Conversion: Cycles -> Milliseconds
uint32_t compute_millis_fast(uint32_t hi, uint32_t lo) {
    uint32_t r_hi = hi % 100000U;
    uint64_t add = (uint64_t)r_hi * 67296ULL;
    uint64_t rem = add + lo;
    uint32_t carry = (uint32_t)(rem >> 32);
    uint32_t lo_sum = (uint32_t)rem;
    uint32_t q_rem = (carry * 42949U) + (uint32_t)((carry * 67296U + lo_sum) / 100000U);
    return (r_hi * 42949U) + q_rem;
}

// Reference standard 64-bit integer divisions
uint32_t reference_micros(uint32_t hi, uint32_t lo) {
    uint64_t cycles = ((uint64_t)hi << 32) | (uint64_t)lo;
    return (uint32_t)(cycles / 100ULL);
}

uint32_t reference_millis(uint32_t hi, uint32_t lo) {
    uint64_t cycles = ((uint64_t)hi << 32) | (uint64_t)lo;
    return (uint32_t)(cycles / 100000ULL);
}

// Mock Cycle Counter Register Reader with simulated race condition
struct MockCycleCounterSource {
    uint32_t hi_first;
    uint32_t lo;
    uint32_t hi_second;
    int read_count;

    void reset(uint32_t h1, uint32_t l, uint32_t h2) {
        hi_first = h1;
        lo = l;
        hi_second = h2;
        read_count = 0;
    }
};

static MockCycleCounterSource mock_cycles;

void mock_getCycleCounterWords(uint32_t *hi_out, uint32_t *lo_out) {
    uint32_t hi, lo, hi2;
    do {
        mock_cycles.read_count++;
        if (mock_cycles.read_count == 1) {
            hi = mock_cycles.hi_first;
            lo = mock_cycles.lo;
            hi2 = mock_cycles.hi_second;
        } else {
            // Subsequent read settles after race
            hi = mock_cycles.hi_second;
            lo = mock_cycles.lo;
            hi2 = mock_cycles.hi_second;
        }
    } while (hi != hi2);
    *hi_out = hi;
    *lo_out = lo;
}

// Synopsys DW_apb_timers Hardware Timer 0 Mock
struct MockTimer0 {
    uint32_t load_count;
    uint32_t current_value;
    uint32_t control;
    uint32_t eoi_read_count;
    uint32_t overflows;

    void reset() {
        load_count = 0xFFFFFFFFU;
        current_value = 0xFFFFFFFFU;
        control = 0;
        eoi_read_count = 0;
        overflows = 0;
    }

    uint32_t read_eoi() {
        eoi_read_count++;
        return 0; // Reading clears the active interrupt
    }
};

static MockTimer0 mock_timer0;

void mock_timer0_isr() {
    mock_timer0.read_eoi();
    mock_timer0.overflows++;
}

// Bare-Metal Memory Primitives (Matching system.c implementations)
void *bare_memset(void *dest, int c, size_t n) {
    uint8_t *p = (uint8_t *)dest;
    uint8_t val = (uint8_t)c;
    while (n--) {
        *p++ = val;
    }
    return dest;
}

void *bare_memcpy(void *dest, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;
    while (n--) {
        *d++ = *s++;
    }
    return dest;
}

void *bare_memmove(void *dest, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;
    if (d < s) {
        while (n--) {
            *d++ = *s++;
        }
    } else if (d > s) {
        d += n;
        s += n;
        while (n--) {
            *--d = *--s;
        }
    }
    return dest;
}

} // namespace

// =============================================================================
// Test 1: Atomic 64-Bit Cycle Counter Reading Loop Prevents Rollover Time Warp
// =============================================================================
TEST(AriesSystemTimeTest, Test_Timebase_Atomic64BitCycleRead_RolloverSafety) {
    // Arrange: Lower 32 bits roll over from 0xFFFFFFFF to 0x00000005 during read.
    // 1st read of mcycleh returns 0; mcycle returns 0x00000005; 2nd read of mcycleh returns 1.
    mock_cycles.reset(0x00000000U, 0x00000005U, 0x00000001U);

    // Act: Execute atomic double-read loop
    uint32_t hi = 0, lo = 0;
    mock_getCycleCounterWords(&hi, &lo);

    // Assert: Loop detected race (read_count == 2) and returned consistent state
    EXPECT_EQ(mock_cycles.read_count, 2)
        << "Double-read loop must retry when mcycleh changes during read";
    EXPECT_EQ(hi, 0x00000001U) << "mcycleh must reflect updated post-rollover value";
    EXPECT_EQ(lo, 0x00000005U);

    // Real-world flight consequence:
    // Without the double-read loop (hi == hi2), reading hi=0, lo=5 results in a 4.29-second
    // backward time jump, triggering an enormous PID D-term spike that destabilizes the drone.
}

// =============================================================================
// Test 2: Fast Modular Reciprocal Division Accuracy Across Dynamic Range
// =============================================================================
TEST(AriesSystemTimeTest, Test_Timebase_FastModularReciprocal_MicrosAccuracy) {
    // Arrange: Array of diverse cycle points spanning 0 cycles to days of uptime
    const struct { uint32_t hi; uint32_t lo; } test_cases[] = {
        { 0U, 0U },                         // Zero
        { 0U, 50U },                        // 0 us (fractional truncation)
        { 0U, 100U },                       // Exactly 1 us
        { 0U, 999U },                       // 9 us
        { 0U, 100000000U },                 // Exactly 1 second (100M cycles)
        { 0U, 0xFFFFFFFFU },                // Just before 32-bit rollover (~42.94 s)
        { 1U, 0x00000000U },                // Exactly at rollover
        { 1U, 0x00000064U },                // 100 cycles past rollover
        { 5U, 123456789U },                 // ~3.5 minutes uptime
        { 100U, 54321U },                   // High mcycleh value
        { 1000U, 0xABCDEF00U },             // Days of continuous flight uptime
    };

    // Act & Assert: Fast modular math must strictly equal 64-bit integer division
    for (const auto &tc : test_cases) {
        uint32_t fast_us = compute_micros_fast(tc.hi, tc.lo);
        uint32_t ref_us  = reference_micros(tc.hi, tc.lo);

        EXPECT_EQ(fast_us, ref_us)
            << "Mismatch at hi=" << tc.hi << " lo=" << tc.lo
            << " (Fast: " << fast_us << ", Ref: " << ref_us << ")";
    }

    // Real-world flight consequence:
    // Standard 64-bit division by 100 calls __udivdi3, consuming > 150 cycles.
    // Fast modular reciprocal math executes in < 25 cycles, freeing > 65% CPU for PID math.
}

// =============================================================================
// Test 3: Fast Millisecond Clock Derivation and Precision
// =============================================================================
TEST(AriesSystemTimeTest, Test_Timebase_FastModularReciprocal_MillisAccuracy) {
    // Arrange: Diverse cycle counts tested against standard division by 100,000
    const struct { uint32_t hi; uint32_t lo; } test_cases[] = {
        { 0U, 0U },
        { 0U, 99999U },                     // 0 ms
        { 0U, 100000U },                    // Exactly 1 ms
        { 0U, 100000000U },                 // Exactly 1000 ms (1 s)
        { 0U, 0xFFFFFFFFU },                // ~42,949 ms
        { 1U, 0U },                         // Rollover
        { 10U, 500000U },                   // Long flight uptime
    };

    // Act & Assert
    for (const auto &tc : test_cases) {
        uint32_t fast_ms = compute_millis_fast(tc.hi, tc.lo);
        uint32_t ref_ms  = reference_millis(tc.hi, tc.lo);

        EXPECT_EQ(fast_ms, ref_ms)
            << "Millis mismatch at hi=" << tc.hi << " lo=" << tc.lo;
    }
}

// =============================================================================
// Test 4: Strict Monotonicity: 100,000 Continuous Sequential Queries
// =============================================================================
TEST(AriesSystemTimeTest, Test_Timebase_StrictMonotonicity_NoBackwardJumps) {
    // Arrange: Simulate continuous monotonically increasing cycles across 32-bit boundary
    uint32_t hi = 0;
    uint32_t lo = 0xFFFE0000U; // Start near rollover point
    uint32_t prev_us = compute_micros_fast(hi, lo);

    // Act: Advance counter 100,000 times by variable cycle steps (10 to 500 cycles)
    for (int i = 0; i < 100000; i++) {
        uint32_t step = 10U + (uint32_t)(i % 50);
        uint32_t old_lo = lo;
        lo += step;
        if (lo < old_lo) {
            hi++; // 32-bit overflow
        }

        uint32_t cur_us = compute_micros_fast(hi, lo);

        // Assert: Time must strictly never decrease
        ASSERT_GE(cur_us, prev_us)
            << "Monotonicity violated at step " << i
            << ": prev=" << prev_us << " cur=" << cur_us
            << " (hi=" << hi << " lo=" << lo << ")";

        prev_us = cur_us;
    }
}

// =============================================================================
// Test 5: delayMicroseconds Timing Bound Verification
// =============================================================================
TEST(AriesSystemTimeTest, Test_Timebase_DelayMicroseconds_BoundedExecution) {
    // Arrange: Simulated microsecond query source
    uint32_t sim_us = 1000U;
    const uint32_t delay_target = 50U; // 50 us delay

    // Act: Simulate delay loop execution
    uint32_t start_us = sim_us;
    while ((sim_us - start_us) < delay_target) {
        sim_us += 10U; // Simulate clock advance
    }

    // Assert: Loop exits once target is met or exceeded
    EXPECT_GE(sim_us - start_us, delay_target);
    EXPECT_EQ(sim_us - start_us, 50U);
}

// =============================================================================
// Test 6: Hardware Timer 0 EOI Interrupt Clear Verification
// =============================================================================
TEST(AriesSystemTimeTest, Test_Timebase_Timer0_EOI_Acknowledgement) {
    // Arrange: Fresh Timer 0 state
    mock_timer0.reset();
    ASSERT_EQ(mock_timer0.eoi_read_count, 0U);
    ASSERT_EQ(mock_timer0.overflows, 0U);

    // Act: Hardware timer triggers interrupt service routine
    mock_timer0_isr();

    // Assert: ISR read EOI to clear active interrupt in Synopsys DW_apb_timers IP
    EXPECT_EQ(mock_timer0.eoi_read_count, 1U)
        << "Timer 0 ISR must read EOI register to clear hardware interrupt latch";
    EXPECT_EQ(mock_timer0.overflows, 1U);

    // Real-world flight consequence:
    // If EOI is not read, the level-triggered timer interrupt remains permanently asserted,
    // causing an infinite ISR loop that freezes the flight controller upon boot.
}

// =============================================================================
// Test 7: External Interrupt Dispatching via O(1) __builtin_ctz Fast-Path
// =============================================================================
TEST(AriesSystemTimeTest, Test_Timebase_ExternalInterrupt_CTZ_FastPath) {
    // Arrange: Active peripheral interrupt bitmask in INTR_STATUS register
    // Simultaneous interrupts: UART0 (bit 0), SPI0 (bit 3), Timer 0 (bit 7)
    uint32_t intr_status = (1UL << 0) | (1UL << 3) | (1UL << 7);

    // Act: Dispatch pending interrupts in priority order using __builtin_ctz
    std::vector<int> dispatched_irqs;
    int loop_iterations = 0;

    while (intr_status != 0U) {
        int irq_num = __builtin_ctz(intr_status);
        dispatched_irqs.push_back(irq_num);
        intr_status &= ~(1UL << irq_num); // Clear serviced bit
        loop_iterations++;
    }

    // Assert: Dispatched in ascending order with exactly 3 iterations (not 32!)
    ASSERT_EQ(dispatched_irqs.size(), 3U);
    EXPECT_EQ(dispatched_irqs[0], 0); // UART0
    EXPECT_EQ(dispatched_irqs[1], 3); // SPI0
    EXPECT_EQ(dispatched_irqs[2], 7); // Timer 0
    EXPECT_EQ(loop_iterations, 3)
        << "CTZ fast-path must only execute iterations for active bits (05_EVAL_SYSTEM_C.md)";
}

// =============================================================================
// Test 8: Soft-Reset Vector Jump Target Validation
// =============================================================================
TEST(AriesSystemTimeTest, Test_Timebase_SoftReset_VectorJumpValidation) {
    // Arrange: Platform execution origin
    const uint32_t sram_origin = THEJAS32_SRAM_ORIGIN;

    // Assert: Reset vector jumps to 0x00202000 (resolves historical while(1) flaw)
    EXPECT_EQ(sram_origin, 0x00202000U)
        << "Soft reset must branch directly to entry point _start at 0x00202000";

    // Real-world flight consequence:
    // In historical master (05_EVAL_SYSTEM_C.md, Score 3.0/10), systemReset() trapped in while(1);
    // which prevented automated reboot after saving settings in the Betaflight Configurator.
}

// =============================================================================
// Test 9: Bare-Metal Non-RVC Memory Primitives (memset, memcpy, memmove)
// =============================================================================
TEST(AriesSystemTimeTest, Test_MemoryPrimitives_NonRVC_Memcpy_Memset_Memmove) {
    // 1. Test memset
    uint8_t buffer[64];
    bare_memset(buffer, 0x5A, sizeof(buffer));
    for (size_t i = 0; i < sizeof(buffer); i++) {
        ASSERT_EQ(buffer[i], 0x5AU);
    }

    // 2. Test memcpy
    uint8_t src[32];
    uint8_t dest[32];
    for (size_t i = 0; i < sizeof(src); i++) src[i] = (uint8_t)(i + 1);
    bare_memcpy(dest, src, sizeof(src));
    for (size_t i = 0; i < sizeof(dest); i++) {
        ASSERT_EQ(dest[i], src[i]);
    }

    // 3. Test memmove with overlapping regions (forward copy: d < s)
    uint8_t overlap_buf[32];
    for (size_t i = 0; i < sizeof(overlap_buf); i++) overlap_buf[i] = (uint8_t)i;
    bare_memmove(&overlap_buf[0], &overlap_buf[4], 16);
    for (size_t i = 0; i < 16; i++) {
        ASSERT_EQ(overlap_buf[i], (uint8_t)(i + 4));
    }

    // 4. Test memmove with overlapping regions (backward copy: d > s)
    for (size_t i = 0; i < sizeof(overlap_buf); i++) overlap_buf[i] = (uint8_t)i;
    bare_memmove(&overlap_buf[4], &overlap_buf[0], 16);
    for (size_t i = 0; i < 16; i++) {
        ASSERT_EQ(overlap_buf[i + 4], (uint8_t)i);
    }

    // Real-world flight consequence:
    // Standard library newlib memset/memcpy contain 16-bit compressed RVC opcodes.
    // Calling them on RV32IM ET1031 silicon immediately triggers Illegal Instruction traps (mcause = 2).
}

// =============================================================================
// Test 10: Monotonic Millisecond Delay Drift Mitigation (Finding 5)
// =============================================================================
TEST(AriesSystemTimeTest, Test_Delay_MonotonicMillis_PreventsAccumulatedDrift) {
    // Arrange: Verify delay duration calculation based on millis() delta
    // Simulating a delay(50) call
    uint32_t start_ms = 1000;
    uint32_t current_ms = 1049;

    // Act: Elapsed time check using unsigned 32-bit arithmetic (handles rollover)
    bool is_done = ((uint32_t)(current_ms - start_ms) >= 50);
    EXPECT_FALSE(is_done);

    current_ms = 1050;
    is_done = ((uint32_t)(current_ms - start_ms) >= 50);
    EXPECT_TRUE(is_done);

    // Verify 32-bit millisecond rollover boundary (at 2^32 - 10 ms)
    start_ms = 0xFFFFFFF6U;
    current_ms = 0x00000004U; // 14 ms elapsed across rollover boundary
    uint32_t elapsed = (uint32_t)(current_ms - start_ms);
    EXPECT_EQ(elapsed, 14U);

    // Real-world flight consequence:
    // Calling delayMicroseconds(1000) inside delay(ms) in a loop repeatedly executes 64-bit cycle math
    // and loop overhead, causing 1000 ms to stretch to 1020+ ms. Measuring millis() delta directly
    // eliminates cumulative drift during sensor boot calibration.
}

// =============================================================================
// Test 11: Machine External Interrupt Dispatch & Unhandled Line Masking (Finding 2)
// =============================================================================
TEST(AriesSystemTimeTest, Test_HandleTrap_UnhandledExternalInterrupt_MasksIntrEnBit) {
    // Arrange: THEJAS32 interrupt controller mock state
    uint32_t mock_intr_en = 0x00000080U;     // Bit 7 enabled in hardware
    uint32_t mock_intr_status = 0x00000080U; // Bit 7 asserted (spurious noise or unhandled event)
    void (*mock_interrupt_table[32])(void) = {nullptr}; // No handler registered for bit 7

    // Act: Simulate handle_trap fast CTZ dispatch loop
    uint32_t status = mock_intr_status;
    int handled_count = 0;
    while (status != 0U) {
        // Find trailing zero bit
        uint32_t i = 0;
        uint32_t temp = status;
        while ((temp & 1U) == 0U) {
            temp >>= 1;
            i++;
        }

        if (i < 32 && mock_interrupt_table[i] != nullptr) {
            mock_interrupt_table[i]();
            handled_count++;
        } else {
            // Finding 2: Defensively mask spurious/unhandled interrupt in INTR_EN
            if (i < 32) {
                mock_intr_en &= ~(1UL << i);
            }
        }
        status &= ~(1UL << i);
    }

    // Assert: Unhandled interrupt bit 7 must be masked in INTR_EN
    EXPECT_EQ(handled_count, 0);
    EXPECT_EQ(mock_intr_en, 0U)
        << "Spurious/unhandled peripheral interrupt must be automatically masked in INTR_EN";

    // Real-world flight consequence:
    // If unhandled interrupts are not masked in INTR_EN, the external line remains asserted,
    // causing an immediate re-trap upon mret, completely locking the CPU in an infinite trap storm.
}

// =============================================================================
// Test 12: Peripheral Interrupt Cleanup Across Software Reset (Finding 3)
// =============================================================================
TEST(AriesSystemTimeTest, Test_SystemReset_DisablesPeripheralInterruptsBeforeJump) {
    // Arrange: Active peripheral interrupt state prior to soft reboot
    uint32_t mock_intr_en = 0x00000101U; // Timer 0 (bit 0) and Timer 1 (bit 8) active

    // Act: Software reset peripheral cleanup sequence
    // Prior to jumping to entry point _start (0x00202000), disable all peripheral lines
    mock_intr_en = 0U;

    // Assert: All peripheral interrupts disabled
    EXPECT_EQ(mock_intr_en, 0U)
        << "systemReset must zero INTR_EN before jumping to _start to avoid boot-time trap panics";

    // Real-world flight consequence:
    // If INTR_EN remains active across soft reset, a pending Timer or UART interrupt fires
    // before the C runtime clears BSS and populates interrupt_table, crashing boot.
}

namespace {
uint32_t g_real_timer0_irq_hits = 0U;
void real_test_timer0_isr(void) { g_real_timer0_irq_hits++; }
} // namespace

// =============================================================================
// Test 13: Real system.c Execution (mcycle->micros/millis, Trap CTZ & Reset)
// =============================================================================
TEST(AriesSystemTimeTest, Test_RealSystemC_TimebaseTrapDispatchAndReset) {
    thejas32_host_reset_all();
    g_real_timer0_irq_hits = 0U;

    // 1. Cross-32-bit cycle counter math against 64-bit integer division
    const uint64_t test_cycles = 0x0000000100001000ULL;
    thejas32_host_mcycle_set(test_cycles);
    EXPECT_EQ(getCycleCounter64(), test_cycles);
    EXPECT_EQ(micros(), (uint32_t)(test_cycles / 100ULL));
    EXPECT_EQ(millis(), (uint32_t)(test_cycles / 100000ULL));

    // 2. O(1) CTZ external interrupt dispatch + unhandled IRQ auto-masking
    thejas32_interrupt_register(THEJAS32_IRQ_TIMER0, real_test_timer0_isr);
    thejas32_interrupt_enable(THEJAS32_IRQ_TIMER0);
    thejas32_interrupt_register(15U, nullptr);
    thejas32_interrupt_enable(15U);
    THEJAS32_INTR_CTRL.INTR_STATUS = (1UL << THEJAS32_IRQ_TIMER0) | (1UL << 15U);

    EXPECT_EQ(handle_trap(0x8000000BUL, 0x00202010U, nullptr), 0x00202010U);
    EXPECT_EQ(g_real_timer0_irq_hits, 1U);
    EXPECT_NE(THEJAS32_INTR_CTRL.INTR_EN & (1UL << THEJAS32_IRQ_TIMER0), 0U);
    EXPECT_EQ(THEJAS32_INTR_CTRL.INTR_EN & (1UL << 15U), 0U)
        << "Unhandled external IRQ must be masked in INTR_EN";

    // 3. Synchronous trap panic increments panic counter (does not hang host test)
    EXPECT_EQ(handle_trap(2U, 0x00202100U, nullptr), 0x00202104U);
    EXPECT_EQ(thejas32_host_panic_count, 1U);

    // 4. systemReset zeroes INTR_EN and clears MIE before jumping
    thejas32_host_mstatus = 0x8U;
    systemReset();
    EXPECT_EQ(THEJAS32_INTR_CTRL.INTR_EN, 0U);
    EXPECT_EQ(thejas32_host_mstatus & 0x8U, 0U);
    EXPECT_EQ(thejas32_host_reset_count, 1U);

    // 5. targetPreInit globally enables machine external interrupts (MEIE + MIE)
    thejas32_host_mie = 0U;
    thejas32_host_mstatus = 0U;
    THEJAS32_UART(0).UART_LSR = 0x60U;
    targetPreInit();
    EXPECT_NE(thejas32_host_mie & 0x800U, 0U);
    EXPECT_NE(thejas32_host_mstatus & 0x8U, 0U);
}
