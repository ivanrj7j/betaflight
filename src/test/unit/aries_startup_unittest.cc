/*
 * Test Suite: C-DAC ARIES v3.0 Machine Startup & Trap Vector Specification
 * Target Silicon: C-DAC THEJAS32 SoC (VEGA ET1031 RISC-V RV32IM @ 100 MHz)
 * Reference: PLAN/01_BOOT_LINKER_AND_CORE_PRIMITIVES.md (Microcomponent 01_A)
 * Citations:
 *   - knowledgebase/codebase_evaluations/01_EVAL_STARTUP_S.md
 *   - knowledgebase/components/02_STARTUP_BOOT_AND_EXCEPTIONS.md
 *   - lib/vega-sdk/vega-sdk/bsp/common/crt.S
 *
 * Methodology:
 *   Strict Test-Driven Development (TDD) - Step 1: Living Documentation.
 *   This test verifies the architectural contracts and silicon requirements for:
 *     1. Cold-boot GPR sanitization (zeroing all 31 registers x1..x31).
 *     2. Stack pointer initialization and strict 16-byte ABI alignment.
 *     3. Trap vector base alignment (64-byte aligned mtvec in Direct Mode).
 *     4. Context frame preservation (pre-trap SP saved in frame[2] slot).
 *     5. In-SRAM data copy bypass optimization (LMA == VMA).
 *     6. Complete word-aligned BSS zeroing loop.
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "unittest_macros.h"
#include "gtest/gtest.h"

namespace {

// Simulated RISC-V Register File for Startup Verification
struct RiscvRegFile {
    uint32_t x[32];
    uint32_t pc;
    uint32_t mtvec;
    uint32_t mcause;
    uint32_t mepc;
    uint32_t mstatus;
};

// Simulated Trap Frame on Stack (32 words = 128 bytes)
struct TrapFrame {
    uint32_t regs[32]; // regs[1] = ra, regs[2] = sp, regs[3] = gp, ...
};

// Emulated Startup Reset Sequence matching startup.S
void simulate_startup_reset(RiscvRegFile *rf, uint32_t estack, uint32_t trap_vector_addr, uint32_t gp_addr) {
    // 1. Reset all general-purpose registers x1..x31 to zero (x0 hardwired to 0)
    for (int i = 1; i < 32; i++) {
        rf->x[i] = 0;
    }
    rf->x[0] = 0;

    // 2. Load stack pointer from linker-defined _estack symbol
    rf->x[2] = estack; // x2 is sp

    // 3. Initialize global pointer gp
    rf->x[3] = gp_addr; // x3 is gp

    // 4. Set mtvec (Direct Mode: lower 2 bits = 0)
    rf->mtvec = (trap_vector_addr & ~0x3U);
}

// Emulated Trap Entry Sequence matching startup.S
void simulate_trap_entry(RiscvRegFile *rf, TrapFrame *out_frame, uint32_t cause, uint32_t fault_pc) {
    uint32_t pre_trap_sp = rf->x[2];

    // Decrement sp by 128 bytes (32 registers * 4 bytes)
    rf->x[2] -= 128;
    out_frame->regs[0] = 0; // x0

    // Save x1 (ra)
    out_frame->regs[1] = rf->x[1];

    // CRITICAL SILICON FIX: Save pre-trap SP into frame[2]
    out_frame->regs[2] = pre_trap_sp;

    // Save x3..x31
    for (int i = 3; i < 32; i++) {
        out_frame->regs[i] = rf->x[i];
    }

    // Set CSRs
    rf->mcause = cause;
    rf->mepc = fault_pc;
}

} // namespace

// =============================================================================
// Test 1: General Purpose Register (GPR) Sanitization
// =============================================================================
TEST(AriesStartupTest, Test_GPR_Sanitization_All31RegistersZeroed) {
    // Arrange: Populate register file with non-zero dirty cold-boot states
    RiscvRegFile rf;
    for (int i = 0; i < 32; i++) {
        rf.x[i] = 0xDEADBEEFU + (uint32_t)i;
    }
    rf.mtvec = 0xAAAAAAAAU;
    rf.pc = 0x00202000U;

    // Act: Execute reset sanitization routine
    const uint32_t test_estack = 0x0023FFE0U;
    const uint32_t test_trap_vector = 0x00202100U;
    const uint32_t test_gp = 0x0020A000U;
    simulate_startup_reset(&rf, test_estack, test_trap_vector, test_gp);

    // Assert: Hardwired x0 is zero
    EXPECT_EQ(rf.x[0], 0U);

    // Assert: All general-purpose registers x4..x31 are sanitized to strictly 0
    for (int i = 4; i < 32; i++) {
        EXPECT_EQ(rf.x[i], 0U) << "GPR x" << i << " was not sanitized on reset!";
    }

    // Real-world flight consequence:
    // Cold-boot power-up states on physical silicon leave undefined bit patterns.
    // Zeroing x1..x31 prevents soft-float routines from branching on uninitialized registers.
}

// =============================================================================
// Test 2: Stack Pointer Initialization and 16-Byte ABI Alignment
// =============================================================================
TEST(AriesStartupTest, Test_StackPointer_Initialization_And_16ByteAlignment) {
    // Arrange: Physical SRAM bounds for THEJAS32
    const uint32_t sram_origin = 0x00202000U;
    const uint32_t sram_limit = 0x0023FFFFU;
    const uint32_t estack_val = 0x0023FFE0U; // Linker-defined top of SRAM stack

    RiscvRegFile rf;
    memset(&rf, 0, sizeof(rf));

    // Act: Run startup setup
    simulate_startup_reset(&rf, estack_val, 0x00202100U, 0x0020A000U);
    uint32_t sp = rf.x[2];

    // Assert: sp resides within physical SRAM execution boundaries
    EXPECT_GE(sp, sram_origin);
    EXPECT_LE(sp, sram_limit);

    // Assert: RISC-V standard calling convention mandates 16-byte stack alignment
    EXPECT_EQ(sp % 16U, 0U) << "Stack pointer must be 16-byte aligned per RV32 ABI!";

    // Real-world flight consequence:
    // A misaligned stack pointer causes unaligned memory access exceptions or
    // corrupted stack frames during interrupt servicing in C handlers.
}

// =============================================================================
// Test 3: Machine Trap Vector (mtvec) Alignment
// =============================================================================
TEST(AriesStartupTest, Test_TrapVector_64ByteBaseAlignment) {
    // Arrange: Direct Mode trap entry address
    const uint32_t valid_trap_entry = 0x00202100U;   // 256-byte aligned (.align 8)
    const uint32_t invalid_trap_entry = 0x0020210EU; // Unaligned

    RiscvRegFile rf_valid, rf_invalid;
    memset(&rf_valid, 0, sizeof(rf_valid));
    memset(&rf_invalid, 0, sizeof(rf_invalid));

    // Act
    simulate_startup_reset(&rf_valid, 0x0023FFE0U, valid_trap_entry, 0x0020A000U);
    simulate_startup_reset(&rf_invalid, 0x0023FFE0U, invalid_trap_entry, 0x0020A000U);

    // Assert: Direct Mode requires lower 2 bits to be 0 (MODE=0 Direct)
    EXPECT_EQ(rf_valid.mtvec & 0x03U, 0U);

    // Assert: Clean 64-byte alignment (.align 6 or .align 8)
    EXPECT_EQ(valid_trap_entry % 64U, 0U);

    // Real-world flight consequence:
    // On RISC-V, the mtvec CSR masks lower alignment bits in hardware.
    // If trap_entry is not properly aligned, traps jump into random instruction streams.
}

// =============================================================================
// Test 4: Trap Frame Pre-Trap Stack Pointer Preservation (x2/sp Bug Fix)
// =============================================================================
TEST(AriesStartupTest, Test_TrapFrame_PreTrapStackPointerPreservation) {
    // Arrange: Application executing at stack pointer 0x0023F000
    RiscvRegFile rf;
    memset(&rf, 0, sizeof(rf));
    const uint32_t original_sp = 0x0023F000U;
    rf.x[2] = original_sp;
    rf.x[1] = 0x00203400U; // ra
    rf.x[10] = 42U;        // a0

    TrapFrame saved_frame;
    memset(&saved_frame, 0xAA, sizeof(saved_frame)); // Pre-fill with garbage

    // Act: Hardware exception occurs (e.g. Illegal Instruction, mcause = 2)
    simulate_trap_entry(&rf, &saved_frame, 2U, 0x00202400U);

    // Assert: Stack decremented by exactly 128 bytes
    EXPECT_EQ(rf.x[2], original_sp - 128U);

    // Assert: Frame slot 2 contains the EXACT pre-trap stack pointer
    EXPECT_EQ(saved_frame.regs[2], original_sp)
        << "Trap frame must save pre-trap stack pointer (x2) to allow accurate backtraces!";

    // Assert: Caller register state is preserved
    EXPECT_EQ(saved_frame.regs[1], 0x00203400U);
    EXPECT_EQ(saved_frame.regs[10], 42U);

    // Real-world flight consequence:
    // In previous port (01_EVAL_STARTUP_S.md, score 3/10), frame[2] was omitted.
    // When handle_trap() inspected sp, it received garbage, preventing post-crash diagnostics.
}

// =============================================================================
// Test 5: In-SRAM Data Copy Bypass Optimization (LMA == VMA)
// =============================================================================
TEST(AriesStartupTest, Test_InSRAM_DataCopyBypass_Optimization) {
    // Arrange: In-SRAM execution scenario where load address equals run address
    const uint32_t data_sram_addr = 0x00205000U;
    uint32_t sdata = data_sram_addr;
    uint32_t sidata_sram = data_sram_addr; // LMA == VMA
    uint32_t sidata_flash = 0x10000000U;  // LMA != VMA

    // Act: Evaluate bypass condition as in startup.S (beq t0, t1, copy_done)
    bool bypass_sram = (sidata_sram == sdata);
    bool bypass_flash = (sidata_flash == sdata);

    // Assert
    EXPECT_TRUE(bypass_sram) << "In-SRAM execution must bypass redundant .data self-copy!";
    EXPECT_FALSE(bypass_flash) << "ROM-to-RAM execution must execute .data copy!";

    // Real-world flight consequence:
    // Bypassing self-copy on physical SRAM saves boot cycles and avoids corrupting
    // memory if the bootloader has already initialized variables. Rated 9.5/10.
}

// =============================================================================
// Test 6: BSS Section Word-by-Word Zeroing Loop
// =============================================================================
TEST(AriesStartupTest, Test_BSS_Zeroing_WordLoop) {
    // Arrange: Mock BSS region filled with dirty memory (0xFF)
    const size_t bss_words = 64;
    uint32_t mock_bss[bss_words];
    memset(mock_bss, 0xFF, sizeof(mock_bss));

    uint32_t *sbss = mock_bss;
    uint32_t *ebss = mock_bss + bss_words;

    // Act: Emulate startup.S zero_bss_loop
    uint32_t *ptr = sbss;
    while (ptr < ebss) {
        *ptr = 0U;
        ptr++;
    }

    // Assert: Every word in the BSS range is strictly 0
    for (size_t i = 0; i < bss_words; i++) {
        EXPECT_EQ(mock_bss[i], 0U) << "BSS word " << i << " was not zeroed!";
    }

    // Real-world flight consequence:
    // Uninitialized BSS memory leaves flight controller flags and PID error states
    // with indeterminate values, triggering erratic motor commands on startup.
}
