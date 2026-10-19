/*
 * Startup and Trap Vector handling for ARIES V3 (THEJAS32) RISC-V target.
 *
 * Sets up stack pointer, global pointer, copies .data, clears .bss,
 * configures trap vector (mtvec), and jumps to main().
 * Includes complete register save/restore trap entry and interrupt dispatcher.
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
    /* 1. Clear general purpose registers */
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

    /* 2. Load stack pointer from the linker-defined _estack symbol */
    la sp, _estack

    /* 3. Initialize global pointer if defined */
.option push
.option norelax
    la gp, __global_pointer$
.option pop

    /* 4. Set up machine trap vector */
    la t0, trap_entry
    csrw mtvec, t0

    /* 5. Copy .data from flash (load address) to RAM */
    la t0, _sidata          /* source address in flash */
    la t1, _sdata           /* destination address in RAM */
    la t2, _edata           /* end address of .data in RAM */
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

    /* 7. Call main */
    call main

    /* 8. If main returns, hang here in a documented loop */
1:  j 1b

    .size _start, . - _start

/* -----------------------------------------------------------------------------
 * Trap / Exception / Interrupt Entry Point
 * -----------------------------------------------------------------------------
 */
    .align 4
    .global trap_entry
    .type trap_entry, @function
trap_entry:
    /* Allocate stack frame for 32 registers (32 * 4 = 128 bytes, aligned to 16 -> 128 bytes) */
    addi sp, sp, -128

    SREG x1,  1*REGBYTES(sp)
    /* sp saved later if needed */
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

    /* Read trap cause and trap PC */
    csrr a0, mcause
    csrr a1, mepc
    mv   a2, sp

    /* Pass to C handler: handle_trap(mcause, mepc, sp) */
    jal handle_trap

    /* handle_trap returns new/resumed mepc in a0 */
    csrw mepc, a0

    /* Restore context */
    LREG x1,  1*REGBYTES(sp)
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

    addi sp, sp, 128
    mret

    .size trap_entry, . - trap_entry
