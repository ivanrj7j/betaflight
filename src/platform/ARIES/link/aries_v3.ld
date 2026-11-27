/*
 * C-DAC ARIES v3.0 Flight Controller Linker Script
 * Target Silicon: C-DAC THEJAS32 SoC (VEGA ET1031 RISC-V RV32IM @ 100 MHz)
 * Silicon Memory: 256 KB internal SRAM (0x00200000 - 0x0023FFFF)
 * Boot Partition: First 8 KB (0x00200000 - 0x00201FFF) reserved by on-chip ROM bootloader
 * Firmware Budget: Exactly 248 KB (253,952 bytes) spanning 0x00202000 to 0x0023FFFF
 * Execution Mode: In-SRAM Execution (No XIP flash mapping)
 *
 * Citations & Evaluations:
 *   - knowledgebase/codebase_evaluations/02_EVAL_LINKER_ARIES_V3_LD.md
 *   - knowledgebase/components/09_BUILD_LINKER_AND_MEMORY.md
 *   - logs/hardware_inspection/12_STM32_LINKER_AND_SECTIONS.md
 *   - logs/hardware_inspection/25_ESP32_BUILD_AND_LINKER.md
 *   - lib/vega-sdk/vega-sdk/bsp/common/mbl.lds
 *   - lib/vega-bsp/thejas32/link1.lds
 */

OUTPUT_ARCH("riscv")
ENTRY(_start)

/*
 * Memory Region Definition:
 * Usable SRAM starts at 0x00202000 directly above the ROM bootloader partition.
 * Clamped to 242 KB (0x3C800 bytes, top 0x0023E7FF) preserving 6 KB ROM bootloader workspace.
 */
MEMORY
{
    RAM (wxa!ri) : ORIGIN = 0x00202000, LENGTH = 242K
}

SECTIONS
{
    /*
     * Executable Code Section:
     * CRITICAL FIX: KEEP(*(.text.startup*)) MUST be the very first block in .text.
     * Placing .text.startup ahead of .init guarantees that _start is anchored exactly
     * at ORIGIN(RAM) (0x00202000), preventing library .init code from usurping the reset vector.
     */
    .text : {
        . = ALIGN(4);
        _stext = .;
        KEEP(*(.text.startup*))
        KEEP(*(SORT_NONE(.init)))
        *(.text .text.*)
        *(.gnu.linkonce.t.*)
        . = ALIGN(4);
        _etext = .;
    } > RAM

    /*
     * Read-Only Data Section:
     * Separated from .text to enable clean memory profiling and footprint analysis.
     */
    .rodata : {
        . = ALIGN(4);
        _srodata = .;
        *(.rodata .rodata.*)
        *(.srodata .srodata.*)
        *(.rdata)
        *(.gnu.linkonce.r.*)
        . = ALIGN(4);
        _erodata = .;
    } > RAM

    /*
     * Betaflight Parameter Group Registry Section:
     * Distributed configuration descriptors registered via PG_REGISTER().
     * Kept from linker garbage collection and 4-byte word aligned.
     */
    .pg_registry : {
        . = ALIGN(4);
        PROVIDE_HIDDEN (__pg_registry_start = .);
        KEEP (*(.pg_registry))
        KEEP (*(SORT(.pg_registry.*)))
        PROVIDE_HIDDEN (__pg_registry_end = .);
    } > RAM

    /*
     * Betaflight Parameter Group Reset Defaults Section:
     * Factory default initialization templates for registered parameter groups.
     */
    .pg_resetdata : {
        . = ALIGN(4);
        PROVIDE_HIDDEN (__pg_resetdata_start = .);
        KEEP (*(.pg_resetdata))
        KEEP (*(SORT(.pg_resetdata.*)))
        PROVIDE_HIDDEN (__pg_resetdata_end = .);
    } > RAM

    /*
     * Initialized Data Section:
     * VMA and LMA are identical in in-SRAM execution mode.
     */
    .data : {
        . = ALIGN(4);
        _sdata = .;
        *(.data .data.*)
        *(.sdata .sdata.*)
        *(.gnu.linkonce.d.*)
        . = ALIGN(4);
        _edata = .;
    } > RAM

    _sidata = LOADADDR(.data);

    /*
     * Uninitialized Data (BSS) Section:
     * Zeroed on boot by startup.S. Includes in-RAM simulated EEPROM (CONFIG_IN_RAM, 8192 bytes).
     */
    .bss (NOLOAD) : {
        . = ALIGN(4);
        _sbss = .;
        *(.bss .bss.*)
        *(.sbss .sbss.*)
        *(.gnu.linkonce.b.*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
    } > RAM

    /*
     * Persistent Non-Initialized Data (.noinit) Section:
     * Survives warm soft resets; explicitly NOT cleared by startup.S bss zeroing loop.
     */
    .noinit (NOLOAD) : {
        . = ALIGN(4);
        _snoinit = .;
        *(.noinit .noinit.*)
        . = ALIGN(4);
        _enoinit = .;
    } > RAM

    /*
     * Dedicated Execution Stack Section:
     * 8 KB (0x2000 bytes) reserved headroom for deep nested interrupts (Timer 0, Timer 1, UART RX)
     * and soft-float trigonometric calculations. 16-byte aligned per RV32 ABI.
     */
    .stack (NOLOAD) : {
        . = ALIGN(16);
        _sstack = .;
        . = . + 0x2000;
        . = ALIGN(16);
        _estack = .;
    } > RAM

    /*
     * Global Boundary & Pointer Symbols:
     */
    _end = .;
    _stack_size = _estack - _sstack;
    PROVIDE(_Min_Stack_Size = 0x2000);
    PROVIDE(__global_pointer$ = _sdata + 0x800);
    PROVIDE(__bss_start = _sbss);
    PROVIDE(__bss_end = _ebss);

    /*
     * Compile-Time SRAM & Stack Budget Guardrails:
     * Prevents image overflow into the 0x0023E800..0x0023FFFF ROM bootloader region.
     */
    ASSERT(_stack_size >= 0x2000, "FATAL: Reserved stack size fell below 8 KB minimum!")
    ASSERT(_end <= (ORIGIN(RAM) + LENGTH(RAM)), "FATAL: Total firmware image + stack exceeds 242 KB bootloader-safe SRAM!")
}
