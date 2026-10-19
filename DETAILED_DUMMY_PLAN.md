# Detailed Plan: Minimal "Dummy / Stub" Compilation for ARIES v3.0

## 1. Objective & Scope

The purpose of this phase is **not** to produce a flight-ready binary immediately, but to achieve a clean **end-to-end compilation and linking** of the Betaflight core for the **C-DAC ARIES v3.0 (THEJAS32 / VEGA ET1031 RISC-V)** platform.

### Goals
1. Verify that Betaflight's shared C codebase (scheduler, PID control algorithms, attitude estimation math, filtering, CLI, MSP protocol) compiles without error under the **RISC-V RV32IM** architecture.
2. Confirm that the GCC soft-float library (`libgcc.a` / `libm.a`) successfully resolves all floating-point math symbols (`__addsf3`, `__mulsf3`, `__divsf3`, `sinf`, `sqrtf`, etc.).
3. Prove that the toolchain emits a valid 32-bit RISC-V ELF executable (`obj/betaflight_ARIESV3.elf`) and check its memory footprint against the 256 KB SRAM / 2 MB Flash limits.

### Non-Goals (Deferred to Subsequent Phases)
* Writing real peripheral registers for SPI, UART, I2C, Timers, or Flash.
* Flashing hardware or bench testing motors.

---

## 2. Toolchain & Environment Specifications

* **Compiler:** `riscv32-esp-elf-gcc` (GCC 13.2.0)
* **Path:** `/home/ivan/.espressif/tools/riscv32-esp-elf/esp-13.2.0_20250707/riscv32-esp-elf/bin/riscv32-esp-elf-`
* **Architecture Flags:**
  * `-march=rv32im` (RV32 integer base + hardware multiply/divide extension)
  * `-mabi=ilp32` (Standard 32-bit soft-float ABI)
  * `-mcmodel=medany` (Allows addressing within +/- 2GB range)
* **Optimization Profile:**
  * `-O2 -ffast-math -fno-math-errno -fdata-sections -ffunction-sections`
* **Linker Flags:**
  * `-Wl,--gc-sections -Wl,-Map,obj/betaflight_ARIESV3.map -T src/platform/ARIES/link/aries_v3.ld`

---

## 3. Architecture & File Breakdown

To integrate with Betaflight's top-level `Makefile`, all files will reside in `src/platform/ARIES/`:

```
src/platform/ARIES/
├── mk/
│   └── ARIES.mk                       # Build flags, include paths, source lists
├── target/
│   └── ARIESV3/
│       ├── target.mk                  # Target identification (TARGET_MCU, TARGET_MCU_FAMILY)
│       └── target.h                   # Board feature configuration and pin stub definitions
├── link/
│   └── aries_v3.ld                    # Linker script mapping 256KB SRAM and 2MB Flash
├── include/
│   └── platform/
│       ├── platform.h                 # Memory attributes (FAST_CODE), endianness, types
│       └── timer_def.h                # Hardware timer stubs
├── io_def_generated.h                 # GPIO pin definitions stub
├── dma_reqmap_mcu.h                   # DMA mapping stubs
├── system.c                           # Clock init, mcycle CSR counter, micros() timebase
└── stub_drivers.c                     # Dummy implementations of required HAL driver hooks
```

---

## 4. Detailed Step-by-Step Implementation

### Step 1: Target Definition (`target.mk` & `target.h`)
* **`src/platform/ARIES/target/ARIESV3/target.mk`**:
  Specifies the target variables expected by Betaflight's root `Makefile`:
  ```makefile
  TARGET_MCU        := THEJAS32
  TARGET_MCU_FAMILY := ARIES
  MCU_FLASH_SIZE    := 2048
  ```
* **`src/platform/ARIES/target/ARIESV3/target.h`**:
  * Define hardware limits: 100 MHz clock (`SystemCoreClock = 100000000`).
  * Explicitly disable peripherals that require heavy hardware drivers not yet present:
    * `#undef USE_VCP` (No native on-chip USB; CP2102N bridge uses UART0)
    * `#undef USE_SDCARD`
    * `#undef USE_MAX7456`
    * `#undef USE_CAMERA_CONTROL`
  * Enable core flight features:
    * `#define USE_GYRO`
    * `#define USE_ACC`
    * `#define USE_CLI`
    * `#define USE_SERIAL_UART`
    * `#define FAST_MATH`

---

### Step 2: Build System Rules (`ARIES.mk`)
* **`src/platform/ARIES/mk/ARIES.mk`**:
  * Set `PLATFORM_SDK := none` (skips external SDK auto-hydration like esp-idf or pico-sdk).
  * Configure compiler prefix:
    ```makefile
    ARM_SDK_PREFIX ?= /home/ivan/.espressif/tools/riscv32-esp-elf/esp-13.2.0_20250707/riscv32-esp-elf/bin/riscv32-esp-elf-
    ```
  * Set `ARCH_FLAGS`:
    ```makefile
    ARCH_FLAGS = -march=rv32im -mabi=ilp32 -mcmodel=medany
    ```
  * Define include paths pointing to `src/platform/ARIES/include`.
  * Add `ARIES/system.c` and `ARIES/stub_drivers.c` to `MCU_COMMON_SRC`.
  * Exclude ARM-specific and unsupported drivers via `MCU_EXCLUDES`.

---

### Step 3: Memory Linker Script (`aries_v3.ld`)
* **`src/platform/ARIES/link/aries_v3.ld`**:
  Define a clean RISC-V memory layout:
  ```ld
  MEMORY
  {
      FLASH (rx)  : ORIGIN = 0x20000000, LENGTH = 2048K
      RAM   (rwx) : ORIGIN = 0x10000000, LENGTH = 256K
  }

  SECTIONS
  {
      .text : {
          *(.text*)
          *(.rodata*)
      } > FLASH

      .data : {
          *(.data*)
      } > RAM AT > FLASH

      .bss : {
          *(.bss*)
          *(COMMON)
      } > RAM

      .stack (NOLOAD) : {
          . = ALIGN(8);
          _sstack = .;
          . = . + 0x4000; /* 16 KB Stack */
          _estack = .;
      } > RAM
  }
  ```

---

### Step 4: System Timebase & Cycle Counter (`system.c`)
* Implement the core timing hooks expected by [`scheduler.c`](file:///home/ivan/Desktop/betaflight/src/main/scheduler/scheduler.c):
  ```c
  #include "platform.h"
  #include "drivers/time.h"
  #include "drivers/system.h"

  uint32_t SystemCoreClock = 100000000;

  // Reads the 64-bit/32-bit RISC-V machine cycle counter
  FAST_CODE uint32_t getCycleCounter(void) {
      uint32_t val;
      __asm__ __volatile__("csrr %0, mcycle" : "=r"(val));
      return val;
  }

  // Basic microsecond timebase stub based on cycle counter
  FAST_CODE timeUs_t micros(void) {
      return (timeUs_t)(getCycleCounter() / (SystemCoreClock / 1000000));
  }

  timeMs_t millis(void) {
      return micros() / 1000;
  }

  void delayMicroseconds(uint32_t us) {
      timeUs_t start = micros();
      while ((micros() - start) < us);
  }

  void delay(uint32_t ms) {
      delayMicroseconds(ms * 1000);
  }

  void systemInit(void) {}
  void systemReset(void) { while(1); }
  ```

---

### Step 5: HAL Driver Stubs (`stub_drivers.c`)
* Provide safe dummy functions for the hardware interfaces that Betaflight links against:
  1. **GPIO / IO:** `IOGetByTag()`, `IOInit()`, `IOConfigGPIO()`, `IORead()`, `IOWrite()`, `IOToggle()`.
  2. **External Interrupts (EXTI):** `extiEnable()`, `extiDisable()`.
  3. **UART / Serial:** `uartOpen()`, `uartWrite()`, `uartRead()`, `uartTotalRxBytesWaiting()`.
  4. **SPI:** `spiInit()`, `spiSetSpeed()`, `spiTransferByte()`, `spiTransfer()`.
  5. **Motor PWM:** `motorInit()`, `pwmWriteMotor()`.
  6. **ADC:** `adcInit()`, `adcReadChannel()`.
  7. **Flash:** `flashInit()`, `flashSectorErase()`, `flashWrite()`, `flashRead()`.

---

### Step 6: Compilation Command & Execution
Execute the build from the project root:
```bash
make clean
make TARGET=ARIESV3 ARM_SDK_PREFIX=/home/ivan/.espressif/tools/riscv32-esp-elf/esp-13.2.0_20250707/riscv32-esp-elf/bin/riscv32-esp-elf- V=1
```

---

## 5. Verification & Analysis Checklist

When the compilation finishes, verify the following:

1. **Build Exit Code:** Must complete with exit code `0`.
2. **Output Artifact:** `obj/betaflight_ARIESV3.elf` must exist.
3. **Symbol Table Check:**
   ```bash
   /home/ivan/.espressif/tools/riscv32-esp-elf/esp-13.2.0_20250707/riscv32-esp-elf/bin/riscv32-esp-elf-objdump -t obj/betaflight_ARIESV3.elf | grep -E "scheduler|pidController|micros|getCycleCounter"
   ```
   Confirms all core Betaflight routines were built.
4. **Soft-Float Linkage Verification:**
   ```bash
   /home/ivan/.espressif/tools/riscv32-esp-elf/esp-13.2.0_20250707/riscv32-esp-elf/bin/riscv32-esp-elf-nm obj/betaflight_ARIESV3.elf | grep -E "__addsf3|__mulsf3|sinf"
   ```
   Confirms standard software floating-point routines from `libgcc` were resolved cleanly.
5. **Memory Sizing Check:**
   ```bash
   /home/ivan/.espressif/tools/riscv32-esp-elf/esp-13.2.0_20250707/riscv32-esp-elf/bin/riscv32-esp-elf-size obj/betaflight_ARIESV3.elf
   ```
   * Text (Flash) should comfortably be < 1 MB (limit: 2 MB).
   * Data + BSS (RAM) should comfortably be < 120 KB (limit: 256 KB).

---

## 6. Next Steps Following Successful Dummy Build

Once this compilation succeeds:
1. Replace `stub_drivers.c` UART stubs with actual THEJAS32 UART0 register operations to establish communication with the Betaflight Configurator.
2. Hook Timer 0 into `system.c` for an interrupt-free hardware microsecond timer.
3. Implement `bus_spi_aries.c` for SPI0 to read from the external IMU sensor.
