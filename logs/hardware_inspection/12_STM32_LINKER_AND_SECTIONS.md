# Hardware Inspection Log: `src/platform/STM32/link/stm32_flash_f411.ld` & `stm32_flash_f4_split.ld`

## 1. File Purpose & Role in Architecture
* **File Path**: `src/platform/STM32/link/stm32_flash_f411.ld` and `stm32_flash_f4_split.ld`
* **Subsystem**: Physical Memory Mapping, Sector Splitting, Section Link Order, and Hot Reboot Preservation.
* **Key Components**:
  * Memory partitions: `FLASH` (16K), `FLASH_CONFIG` (16K), `FLASH1` (480K), `RAM` (128K), `SYSTEM_MEMORY` (29K ST ROM).
  * `_Hot_Reboot_Flags_Size`: 16-byte buffer protected at the peak of SRAM.
  * Section layout: `.isr_vector` $\rightarrow$ `.text` $\rightarrow$ `.rodata` $\rightarrow$ `.data` $\rightarrow$ `.bss` $\rightarrow$ `STACK`.

---

## 2. Hardware Challenges STM32 Developers Faced & How They Overcame Them

### Challenge 1: Flash Sector Erase Boundary Collisions
* **The Problem**: In STM32 internal NOR flash, erase operations can only occur on entire sectors. On STM32F4, sectors have uneven sizes:
  * Sector 0: 16 KB
  * Sector 1: 16 KB
  * Sector 2: 16 KB
  * Sector 3: 16 KB
  * Sector 4: 64 KB
  * Sector 5: 128 KB
  If firmware code and user settings (PID tunes, rates) shared the same sector, saving settings in Configurator would erase the firmware code!
* **How It Was Overcome**:
  1. Developers split the flash layout across sector boundaries:
     * `FLASH` (`0x08000000` to `0x08003FFF`, 16 KB): Sector 0 for vector table and startup code.
     * `FLASH_CONFIG` (`0x08004000` to `0x08007FFF`, 16 KB): Sector 1 dedicated exclusively to configuration data (`ParameterGroups`).
     * `FLASH1` (`0x08008000` to `0x0807FFFF`, 480 KB): Sector 2 onwards for remaining flight firmware.
  2. To avoid wasting unused bytes in Sector 0, they aliased `REGION_ALIAS("MOVABLE_FLASH", FLASH)` to pack exception index tables (`.ARM.exidx`) into the empty space behind the vector table.

---

### Challenge 2: Protecting Reboot Reason Tokens from Stack Corruption
* **The Problem**: The Main Stack Pointer starts at the top of RAM (`ORIGIN(RAM) + LENGTH(RAM)`) and grows downwards. If `persistentObjectWrite()` writes a reboot reason token at the top of RAM, the initial function calls after reset immediately overwrite and corrupt the token!
* **How It Was Overcome**:
  The linker script offsets the stack pointer by 16 bytes:
  ```ld
  _Hot_Reboot_Flags_Size = 16;
  _estack = ORIGIN(STACKRAM) + LENGTH(STACKRAM) - _Hot_Reboot_Flags_Size;
  ```
  The stack starts 16 bytes below the top of RAM, guaranteeing the reserved reboot flags remain safe from push/call operations.

---

## 3. Direct Porting Blueprint for ARIES v3.0 (THEJAS32 / VEGA ET1031)

1. **SRAM Execution Linker Script (`linker_ram.lds`)**:
   * For the immediate goal of compiling <128KB firmware and loading via XMODEM into internal RAM:
     ```ld
     OUTPUT_ARCH(riscv)
     ENTRY(_start)

     MEMORY
     {
         /* 250 KB Internal SRAM mapped by C-DAC bootloader */
         RAM (rwx) : ORIGIN = 0x00200000, LENGTH = 250K
     }

     SECTIONS
     {
         . = 0x00200000;

         .text.init : {
             KEEP(*(.text.init)) /* crt.S entry point */
         } > RAM

         .text : {
             *(.text*)
         } > RAM

         .rodata : {
             *(.rodata*)
         } > RAM

         .data : {
             _sdata = .;
             *(.data*)
             _edata = .;
         } > RAM

         .bss : {
             _sbss = .;
             *(.bss*)
             *(COMMON)
             _ebss = .;
         } > RAM

         . = ALIGN(8);
         _heap_stack_begin = .;
         . = ORIGIN(RAM) + LENGTH(RAM) - 16;
         _heap_stack_end = .;
         _sp = .;
     }
     ```
2. **SPI Flash Persistence**:
   * The 2 MB SPI Flash (`U5`) uses standard 4 KB uniform sectors.
   * Unlike STM32’s irregular 16K/128K sectors, configuration pages can simply occupy the final 64 KB sector of SPI flash (`0x1F0000` to `0x200000`).
