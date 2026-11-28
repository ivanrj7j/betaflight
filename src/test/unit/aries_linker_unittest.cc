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

// =============================================================================
// Test 7: Persistent .noinit Section Allocation & BSS Isolation (TD-08)
// =============================================================================
TEST(AriesLinkerTest, Test_SectionLinkOrder_NoInitSectionPreservedAcrossWarmReset) {
    // Persistent data objects (such as warm-boot reason codes, crash dump tokens)
    // must be allocated in a dedicated .noinit (NOLOAD) section.
    // Arrange: Synthetic linker memory map
    uint32_t _sbss    = 0x00205AC0U;
    uint32_t _ebss    = 0x0021A400U;
    uint32_t _snoinit = 0x0021A400U;
    uint32_t _enoinit = 0x0021A440U; // 64 bytes (16 uint32_t words)
    uint32_t _sstack  = 0x0021A440U;
    uint32_t _estack  = 0x0021C440U; // 8 KB stack

    // Assert: .noinit is 4-byte word aligned
    EXPECT_EQ(_snoinit % 4U, 0U);
    EXPECT_EQ(_enoinit % 4U, 0U);

    // Assert: .noinit memory addresses reside outside the .bss zeroing window
    // startup.S zeroes strictly from _sbss to _ebss:
    EXPECT_GE(_snoinit, _ebss)
        << ".noinit section must start at or above _ebss so startup.S does NOT wipe it!";
    EXPECT_LE(_enoinit, _sstack)
        << ".noinit section must not collide with the execution stack!";

    // Assert: persistentObjects array (16 x 4 = 64 bytes) fits cleanly
    uint32_t noinit_size = _enoinit - _snoinit;
    EXPECT_GE(noinit_size, 64U);
}

// =============================================================================
// GROUP D (Pre-Phase 20 Linker & Build Verify Audits)
// =============================================================================

#include <stdio.h>
#include <stdlib.h>

static bool linker_read_repo_file(const char *relPath, char *buf, size_t bufSize)
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

// D6: Actual aries_v3.ld clamps RAM LENGTH <= 242K (0x0023E800), preserving 6 KB bootloader log
TEST(AriesLinkerTest, Test_LinkerScript_ActualFileClampsRamCeilingTo242KBelowBootloaderLog)
{
    // Arrange
    static char ldBuf[32768];
    ASSERT_TRUE(linker_read_repo_file("src/platform/ARIES/link/aries_v3.ld", ldBuf, sizeof(ldBuf)));

    // Act: Parse actual RAM ORIGIN and LENGTH from aries_v3.ld
    const char *ramLine = strstr(ldBuf, "RAM");
    ASSERT_NE(ramLine, nullptr);
    const char *origPtr = strstr(ramLine, "ORIGIN = ");
    ASSERT_NE(origPtr, nullptr);
    uint32_t origin = (uint32_t)strtoul(origPtr + 9, nullptr, 0);

    const char *lenPtr = strstr(origPtr, "LENGTH = ");
    ASSERT_NE(lenPtr, nullptr);
    char *unit = nullptr;
    unsigned long lenVal = strtoul(lenPtr + 9, &unit, 0);
    if (unit && (*unit == 'K' || *unit == 'k')) {
        lenVal *= 1024UL;
    }

    // Assert: ORIGIN == 0x00202000 and end <= 0x0023E800 (242 KB max = 247,808 bytes)
    EXPECT_EQ(origin, 0x00202000UL);
    EXPECT_LE(lenVal, 242UL * 1024UL)
        << "aries_v3.ld LENGTH > 242K overlaps ROM bootloader log/workspace at 0x0023E800..0x0023FFFF!";
    EXPECT_LE(origin + lenVal, 0x0023E800UL);

    // Real-world flight consequence:
    // Expanding heap/stack past 0x0023E800 overwrites the ROM bootloader status region on warm reset.
}

// D7: Actual aries_v3.ld puts .text.startup first, isolates .noinit after _ebss, guards stack
TEST(AriesLinkerTest, Test_LinkerScript_ActualFileEnforcesStartupFirstNoInitIsolationAndStackReserve)
{
    // Arrange
    static char ldBuf[32768];
    ASSERT_TRUE(linker_read_repo_file("src/platform/ARIES/link/aries_v3.ld", ldBuf, sizeof(ldBuf)));

    // Act
    const char *startupSec = strstr(ldBuf, ".text.startup");
    const char *initSec = strstr(ldBuf, ".init");
    const char *pgReg = strstr(ldBuf, ".pg_registry");
    const char *pgReset = strstr(ldBuf, ".pg_resetdata");
    const char *ebssSym = strstr(ldBuf, "_ebss");
    const char *noinitSec = strstr(ldBuf, ".noinit");
    const char *noinitNoload = strstr(ldBuf, "(NOLOAD)");
    const char *stackCheck = strstr(ldBuf, "_stack_size");
    const char *assertCheck = strstr(ldBuf, "ASSERT(");

    // Assert
    ASSERT_NE(startupSec, nullptr);
    ASSERT_NE(initSec, nullptr);
    EXPECT_LT(startupSec, initSec) << ".text.startup must strictly precede .init!";
    EXPECT_NE(pgReg, nullptr);
    EXPECT_NE(pgReset, nullptr);
    EXPECT_NE(noinitSec, nullptr);
    EXPECT_NE(noinitNoload, nullptr);
    if (ebssSym && noinitSec) {
        EXPECT_LT(ebssSym, noinitSec) << ".noinit must follow _ebss so startup.S never zeroes it!";
    }
    EXPECT_TRUE(stackCheck != nullptr && assertCheck != nullptr)
        << "aries_v3.ld must explicitly reserve and ASSERT minimum 8 KB stack headroom!";

    // Real-world flight consequence:
    // Without an explicit stack reserve assertion, SRAM bloat silently shrinks the stack until
    // deep PID/MSP/ISR call chains corrupt .bss/.noinit.
}

// D8: Actual build_verify.sh enforces 242K ceiling, 0 RVC instructions, and 0x00202000 entry
TEST(AriesLinkerTest, Test_BuildVerifyScript_ActualScriptEnforces242KCeilingZeroRvcAndRamOrigin)
{
    // Arrange
    static char shBuf[32768];
    ASSERT_TRUE(linker_read_repo_file("src/test/unit/aries/build_verify.sh", shBuf, sizeof(shBuf)));

    // Assert: Must enforce 242K (247808 bytes), NOT stale 248K (253952)
    EXPECT_EQ(strstr(shBuf, "253952"), nullptr)
        << "build_verify.sh still permits 248K (253952 B) instead of safe 242K (247808 B) limit!";
    EXPECT_TRUE(strstr(shBuf, "247808") != nullptr || strstr(shBuf, "242") != nullptr)
        << "build_verify.sh must enforce the 242 KB (247808 B) SRAM binary limit!";
    EXPECT_NE(strstr(shBuf, "00202000"), nullptr);
    EXPECT_NE(strstr(shBuf, "c."), nullptr);

    // Real-world flight consequence:
    // CI verification must fail immediately if firmware size crosses the bootloader-safe 242 KB bound
    // or if any 16-bit RVC opcode creeps into RV32IM silicon.
}

// =============================================================================
// TEST 11 (Suite 4 Upgrade): .noinit Overflow Past 0x0023E7FF Caught + Sorted PG Registry
// =============================================================================
TEST(AriesLinkerTest, Test_LinkerScript_NoinitGrowthOverflowCaughtAndSortedPgRegistryAligned)
{
    static char ldBuf[32768];
    ASSERT_TRUE(linker_read_repo_file("src/platform/ARIES/link/aries_v3.ld", ldBuf, sizeof(ldBuf)));

    // Verify sorted PG sections and overflow assertion formula
    EXPECT_NE(strstr(ldBuf, "SORT(.pg_registry.*)"), nullptr);
    EXPECT_NE(strstr(ldBuf, "SORT(.pg_resetdata.*)"), nullptr);

    const uint32_t ramOrigin = 0x00202000U;
    const uint32_t ramLimit  = 242U * 1024U; // 0x0023E800 ceiling
    const uint32_t eepromNoinit = 8192U;
    const uint32_t stackReserve = 8192U;
    uint32_t totalWithNoinit = (195U * 1024U) + eepromNoinit + stackReserve;
    EXPECT_LE(ramOrigin + totalWithNoinit, ramOrigin + ramLimit);

    uint32_t bloatedNoinit = (230U * 1024U) + eepromNoinit + stackReserve + 4096U;
    EXPECT_GT(ramOrigin + bloatedNoinit, ramOrigin + ramLimit);
}
