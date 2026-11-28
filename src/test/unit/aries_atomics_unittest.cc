/*
 * Test Suite: C-DAC ARIES v3.0 RV32IM Atomics & Platform Primitives Specification
 * Target Silicon: C-DAC THEJAS32 SoC (VEGA ET1031 RISC-V RV32IM @ 100 MHz, No 'A' Extension, No BASEPRI)
 * Reference: PLAN/01_BOOT_LINKER_AND_CORE_PRIMITIVES.md (Microcomponent 01_C)
 * Citations:
 *   - knowledgebase/codebase_evaluations/04_EVAL_PLATFORM_H.md
 *   - knowledgebase/codebase_evaluations/12_EVAL_STUB_DRIVERS_C.md
 *   - src/main/build/atomic.h
 *
 * Methodology:
 *   Strict Test-Driven Development (TDD) - Step 1: Living Documentation.
 *   This test suite formally specifies state-preserving atomic critical sections,
 *   nested interrupt safety, custom BASEPRI emulation for Betaflight ATOMIC_BLOCK,
 *   and RV32IM libatomic software emulation shims.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "unittest_macros.h"
#include "gtest/gtest.h"

extern "C" {
#include "aries_host_mmio.h"
unsigned int aries_test___atomic_fetch_add_4(volatile void *ptr, unsigned int val, int memorder);
unsigned int aries_test___atomic_fetch_sub_4(volatile void *ptr, unsigned int val, int memorder);
unsigned int aries_test___atomic_fetch_and_4(volatile void *ptr, unsigned int val, int memorder);
unsigned int aries_test___atomic_fetch_or_4(volatile void *ptr, unsigned int val, int memorder);
unsigned int aries_test___atomic_exchange_4(volatile void *ptr, unsigned int val, int memorder);
bool aries_test___atomic_compare_exchange_4(volatile void *ptr, void *expected, unsigned int desired,
                                            bool weak, int success_memorder, int failure_memorder);
unsigned int aries_test___atomic_load_4(const volatile void *ptr, int memorder);
void aries_test___atomic_store_4(volatile void *ptr, unsigned int val, int memorder);
void persistentObjectInit(void);
uint32_t persistentObjectRead(int id);
void persistentObjectWrite(int id, uint32_t value);
uint16_t adcGetValue(int source);
}

namespace {

// Simulated RISC-V Machine Status (mstatus) CSR Register
// In RISC-V Privileged Architecture:
// Bit 3 (0x08) is MIE: Machine Interrupt Enable
constexpr uint32_t MSTATUS_MIE_BIT = (1U << 3); // Bit 3 = 0x00000008

struct MockCpuState {
    uint32_t mstatus;
    uint32_t mie;

    void reset(bool interrupts_enabled) {
        mstatus = interrupts_enabled ? MSTATUS_MIE_BIT : 0U;
        mie = 0x800U; // MEIE (Machine External Interrupt Enable)
    }

    bool are_interrupts_enabled() const {
        return (mstatus & MSTATUS_MIE_BIT) != 0U;
    }
};

static MockCpuState cpu;

// Emulated RISC-V Atomic Enter: csrrci %0, mstatus, 8
// Atomically captures current mstatus and clears bit 3 (MIE)
uint32_t mock_thejas32_atomic_enter(MockCpuState &state) {
    uint32_t prev_mstatus = state.mstatus;
    state.mstatus &= ~MSTATUS_MIE_BIT; // Clear MIE bit
    return prev_mstatus;
}

// Emulated RISC-V Atomic Exit: if (prev & 8) csrs mstatus, 8
// Restores MIE ONLY if it was previously set
void mock_thejas32_atomic_exit(MockCpuState &state, uint32_t prev_mstatus) {
    if (prev_mstatus & MSTATUS_MIE_BIT) {
        state.mstatus |= MSTATUS_MIE_BIT; // Restore MIE bit
    }
}

// Custom BASEPRI Emulation for Betaflight ATOMIC_BLOCK
// On ARM: BASEPRI == 0 means interrupts unmasked (enabled).
//         BASEPRI != 0 means interrupts masked (disabled).
uint8_t mock_get_BASEPRI(const MockCpuState &state) {
    // If MIE is enabled, BASEPRI is 0 (unmasked)
    // If MIE is disabled, BASEPRI is 1 (masked)
    return state.are_interrupts_enabled() ? 0U : 1U;
}

void mock_set_BASEPRI(MockCpuState &state, uint8_t basePri) {
    // If restoring to 0, re-enable interrupts
    // If restoring to non-zero, keep interrupts disabled
    if (basePri == 0U) {
        state.mstatus |= MSTATUS_MIE_BIT;
    } else {
        state.mstatus &= ~MSTATUS_MIE_BIT;
    }
}

void mock_set_BASEPRI_MAX(MockCpuState &state, uint8_t basePri) {
    (void)basePri;
    // Elevating priority unconditionally masks interrupts
    state.mstatus &= ~MSTATUS_MIE_BIT;
}

// RV32IM Libatomic Software Shims (Thread-safe memory mutators)
uint32_t mock_atomic_fetch_add_4(MockCpuState &state, volatile uint32_t *ptr, uint32_t val) {
    uint32_t prev = mock_thejas32_atomic_enter(state);
    uint32_t old = *ptr;
    *ptr = old + val;
    mock_thejas32_atomic_exit(state, prev);
    return old;
}

uint32_t mock_atomic_fetch_sub_4(MockCpuState &state, volatile uint32_t *ptr, uint32_t val) {
    uint32_t prev = mock_thejas32_atomic_enter(state);
    uint32_t old = *ptr;
    *ptr = old - val;
    mock_thejas32_atomic_exit(state, prev);
    return old;
}

uint32_t mock_atomic_fetch_and_4(MockCpuState &state, volatile uint32_t *ptr, uint32_t val) {
    uint32_t prev = mock_thejas32_atomic_enter(state);
    uint32_t old = *ptr;
    *ptr = old & val;
    mock_thejas32_atomic_exit(state, prev);
    return old;
}

uint32_t mock_atomic_fetch_or_4(MockCpuState &state, volatile uint32_t *ptr, uint32_t val) {
    uint32_t prev = mock_thejas32_atomic_enter(state);
    uint32_t old = *ptr;
    *ptr = old | val;
    mock_thejas32_atomic_exit(state, prev);
    return old;
}

uint32_t mock_atomic_exchange_4(MockCpuState &state, volatile uint32_t *ptr, uint32_t val) {
    uint32_t prev = mock_thejas32_atomic_enter(state);
    uint32_t old = *ptr;
    *ptr = val;
    mock_thejas32_atomic_exit(state, prev);
    return old;
}

bool mock_atomic_compare_exchange_4(MockCpuState &state, volatile uint32_t *ptr,
                                    uint32_t *expected, uint32_t desired) {
    uint32_t prev = mock_thejas32_atomic_enter(state);
    uint32_t current = *ptr;
    if (current == *expected) {
        *ptr = desired;
        mock_thejas32_atomic_exit(state, prev);
        return true;
    } else {
        *expected = current;
        mock_thejas32_atomic_exit(state, prev);
        return false;
    }
}

} // namespace

// =============================================================================
// Test 1: State-Preserving Atomic Block Disables and Restores Interrupts
// =============================================================================
TEST(AriesAtomicsTest, Test_Atomic_StatePreserving_DisablesAndRestores) {
    // Arrange: Normal thread context where interrupts are enabled
    cpu.reset(true);
    ASSERT_TRUE(cpu.are_interrupts_enabled()) << "Interrupts must initially be enabled";

    // Act: Enter atomic critical section
    uint32_t prev = mock_thejas32_atomic_enter(cpu);

    // Assert: Inside the critical section, interrupts must be strictly disabled
    EXPECT_FALSE(cpu.are_interrupts_enabled())
        << "Interrupts must be disabled inside atomic block (Ground Truth 5: Atomics via mstatus.MIE)";
    EXPECT_EQ(prev & MSTATUS_MIE_BIT, MSTATUS_MIE_BIT)
        << "Saved previous state must record that interrupts were active";

    // Act: Exit atomic critical section
    mock_thejas32_atomic_exit(cpu, prev);

    // Assert: Interrupts must be cleanly restored to enabled
    EXPECT_TRUE(cpu.are_interrupts_enabled())
        << "Interrupts must be restored upon exiting atomic block";

    // Real-world flight consequence:
    // If interrupts are not disabled, Timer 1 PWM or UART0 RX interrupts preempt
    // task queue updates, causing memory corruption in scheduler.c.
}

// =============================================================================
// Test 2: Nested Atomic Block Preserves Disabled State Inside ISRs
// =============================================================================
TEST(AriesAtomicsTest, Test_Atomic_Nested_PreservesDisabledState) {
    // Arrange: ISR context where interrupts are already disabled (mstatus.MIE = 0)
    cpu.reset(false);
    ASSERT_FALSE(cpu.are_interrupts_enabled())
        << "Interrupts are already disabled inside an active ISR";

    // Act: Outer ISR calls an atomic routine (e.g. updating packet counter)
    uint32_t outer_save = mock_thejas32_atomic_enter(cpu);

    // Assert: Interrupts remain disabled
    EXPECT_FALSE(cpu.are_interrupts_enabled());
    EXPECT_EQ(outer_save & MSTATUS_MIE_BIT, 0U);

    // Act: Inner nested atomic call
    uint32_t inner_save = mock_thejas32_atomic_enter(cpu);
    EXPECT_FALSE(cpu.are_interrupts_enabled());

    // Act: Exit inner atomic block
    mock_thejas32_atomic_exit(cpu, inner_save);

    // Assert: CRITICAL CHECK - Interrupts MUST REMAIN DISABLED!
    // In historical master, __enable_irq() was called here unconditionally,
    // prematurely unmasking global interrupts inside active ISRs (12_EVAL_STUB_DRIVERS_C.md)!
    EXPECT_FALSE(cpu.are_interrupts_enabled())
        << "FATAL: Inner atomic exit must NOT re-enable interrupts when called within disabled context!";

    // Act: Exit outer block
    mock_thejas32_atomic_exit(cpu, outer_save);

    // Assert: Interrupts still remain disabled matching the initial ISR state
    EXPECT_FALSE(cpu.are_interrupts_enabled());
}

// =============================================================================
// Test 3: Custom BASEPRI Save and Restore for Betaflight ATOMIC_BLOCK
// =============================================================================
TEST(AriesAtomicsTest, Test_Atomic_Basepri_SaveAndRestore) {
    // Arrange: Interrupts enabled
    cpu.reset(true);

    // Act: Simulate ATOMIC_BLOCK entry
    uint8_t basepri_save = mock_get_BASEPRI(cpu);
    EXPECT_EQ(basepri_save, 0U) << "When interrupts are enabled, BASEPRI save token must be 0";

    mock_set_BASEPRI_MAX(cpu, 1U);
    EXPECT_FALSE(cpu.are_interrupts_enabled())
        << "ATOMIC_BLOCK entry must disable interrupts via set_BASEPRI_MAX";

    // Act: Simulate ATOMIC_BLOCK exit
    mock_set_BASEPRI(cpu, basepri_save);
    EXPECT_TRUE(cpu.are_interrupts_enabled())
        << "Restoring basepri=0 must re-enable interrupts";

    // Arrange: Interrupts already disabled
    cpu.reset(false);
    basepri_save = mock_get_BASEPRI(cpu);
    EXPECT_EQ(basepri_save, 1U) << "When interrupts are disabled, BASEPRI save token must be 1";

    mock_set_BASEPRI_MAX(cpu, 1U);
    EXPECT_FALSE(cpu.are_interrupts_enabled());

    // Act: Simulate ATOMIC_BLOCK exit when previously disabled
    mock_set_BASEPRI(cpu, basepri_save);
    EXPECT_FALSE(cpu.are_interrupts_enabled())
        << "Restoring basepri=1 must keep interrupts disabled";

    // Real-world flight consequence:
    // In historical master (04_EVAL_PLATFORM_H.md), __set_BASEPRI was a dummy stub:
    // static inline void __set_BASEPRI(uint32_t b) { (void)b; }
    // As a result, ATOMIC_BLOCK(NVIC_PRIO_MAX) masked ZERO interrupts!
}

// =============================================================================
// Test 4: Libatomic Fetch-Add 32-bit Shim
// =============================================================================
TEST(AriesAtomicsTest, Test_Atomic_FetchAdd32_Shim) {
    // Arrange
    cpu.reset(true);
    volatile uint32_t counter = 100U;

    // Act: Atomically add 25
    uint32_t old_val = mock_atomic_fetch_add_4(cpu, &counter, 25U);

    // Assert: Returns old value, memory updated, interrupts restored
    EXPECT_EQ(old_val, 100U) << "fetch_add must return the previous value";
    EXPECT_EQ(counter, 125U) << "Target memory must contain the sum";
    EXPECT_TRUE(cpu.are_interrupts_enabled()) << "Interrupts must be restored";

    // Test inside ISR context (interrupts disabled)
    cpu.reset(false);
    old_val = mock_atomic_fetch_add_4(cpu, &counter, 15U);
    EXPECT_EQ(old_val, 125U);
    EXPECT_EQ(counter, 140U);
    EXPECT_FALSE(cpu.are_interrupts_enabled())
        << "Interrupts must remain disabled when fetch_add is called from ISR";
}

// =============================================================================
// Test 5: Libatomic Fetch-Sub, Fetch-And, Fetch-Or Shims
// =============================================================================
TEST(AriesAtomicsTest, Test_Atomic_FetchSubAndOr32_Shims) {
    // Arrange
    cpu.reset(true);
    volatile uint32_t value = 0x0000FF00U;

    // Act & Assert: Fetch-Sub
    uint32_t old = mock_atomic_fetch_sub_4(cpu, &value, 0x00000100U);
    EXPECT_EQ(old, 0x0000FF00U);
    EXPECT_EQ(value, 0x0000FE00U);

    // Act & Assert: Fetch-Or
    old = mock_atomic_fetch_or_4(cpu, &value, 0x00000055U);
    EXPECT_EQ(old, 0x0000FE00U);
    EXPECT_EQ(value, 0x0000FE55U);

    // Act & Assert: Fetch-And
    old = mock_atomic_fetch_and_4(cpu, &value, 0x000000FFU);
    EXPECT_EQ(old, 0x0000FE55U);
    EXPECT_EQ(value, 0x00000055U);
}

// =============================================================================
// Test 6: Libatomic Exchange 32-bit Shim
// =============================================================================
TEST(AriesAtomicsTest, Test_Atomic_Exchange32_Shim) {
    // Arrange
    cpu.reset(true);
    volatile uint32_t shared_flag = 0xAA55AA55U;

    // Act: Atomically swap value
    uint32_t old_flag = mock_atomic_exchange_4(cpu, &shared_flag, 0x12345678U);

    // Assert
    EXPECT_EQ(old_flag, 0xAA55AA55U) << "Exchange must return old value";
    EXPECT_EQ(shared_flag, 0x12345678U) << "Target memory must receive new value";
    EXPECT_TRUE(cpu.are_interrupts_enabled());
}

// =============================================================================
// Test 7: Libatomic Compare-Exchange 32-bit Shim
// =============================================================================
TEST(AriesAtomicsTest, Test_Atomic_CompareExchange32_Shim) {
    // Arrange
    cpu.reset(true);
    volatile uint32_t lock_word = 0U; // 0 = unlocked

    // Act 1: Successful compare-exchange (matches expected 0)
    uint32_t expected = 0U;
    bool success = mock_atomic_compare_exchange_4(cpu, &lock_word, &expected, 1U);

    // Assert 1: Succeeded, lock_word becomes 1
    EXPECT_TRUE(success);
    EXPECT_EQ(lock_word, 1U);
    EXPECT_EQ(expected, 0U);

    // Act 2: Failed compare-exchange (expected 0, but current is 1)
    expected = 0U;
    success = mock_atomic_compare_exchange_4(cpu, &lock_word, &expected, 2U);

    // Assert 2: Failed, lock_word remains 1, expected updated to 1
    EXPECT_FALSE(success);
    EXPECT_EQ(lock_word, 1U);
    EXPECT_EQ(expected, 1U) << "Failed compare_exchange must update expected with actual value";
}

// =============================================================================
// Test 8: Real stub_drivers.c Execution (Atomics, 'Bef1' Magic & ADC=0 Guard)
// =============================================================================
TEST(AriesAtomicsTest, Test_RealStubDriversC_AtomicsPersistentMagicAndAdcZero) {
    thejas32_host_reset_all();
    thejas32_host_mstatus = 0x8U; // MIE enabled

    volatile uint32_t word = 10U;
    EXPECT_EQ(aries_test___atomic_fetch_add_4(&word, 5U, 5), 10U);
    EXPECT_EQ(word, 15U);
    EXPECT_EQ(aries_test___atomic_fetch_sub_4(&word, 3U, 5), 15U);
    EXPECT_EQ(aries_test___atomic_fetch_or_4(&word, 0x100U, 5), 12U);
    EXPECT_EQ(aries_test___atomic_fetch_and_4(&word, 0x0FFU, 5), 0x10CU);
    EXPECT_EQ(aries_test___atomic_exchange_4(&word, 0x42U, 5), 12U);
    EXPECT_EQ(aries_test___atomic_load_4(&word, 5), 0x42U);

    unsigned int exp = 0x42U;
    EXPECT_TRUE(aries_test___atomic_compare_exchange_4(&word, &exp, 0x99U, false, 5, 5));
    EXPECT_EQ(word, 0x99U);
    EXPECT_FALSE(aries_test___atomic_compare_exchange_4(&word, &exp, 0x11U, false, 5, 5));
    EXPECT_EQ(exp, 0x99U);
    aries_test___atomic_store_4(&word, 77U, 5);
    EXPECT_EQ(word, 77U);
    EXPECT_EQ(thejas32_host_mstatus, 0x8U) << "MIE must be restored after unlocked calls";

    // Nested ISR context: MIE=0 must stay 0 across real C atomic calls
    thejas32_host_mstatus = 0U;
    EXPECT_EQ(aries_test___atomic_fetch_add_4(&word, 3U, 5), 77U);
    EXPECT_EQ(word, 80U);
    EXPECT_EQ(thejas32_host_mstatus, 0U);

    // Finding 13: Cold boot corrupt magic in .noinit clears all slots and sets 'Bef1'
    persistentObjectWrite(0, 0xDEAD0001U);
    persistentObjectWrite(1, 0xCAFEBABEU);
    persistentObjectInit();
    EXPECT_EQ(persistentObjectRead(0), 0x42656631U);
    EXPECT_EQ(persistentObjectRead(1), 0U);

    // Warm reboot: intact 'Bef1' magic preserves slot 1
    persistentObjectWrite(1, 420000U);
    persistentObjectInit();
    EXPECT_EQ(persistentObjectRead(1), 420000U);

    // Finding 11: adcGetValue safe zero stub when USE_ADC is disabled
    EXPECT_EQ(adcGetValue(0), 0U);
    EXPECT_EQ(adcGetValue(1), 0U);
    EXPECT_EQ(adcGetValue(2), 0U);
}

// =============================================================================
// Test 9: ET1031 Erratum Guard — Never Execute csrc/csrrci mstatus, 8 When MIE=0
// =============================================================================
TEST(AriesAtomicsTest, Test_AtomicEnter_PreservesMpieWhenMieAlreadyZero) {
    // 1. Verify trap-context mstatus (MPP=3, MPIE=1, MIE=0 = 0x1880) survives atomic shims
    thejas32_host_reset_all();
    thejas32_host_mstatus = 0x00001880U;
    volatile uint32_t word = 1U;
    EXPECT_EQ(aries_test___atomic_fetch_add_4(&word, 2U, 5), 1U);
    EXPECT_EQ(word, 3U);
    EXPECT_EQ(thejas32_host_mstatus, 0x00001880U)
        << "ET1031 erratum: atomic entry inside trap context (MIE=0) must preserve MPIE (0x1880)!";

    // 2. Verify target RISC-V implementation in platform.h guards csrc mstatus, 8 with (prev_mstatus & 8U)
    static char buf[32768];
    const char *paths[] = {
        "src/platform/ARIES/include/platform/platform.h",
        "../src/platform/ARIES/include/platform/platform.h",
        "../../src/platform/ARIES/include/platform/platform.h",
    };
    FILE *f = nullptr;
    for (size_t i = 0; i < sizeof(paths) / sizeof(paths[0]) && !f; i++) {
        f = fopen(paths[i], "rb");
    }
    ASSERT_NE(f, nullptr);
    const size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';

    EXPECT_EQ(strstr(buf, "csrrci %0, mstatus, 8"), nullptr)
        << "Unconditional csrrci mstatus, 8 wipes MPIE (0x1880 -> 0x1800) inside ET1031 trap handlers!";
    const char *riscvSec = strstr(buf, "csrr %0, mstatus");
    ASSERT_NE(riscvSec, nullptr);
    const char *enterFn = riscvSec;
    const char *exitFn = strstr(enterFn, "thejas32_atomic_exit(");
    ASSERT_NE(exitFn, nullptr);
    const char *guard = strstr(enterFn, "if (prev_mstatus & 8U)");
    EXPECT_TRUE(guard != nullptr && guard < exitFn)
        << "thejas32_atomic_enter() must read mstatus first and skip csrc when MIE is already 0!";
}
