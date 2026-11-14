OUTPUT_ARCH("riscv")
ENTRY(_start)

MEMORY
{
    RAM (wxa!ri) : ORIGIN = 0x00202000, LENGTH = 248K
}

_Min_Stack_Size = 0x1000;

SECTIONS
{
    .init : {
        . = ALIGN(4);
        KEEP (*(SORT_NONE(.init)))
    } > RAM

    .text : {
        . = ALIGN(4);
        *(.text.startup*)
        *(.text*)
        . = ALIGN(4);
        _etext = .;
        *(.rodata*)
        *(.srodata*)
        . = ALIGN(4);
    } > RAM

    .pg_registry : {
        . = ALIGN(4);
        PROVIDE_HIDDEN (__pg_registry_start = .);
        KEEP (*(.pg_registry))
        KEEP (*(SORT(.pg_registry.*)))
        PROVIDE_HIDDEN (__pg_registry_end = .);
    } > RAM

    .pg_resetdata : {
        . = ALIGN(4);
        PROVIDE_HIDDEN (__pg_resetdata_start = .);
        KEEP (*(.pg_resetdata))
        KEEP (*(SORT(.pg_resetdata.*)))
        PROVIDE_HIDDEN (__pg_resetdata_end = .);
    } > RAM

    .data : {
        . = ALIGN(4);
        _sdata = .;
        *(.data*)
        *(.sdata*)
        . = ALIGN(4);
        _edata = .;
    } > RAM

    _sidata = LOADADDR(.data);

    .bss (NOLOAD) : {
        . = ALIGN(4);
        _sbss = .;
        *(.bss*)
        *(.sbss*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
    } > RAM

    .stack (NOLOAD) : {
        . = ALIGN(16);
        _sstack = .;
        . = . + 0x1400; /* 5 KB Stack (ample margin for Betaflight < 2 KB stack watermark) */
        _estack = .;
    } > RAM

    _end = .;
    PROVIDE(__global_pointer$ = _sdata + 0x800);
    PROVIDE(__bss_start = _sbss);
    PROVIDE(__bss_end = _ebss);

    ASSERT(_end <= (ORIGIN(RAM) + LENGTH(RAM)), "Error: Firmware image exceeds internal SRAM capacity (248 KB)!")
}

