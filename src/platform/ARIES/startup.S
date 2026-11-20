/*
 * Startup and Trap Vector handling for C-DAC ARIES v3.0 (THEJAS32) RISC-V target.
 * Architecture: RV32IM (strictly pure 32-bit, zero compressed instructions).
 *
 * Ground Truths Enforced:
 *   - Ground Truth 1: RV32IM only, no RVC instructions.
 *   - Ground Truth 2: In-SRAM execution starting at 0x00202000.
 *
 * Features:
 *   1. Complete 31-GPR sanitization (zeroing x1..x31 on power-on reset).
 *   2. Direct stack pointer initialization (_estack) with 16-byte alignment.
 *   3. Global pointer (gp) setup with linker relaxation safety.
 *   4. Machine trap vector base alignment (.align 6, 64-byte aligned).
 *   5. In-SRAM data copy bypass optimization (LMA == VMA).
 *   6. Word-aligned BSS zeroing loop.
 *   7. Trap frame context preservation: pre-trap sp saved into frame[2].
 *   8. Return to Machine Mode (MSTATUS_MPP = 3) on mret.
 *   9. Fail-safe low-power halt loop (wfi) if main() exits.
 */

#if __riscv_xlen == 64
# define LREG ld
# define SREG sd
# define REGBYTES 8
#else
# define LREG lw
# define SREG sw
# define REGBYTES 4
#endif

    .section .text.startup, "ax", @progbits
    .global _start
    .type _start, @function

_start:
    /* 1. Clear general purpose registers x1..x31 (x0 is hardwired to zero) */
    li  x1, 0
    li  x2, 0
    li  x3, 0
    li  x4, 0
    li  x5, 0
    li  x6, 0
    li  x7, 0
    li  x8, 0
    li  x9, 0
    li  x10, 0
    li  x11, 0
    li  x12, 0
    li  x13, 0
    li  x14, 0
    li  x15, 0
    li  x16, 0
    li  x17, 0
    li  x18, 0
    li  x19, 0
    li  x20, 0
    li  x21, 0
    li  x22, 0
    li  x23, 0
    li  x24, 0
    li  x25, 0
    li  x26, 0
    li  x27, 0
    li  x28, 0
    li  x29, 0
    li  x30, 0
    li  x31, 0

    /* 2. Load stack pointer from linker-defined _estack symbol */
    la sp, _estack

    /* 3. Initialize global pointer with linker relaxations disabled */
.option push
.option norelax
    la gp, __global_pointer$
.option pop

    /* 4. Set up machine trap vector (Direct Mode: lower 2 bits = 0) */
    la t0, trap_entry
    csrw mtvec, t0

    /* 5. Copy .data from flash (load address) to RAM */
    la t0, _sidata          /* source address in flash */
    la t1, _sdata           /* destination address in RAM */
    la t2, _edata           /* end address of .data in RAM */
    beq t0, t1, copy_done   /* In-SRAM execution optimization: bypass if LMA == VMA */
copy_data_loop:
    beq t1, t2, copy_done
    lw t3, 0(t0)
    sw t3, 0(t1)
    addi t0, t0, 4
    addi t1, t1, 4
    j copy_data_loop
copy_done:

    /* 6. Zero .bss section */
    la t0, _sbss            /* start of .bss */
    la t1, _ebss            /* end of .bss */
zero_bss_loop:
    beq t0, t1, bss_done
    sw zero, 0(t0)          /* store zero (x0) */
    addi t0, t0, 4
    j zero_bss_loop
bss_done:

    /* 7. Call main entry point */
    call main

    /* 8. If main ever returns, enter a fail-safe low-power halt loop */
1:  wfi
    j 1b

    .size _start, . - _start

/* -----------------------------------------------------------------------------
 * Trap / Exception / Interrupt Entry Point
 * Direct Mode alignment requirement: 64-byte boundary (.align 6)
 * -----------------------------------------------------------------------------
 */
    .align 6
    .global trap_entry
    .type trap_entry, @function
trap_entry:
    /* Allocate stack frame for 32 registers (32 * 4 = 128 bytes, 16-byte aligned) */
    addi sp, sp, -128

    /* Save caller-saved and callee-saved registers */
    SREG x1,  1*REGBYTES(sp)

    /* CRITICAL FIX: Calculate and save original pre-trap stack pointer (x2) into frame[2] */
    addi t0, sp, 128
    SREG t0,  2*REGBYTES(sp)

    SREG x3,  3*REGBYTES(sp)
    SREG x4,  4*REGBYTES(sp)
    SREG x5,  5*REGBYTES(sp)
    SREG x6,  6*REGBYTES(sp)
    SREG x7,  7*REGBYTES(sp)
    SREG x8,  8*REGBYTES(sp)
    SREG x9,  9*REGBYTES(sp)
    SREG x10, 10*REGBYTES(sp)
    SREG x11, 11*REGBYTES(sp)
    SREG x12, 12*REGBYTES(sp)
    SREG x13, 13*REGBYTES(sp)
    SREG x14, 14*REGBYTES(sp)
    SREG x15, 15*REGBYTES(sp)
    SREG x16, 16*REGBYTES(sp)
    SREG x17, 17*REGBYTES(sp)
    SREG x18, 18*REGBYTES(sp)
    SREG x19, 19*REGBYTES(sp)
    SREG x20, 20*REGBYTES(sp)
    SREG x21, 21*REGBYTES(sp)
    SREG x22, 22*REGBYTES(sp)
    SREG x23, 23*REGBYTES(sp)
    SREG x24, 24*REGBYTES(sp)
    SREG x25, 25*REGBYTES(sp)
    SREG x26, 26*REGBYTES(sp)
    SREG x27, 27*REGBYTES(sp)
    SREG x28, 28*REGBYTES(sp)
    SREG x29, 29*REGBYTES(sp)
    SREG x30, 30*REGBYTES(sp)
    SREG x31, 31*REGBYTES(sp)

    /* Read trap cause, trap PC, and frame pointer */
    csrr a0, mcause
    csrr a1, mepc
    mv   a2, sp

    /* Pass to C handler: handle_trap(mcause, mepc, sp) */
    jal handle_trap

    /* handle_trap returns new/resumed mepc in a0 */
    csrw mepc, a0

    /* Remain in Machine Mode after mret (MSTATUS_MPP = 3 << 11 = 0x1800) */
    li t0, 0x1800
    csrs mstatus, t0

    /* Restore context */
    LREG x1,  1*REGBYTES(sp)
    /* sp (x2) restored via stack pointer increment below */
    LREG x3,  3*REGBYTES(sp)
    LREG x4,  4*REGBYTES(sp)
    LREG x5,  5*REGBYTES(sp)
    LREG x6,  6*REGBYTES(sp)
    LREG x7,  7*REGBYTES(sp)
    LREG x8,  8*REGBYTES(sp)
    LREG x9,  9*REGBYTES(sp)
    LREG x10, 10*REGBYTES(sp)
    LREG x11, 11*REGBYTES(sp)
    LREG x12, 12*REGBYTES(sp)
    LREG x13, 13*REGBYTES(sp)
    LREG x14, 14*REGBYTES(sp)
    LREG x15, 15*REGBYTES(sp)
    LREG x16, 16*REGBYTES(sp)
    LREG x17, 17*REGBYTES(sp)
    LREG x18, 18*REGBYTES(sp)
    LREG x19, 19*REGBYTES(sp)
    LREG x20, 20*REGBYTES(sp)
    LREG x21, 21*REGBYTES(sp)
    LREG x22, 22*REGBYTES(sp)
    LREG x23, 23*REGBYTES(sp)
    LREG x24, 24*REGBYTES(sp)
    LREG x25, 25*REGBYTES(sp)
    LREG x26, 26*REGBYTES(sp)
    LREG x27, 27*REGBYTES(sp)
    LREG x28, 28*REGBYTES(sp)
    LREG x29, 29*REGBYTES(sp)
    LREG x30, 30*REGBYTES(sp)
    LREG x31, 31*REGBYTES(sp)

    /* Deallocate stack frame and return from trap */
    addi sp, sp, 128
    mret

    .size trap_entry, . - trap_entry
