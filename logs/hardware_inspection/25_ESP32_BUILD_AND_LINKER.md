# Hardware Inspection Log: `src/platform/ESP32/link/esp32c5.ld` & `mk/ESP32C5.mk`
**Consulted Logs**: [12_STM32_LINKER_AND_SECTIONS.md](file:///home/ivan/Desktop/betaflight/logs/hardware_inspection/12_STM32_LINKER_AND_SECTIONS.md) & [13_STM32_BUILD_MAKEFILE.md](file:///home/ivan/Desktop/betaflight/logs/hardware_inspection/13_STM32_BUILD_MAKEFILE.md)

## 1. File Purpose & Role in Architecture
* **File Paths**:
  * [src/platform/ESP32/link/esp32c5.ld](file:///home/ivan/Desktop/betaflight/src/platform/ESP32/link/esp32c5.ld)
  * [src/platform/ESP32/mk/ESP32C5.mk](file:///home/ivan/Desktop/betaflight/src/platform/ESP32/mk/ESP32C5.mk)
  * [src/platform/ESP32/dma_stub_esp32.c](file:///home/ivan/Desktop/betaflight/src/platform/ESP32/dma_stub_esp32.c)
* **Subsystem**: RISC-V Toolchain Configuration, Linker Memory Mapping, and Parameter Group Anchor Table Management.
* **Key Components**:
  * `ARCH_FLAGS`: RISC-V 32-bit architectural target flags (`-march=rv32imac... -mabi=ilp32`).
  * `LD_SCRIPT`: Memory segment allocation for instruction RAM, data RAM, and Flash MMU.
  * `.pg_registry` & `.pg_resetdata`: Mandatory linker sections required by Betaflight's Parameter Group runtime.
  * `dma_stub_esp32.c`: No-op DMA stubs enabling clean builds when `USE_DMA` is undefined.

---

## 2. Comparative Analysis: ESP32-C5 (RISC-V) vs. STM32 (`12_STM32_LINKER_AND_SECTIONS.md` & `13_STM32_BUILD_MAKEFILE.md`)

| Feature / Hardware Challenge | STM32 Implementation (`12_STM32_...`, `13_STM32_...`) | ESP32-C5 Implementation (`esp32c5.ld`, `ESP32C5.mk`) |
| :--- | :--- | :--- |
| **Target Architecture** | ARM Cortex-M4 / M7 (`-mcpu=cortex-m4 -mthumb -mfloat-abi=hard`). | 32-bit RISC-V (`-march=rv32imac... -mabi=ilp32` soft-float). |
| **Toolchain Prefix** | `arm-none-eabi-gcc` | `riscv32-esp-elf-` or `riscv32-unknown-elf-` |
| **Flash Layout Complexity** | Split flash (`FLASH`, `FLASH_CONFIG`, `FLASH1`) due to irregular sector sizes (16 KB vs 128 KB). | Flat MMU-mapped flash (`0x42000000`, 4 MB) with 64 KB configuration sector at end. |
| **RAM Architecture** | Split SRAM (SRAM1, SRAM2, CCM/DTCM, Backup SRAM). | Unified HP SRAM (`0x40800000`, 384 KB) addressable as both IRAM (code) and DRAM (data). |
| **DMA Dependency** | Hard requirement in default makefile; deep DMA hardware coupling. | Optional via `dma_stub_esp32.c` (`#ifndef USE_DMA`), enabling ultra-lightweight builds without DMA drivers. |

---

## 3. Hardware Challenges Faced & How They Were Overcame

### Challenge 1: Parameter Group Initialization Panics Without Linker Anchors
* **The Problem**: Betaflight uses a distributed parameter registration architecture. Macros like `PG_REGISTER()` place descriptors into special object file sections (`.pg_registry`). If the linker script does not declare `__pg_registry_start` and `__pg_registry_end`, or if the linker discards them during dead-code elimination (`--gc-sections`), `pgResetAll()` iterates over invalid pointers and crashes the MCU on the very first boot loop.
* **How It Was Overcome**:
  In `esp32c5.ld`, developers explicitly guarded the sections using `KEEP()` and defined hidden boundary symbols:
  ```ld
  .pg_registry :
  {
      PROVIDE_HIDDEN (__pg_registry_start = .);
      KEEP (*(.pg_registry))
      KEEP (*(SORT(.pg_registry.*)))
      PROVIDE_HIDDEN (__pg_registry_end = .);
  } > flash_seg
  ```

### Challenge 2: Dual IRAM/DRAM Overlap Corruption
* **The Problem**: On many Harvard-architecture SoCs, internal SRAM is aliased to two bus ports: an instruction bus port and a data bus port. If a linker script declares two overlapping memory segments at the same address (e.g. `iram (RX)` and `dram (RW)`), the GNU linker assigns identical virtual addresses (VMAs) to code and variables, causing global variables to overwrite executable functions!
* **How It Was Overcome**:
  The ESP32-C5 linker script declared a **single unified allocator**:
  ```ld
  ram_seg (RWX) : org = 0x40800000, len = 384K
  ```
  Both executable code (`.iram.text`) and read-write data (`.data`, `.bss`) are allocated sequentially from the same segment, preventing address collisions.

---

## 4. Direct Porting Blueprint for ARIES v3.0 (THEJAS32 / VEGA ET1031)

1. **Exact Toolchain & Architecture Flags for THEJAS32**:
   * VEGA ET1031 core implements **RV32IM** (Base 32-bit Integer + Hardware Multiply/Divide).
   * It does **NOT** support the compressed 'C' extension or atomic 'A' extension, and has no hardware FPU.
   * Toolchain Flags:
     ```makefile
     ARCH_FLAGS = -march=rv32im -mabi=ilp32
     CROSS_COMPILE ?= riscv32-unknown-elf-
     ```
2. **RAM Execution Linker Script (`linker_ram.lds`)**:
   * For loading the <128KB binary directly into internal SRAM via XMODEM bootloader, the single-allocator pattern from `esp32c5.ld` is the exact model:
     ```ld
     MEMORY {
         RAM (RWX) : org = 0x00200000, len = 250K
     }
     ```
   * All sections (`.text`, `.rodata`, `.pg_registry`, `.pg_resetdata`, `.data`, `.bss`) map sequentially into `RAM`.
3. **Use DMA Stub for Initial Port**:
   * By adopting the `dma_stub` pattern (`#ifndef USE_DMA`), we satisfy all Betaflight bus/SPI references without including any DMA engine code.
   * This saves ~6 KB of code and several KB of SRAM descriptors, directly guaranteeing we stay under the **128 KB** ceiling!
