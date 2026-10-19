/*
 * Minimal startup code for ARIES V3 (THEJAS32) RISC-V target.
 * Sets up stack pointer, optionally global pointer, copies .data, clears .bss,
 * and jumps to main().
 */

    .section .text.startup, "ax", @progbits
    .global _start
    .type _start, @function
_start:
    /* Load stack pointer from the linker-defined _estack symbol */
    la sp, _estack

    /* Optionally set the global pointer if the symbol __global_pointer$ exists */
    #ifdef __global_pointer$
    la gp, __global_pointer$
    #endif

    /* Copy .data from flash (load address) to RAM */
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

    /* Zero .bss section */
    la t0, _sbss            /* start of .bss */
    la t1, _ebss            /* end of .bss */
zero_bss_loop:
    beq t0, t1, bss_done
    sw zero, 0(t0)          /* store zero (x0) */
    addi t0, t0, 4
    j zero_bss_loop
bss_done:

    /* Call main */
    call main

    /* If main returns, hang here */
1:  j 1b
