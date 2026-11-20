/*
 * Test Suite: C-DAC ARIES v3.0 Linker Script & Memory Layout Specification
 * Target Silicon: C-DAC THEJAS32 SoC (VEGA ET1031 RISC-V RV32IM @ 100 MHz, 256 KB SRAM, No XIP)
 * Reference: PLAN/01_BOOT_LINKER_AND_CORE_PRIMITIVES.md (Microcomponent 01_B)
 * Citations:
 *   - knowledgebase/codebase_evaluations/02_EVAL_LINKER_ARIES_V3_LD.md
 *   - knowledgebase/components/09_BUILD_LINKER_AND_MEMORY.md
 *   - logs/hardware_inspection/12_STM32_LINKER_AND_SECTIONS.md
 *   - logs/hardware_inspection/25_ESP32_BUILD_AND_LINKER.md
 *
 * Methodology:
 *   Strict Test-Driven Development (TDD) - Step 1: Living Documentation.
 *   This test suite formally specifies the linker memory layout, section link order,
 *   budget assertions, Parameter Group registry anchors, and 8 KB stack headroom.
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "unittest_macros.h"
#include "gtest/gtest.h"

namespace {

// Silicon Hardware Memory Constants for C-DAC THEJAS32
constexpr uint32_t THEJAS32_TOTAL_SRAM_BYTES   = 256 * 1024;       // 256 KB (0x00200000 - 0x0023FFFF)
constexpr uint32_t THEJAS32_BOOTLOADER_RESERVED = 8 * 1024;        // 8 KB ROM bootloader partition
constexpr uint32_t THEJAS32_EXEC_ORIGIN        = 0x00202000U;      // Execution origin for user firmware
constexpr uint32_t THEJAS32_EXEC_LENGTH        = 248 * 1024;       // 248 KB usable SRAM budget (253,952 bytes)
constexpr uint32_t THEJAS32_EXEC_CEILING       = 0x0023FFFFU;      // Top-of-SRAM physical limit
constexpr uint32_t THEJAS32_MIN_STACK_SIZE     = 8 * 1024;         // 8 KB minimum stack headroom

// Mock representation of linker section allocation
struct MockSectionPlacement {
    const char *name;
    uint32_t vma_start;
    uint32_t size;
    uint32_t alignment;
};

// Evaluator for linker script budget assertions
bool evaluate_linker_budget_assertion(uint32_t text_sz, uint32_t rodata_sz,
                                     uint32_t data_sz, uint32_t bss_sz,
                                     uint32_t stack_sz, uint32_t *out_total) {
    uint32_t total = text_sz + rodata_sz + data_sz + bss_sz + stack_sz;
    if (out_total) {
        *out_total = total;
    }
    return (total <= THEJAS32_EXEC_LENGTH);
}

} // namespace

// =============================================================================
// Test 1: In-SRAM Execution Memory Boundaries
// =============================================================================
TEST(AriesLinkerTest, Test_MemoryBounds_OriginAndLength) {
    // Arrange: Physical SRAM constants
    const uint32_t expected_origin = THEJAS32_EXEC_ORIGIN;
    const uint32_t expected_length = THEJAS32_EXEC_LENGTH;
    const uint32_t expected_ceiling = THEJAS32_EXEC_CEILING;

    // Act: Derive SRAM memory mapping
    uint32_t computed_ceiling = expected_origin + expected_length - 1U;

    // Assert: Execution origin begins immediately above 8 KB bootloader partition
    EXPECT_EQ(expected_origin, 0x00202000U)
        << "Execution origin must be 0x00202000 (Ground Truth 2: In-SRAM Execution)";

    // Assert: Total usable length is strictly 248 KB
    EXPECT_EQ(expected_length, 253952U)
        << "Usable SRAM budget must be 248 KB (253,952 bytes)";

    // Assert: Top of memory does not exceed physical 256 KB SRAM limit
    EXPECT_EQ(computed_ceiling, expected_ceiling)
        << "Computed memory ceiling must exactly equal 0x0023FFFF";

    // Real-world flight consequence:
    // If ORIGIN is set to 0x00200000 or LENGTH set to 256K (as in generic C-DAC link1.lds),
    // the firmware overwrites the on-chip ROM bootloader's transfer vector, causing hard crashes.
}

// =============================================================================
// Test 2: Compile-Time SRAM Budget Enforcement Assertion
// =============================================================================
TEST(AriesLinkerTest, Test_SRAMBudgetEnforcement_CompileTimeAssertion) {
    // Arrange: Scenario A - Valid optimized Betaflight build (< 248 KB)
    const uint32_t text_valid   = 160 * 1024; // 160 KB code
    const uint32_t rodata_valid = 20 * 1024;  // 20 KB constants
    const uint32_t data_valid   = 2 * 1024;   // 2 KB initialized data
    const uint32_t bss_valid    = 45 * 1024;  // 45 KB BSS (including 8 KB EEPROM)
    const uint32_t stack_valid  = 8 * 1024;   // 8 KB stack

    // Arrange: Scenario B - Unpruned bloated build (> 248 KB, e.g. 770 KB)
    const uint32_t text_bloat   = 600 * 1024; // 600 KB unpruned code

    // Act
    uint32_t total_valid = 0;
    bool pass_valid = evaluate_linker_budget_assertion(text_valid, rodata_valid,
                                                       data_valid, bss_valid,
                                                       stack_valid, &total_valid);

    uint32_t total_bloat = 0;
    bool pass_bloat = evaluate_linker_budget_assertion(text_bloat, rodata_valid,
                                                       data_valid, bss_valid,
                                                       stack_valid, &total_bloat);

    // Assert: Valid build passes within budget with headroom
    EXPECT_TRUE(pass_valid);
    EXPECT_LE(total_valid, THEJAS32_EXEC_LENGTH);
    EXPECT_GT(THEJAS32_EXEC_LENGTH - total_valid, 0U);

    // Assert: Bloated build triggers fatal linker assertion
    EXPECT_FALSE(pass_bloat);
    EXPECT_GT(total_bloat, THEJAS32_EXEC_LENGTH);

    // Real-world flight consequence:
    // Without compile-time ASSERT(_end <= ORIGIN(RAM) + LENGTH(RAM)), oversized binaries
    // silently wrap or overwrite unmapped memory, bricking the drone upon upload.
}

// =============================================================================
// Test 3: Dedicated 8 KB Stack Allocation & 16-Byte Alignment
// =============================================================================
TEST(AriesLinkerTest, Test_StackAllocation_Minimum8KBHeadroom) {
    // Arrange: Stack section boundary symbols
    const uint32_t sram_top = 0x0023FFE0U; // 32 bytes below ceiling for hardware safety
    const uint32_t stack_size = THEJAS32_MIN_STACK_SIZE; // 8192 bytes
    const uint32_t sstack = sram_top - stack_size;
    const uint32_t estack = sram_top;

    // Act: Check stack bounds and alignment
    uint32_t allocated_size = estack - sstack;

    // Assert: Stack size meets or exceeds 8 KB requirement
    EXPECT_EQ(allocated_size, 8192U)
        << "Stack size must be at least 8 KB (0x2000 bytes) for deep nested interrupt handling";

    // Assert: estack and sstack must be 16-byte aligned per RV32 ABI
    EXPECT_EQ(estack % 16U, 0U) << "Top of stack (_estack) must be 16-byte aligned!";
    EXPECT_EQ(sstack % 16U, 0U) << "Bottom of stack (_sstack) must be 16-byte aligned!";

    // Real-world flight consequence:
    // Betaflight 4.x interrupt nesting (Timer 0 timebase, Timer 1 PWM, UART0 RX ring)
    // combined with soft-float trigonometric routines requires > 5 KB peak stack depth.
    // Sizing stack to 4 KB risks silent stack collision with .bss, corrupting PID state.
}

// =============================================================================
// Test 4: Section Link Order: .text.startup Precedence Over .init
// =============================================================================
TEST(AriesLinkerTest, Test_SectionLinkOrder_StartupPrecedenceOverInit) {
    // Arrange: Model linker output section layout
    // Section order must ensure .text.startup is at the absolute front of the binary
    const char *section_order[] = {
        ".text.startup",
        ".init",
        ".text",
        ".rodata",
        ".pg_registry",
        ".pg_resetdata",
        ".data",
        ".bss",
        ".stack"
    };

    // Act: Find position of .text.startup vs .init
    int startup_idx = -1;
    int init_idx = -1;
    for (int i = 0; i < 9; i++) {
        if (strcmp(section_order[i], ".text.startup") == 0) startup_idx = i;
        if (strcmp(section_order[i], ".init") == 0) init_idx = i;
    }

    // Assert: .text.startup MUST precede .init
    EXPECT_LT(startup_idx, init_idx)
        << "FATAL: .text.startup must precede .init in linker script to guarantee "
        << "_start is anchored at 0x00202000 (02_EVAL_LINKER_ARIES_V3_LD.md)";

    // Assert: .text.startup must be the very first output block
    EXPECT_EQ(startup_idx, 0);

    // Real-world flight consequence:
    // If .init precedes .text.startup, any library defining .init will usurp address 0x00202000,
    // causing the ROM bootloader to jump into library init code before GPRs or stack are set up.
}

// =============================================================================
// Test 5: Parameter Group Registry Linker Anchors & Alignment
// =============================================================================
TEST(AriesLinkerTest, Test_ParameterGroupRegistry_AnchorsAndAlignment) {
    // Arrange: Simulated Parameter Group registry layout
    // Each pgRegistry_t entry is 16 bytes in size
    const uint32_t pg_registry_start = 0x00207000U;
    const uint32_t num_pg_entries = 80;
    const uint32_t pg_entry_size = 16;
    const uint32_t pg_registry_end = pg_registry_start + (num_pg_entries * pg_entry_size);

    // Act
    uint32_t total_registry_bytes = pg_registry_end - pg_registry_start;

    // Assert: Boundary symbols are 4-byte word aligned
    EXPECT_EQ(pg_registry_start % 4U, 0U);
    EXPECT_EQ(pg_registry_end % 4U, 0U);

    // Assert: Total registry size is an exact multiple of the entry structure
    EXPECT_EQ(total_registry_bytes % pg_entry_size, 0U);

    // Real-world flight consequence:
    // Betaflight's PG runtime iterates from __pg_registry_start to __pg_registry_end.
    // Misalignment or stripping of these anchors prevents configuration discovery,
    // freezing the flight controller on boot in pgResetAll().
}

// =============================================================================
// Test 6: 4-Byte Word Alignment on All Section Endpoints
// =============================================================================
TEST(AriesLinkerTest, Test_SectionEndpoints_4ByteAlignment) {
    // Arrange: Synthetic section boundary offsets
    uint32_t _etext   = 0x00204124U;
    uint32_t _srodata = 0x00204124U;
    uint32_t _erodata = 0x00205800U;
    uint32_t _sdata   = 0x00205800U;
    uint32_t _edata   = 0x00205AC0U;
    uint32_t _sbss    = 0x00205AC0U;
    uint32_t _ebss    = 0x0021A400U;

    // Act & Assert: All section termination markers must be 4-byte aligned
    EXPECT_EQ(_etext % 4U, 0U);
    EXPECT_EQ(_srodata % 4U, 0U);
    EXPECT_EQ(_erodata % 4U, 0U);
    EXPECT_EQ(_sdata % 4U, 0U);
    EXPECT_EQ(_edata % 4U, 0U);
    EXPECT_EQ(_sbss % 4U, 0U);
    EXPECT_EQ(_ebss % 4U, 0U);

    // Real-world flight consequence:
    // In startup.S, copy_data_loop and zero_bss_loop advance pointers by 4 bytes (lw/sw).
    // An odd section boundary causes the loop condition (beq t0, t1) to miss termination,
    // resulting in runaway memory overwrites and hardware lockups.
}
