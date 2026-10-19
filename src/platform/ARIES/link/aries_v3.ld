OUTPUT_ARCH("riscv")
ENTRY(_start)

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x20000000, LENGTH = 2048K
    RAM   (rwx) : ORIGIN = 0x10000000, LENGTH = 256K
}

_Min_Stack_Size = 0x800;

/* Base address where configuration is stored in Flash (last 32KB of 2MB flash) */
__config_start = 0x20000000 + 2048K - 32K;
__config_end   = 0x20000000 + 2048K;

SECTIONS
{
    .text : {
        . = ALIGN(4);
        *(.text.startup*)
        *(.text*)
        *(.rodata*)
        *(.srodata*)
        . = ALIGN(4);
        _etext = .;
    } > FLASH

    .pg_registry : {
        . = ALIGN(4);
        PROVIDE_HIDDEN (__pg_registry_start = .);
        KEEP (*(.pg_registry))
        KEEP (*(SORT(.pg_registry.*)))
        PROVIDE_HIDDEN (__pg_registry_end = .);
    } > FLASH

    .pg_resetdata : {
        . = ALIGN(4);
        PROVIDE_HIDDEN (__pg_resetdata_start = .);
        KEEP (*(.pg_resetdata))
        KEEP (*(SORT(.pg_resetdata.*)))
        PROVIDE_HIDDEN (__pg_resetdata_end = .);
    } > FLASH

    .data : {
        . = ALIGN(4);
        _sdata = .;
        *(.data*)
        *(.sdata*)
        . = ALIGN(4);
        _edata = .;
    } > RAM AT > FLASH

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
        . = . + 0x4000;
        _estack = .;
    } > RAM

    _end = .;
    PROVIDE(__global_pointer$ = _sdata + 0x800);
    PROVIDE(__bss_start = _sbss);
    PROVIDE(__bss_end = _ebss);
}

