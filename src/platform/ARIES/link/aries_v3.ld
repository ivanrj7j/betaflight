OUTPUT_ARCH("riscv")
ENTRY(main)

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x20000000, LENGTH = 2048K
    RAM   (rwx) : ORIGIN = 0x10000000, LENGTH = 256K
}

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
}
