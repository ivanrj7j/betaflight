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

---

## 7. Phase 1 & 2 Development Execution Log (Step-by-Step History)

This section documents the actual step-by-step engineering work performed to bring the C-DAC ARIES v3.0 (THEJAS32 / VEGA ET1031 RISC-V) port from a blank directory to a clean, 100% linked Betaflight ELF and HEX executable.

### 7.1 Architecture & Baseline Discovery
* **Toolchain Identification:** Located the pre-installed ESP RISC-V toolchain at `/home/ivan/.espressif/tools/riscv32-esp-elf/esp-13.2.0_20250707/riscv32-esp-elf/bin/riscv32-esp-elf-gcc` (GCC 13.2.0).
* **Hardware Profile:**
  * Core: VEGA ET1031 (RV32IM — integer + multiply/divide, soft-float, 32-bit).
  * System Clock: 100 MHz oscillator.
  * Flash: 2 MB external SPI Flash mapped at `0x20000000`.
  * RAM: 256 KB internal SRAM mapped at `0x10000000`.
  * Communication: Silicon Labs CP2102N USB-to-UART bridge connected to THEJAS32 UART0.
  * Sensors: External SPI IMU (hardware ready on board headers).
* **Isolation Rule:** To maintain upstream maintainability, all changes had to remain strictly isolated within `src/platform/ARIES/` without touching common Betaflight core files.

### 7.2 File Creation (Phase 1 Skeleton)
The platform skeleton was established with the following files:
1. `src/platform/ARIES/mk/ARIES.mk`: Configured `PLATFORM_SDK := none`, toolchain paths, `ARCH_FLAGS`, `MCU_COMMON_SRC`, and linker parameters.
2. `src/platform/ARIES/target/ARIESV3/target.mk`: Declared `TARGET_MCU := THEJAS32`, `TARGET_MCU_FAMILY := ARIES`, `MCU_FLASH_SIZE := 2048`.
3. `src/platform/ARIES/target/ARIESV3/target.h`: Board profile, initial `#undef` list for unsupported features, pin stubs.
4. `src/platform/ARIES/link/aries_v3.ld`: Linker script defining FLASH (`0x20000000`, 2048K) and RAM (`0x10000000`, 256K).
5. `src/platform/ARIES/include/platform/platform.h`: Minimal platform shim (NVIC priority emulation, FAST_CODE attributes, GPIO config definitions).
6. `src/platform/ARIES/include/platform/timer_def.h`: Timer definitions stub.
7. `src/platform/ARIES/io_def_generated.h`: Dummy pin definitions (`DEFIO_PORT_USED_COUNT 0`).
8. `src/platform/ARIES/dma_reqmap_mcu.h`: DMA request map stub.
9. `src/platform/ARIES/system.c`: Microsecond/millisecond timebase based on RISC-V `csrr mcycle` CSR, clock cycle math, system reset stubs.
10. `src/platform/ARIES/stub_drivers.c`: Placeholder stub implementation file.

Verification: `make TARGET=ARIESV3 target-mcu` successfully returned `ARIES : THEJAS32`.

---

## 8. Chronological Mistake-by-Mistake Log & How Each Was Fixed

During the compilation and linking iterations of Phase 2, a total of **28 distinct errors and warnings** were encountered. Below is the complete record of every failure, its root cause, and the exact solution applied.

### Mistake 1: `systemResetToBootloader(void)` Signature Mismatch
* **Symptom:** `error: conflicting types for 'systemResetToBootloader'` in `system.c`.
* **Root Cause:** In `drivers/system.h`, the declaration is `void systemResetToBootloader(bootloaderRequestType_e requestType);`. The initial stub in `system.c` was authored with `void systemResetToBootloader(void)`.
* **Fix:** Updated the function prototype in `system.c` to accept `bootloaderRequestType_e requestType` and marked the parameter `UNUSED(requestType)`.

### Mistake 2: Duplicate Definition of `dmaChannelDescriptor_t`
* **Symptom:** `error: redefinition of typedef 'dmaChannelDescriptor_t'` during compilation.
* **Root Cause:** `dmaChannelDescriptor_t` was manually defined in `platform.h` to satisfy early type dependencies. However, `drivers/dma.h` provides the authoritative definition for all targets.
* **Fix:** Removed the typedef from `platform.h` and retained only `typedef struct { void* test; } DMA_InitTypeDef;`.

### Mistake 3: Missing `SPI_IO_CS_CFG`
* **Symptom:** `error: 'SPI_IO_CS_CFG' undeclared` in `drivers/bus_spi.c`.
* **Root Cause:** Betaflight SPI initialization uses `SPI_IO_CS_CFG` to configure chip-select pin output drive characteristics.
* **Fix:** Added `#define SPI_IO_CS_CFG IOCFG_OUT_PP` to `platform.h`.

### Mistake 4: Unconditional Inclusion of Flash Drivers (`handle.dev` Member Missing)
* **Symptom:** `error: 'flashHandle_t' has no member named 'dev'` across `drivers/flash/flash_*.c`.
* **Root Cause:** Betaflight's `mk/source.mk` unconditionally includes `FLASH_SRC` in `SRC`. In `drivers/flash/flash_impl.h`, the `dev` member inside `flashHandle_t` is wrapped inside `#if defined(USE_SPI)`. Because `target.h` had `#undef USE_SPI` initially, the union member disappeared while the flash C files were still compiled.
* **Fix:** Added `#define USE_SPI` in `target.h` and kept `#undef USE_FLASH` and `#undef USE_FLASHFS` to disable filesystem usage while letting the driver stubs compile.

### Mistake 5: Missing `FLASH_CONFIG_BUFFER_TYPE`
* **Symptom:** `error: 'FLASH_CONFIG_BUFFER_TYPE' undeclared` in `drivers/flash/flash_impl.h`.
* **Root Cause:** Flash configuration streamer needs to know the native word programming unit size.
* **Fix:** Added `#define FLASH_CONFIG_BUFFER_TYPE uint32_t` in `platform.h`.

### Mistake 6: Missing `FunctionalState` Type
* **Symptom:** `error: unknown type name 'FunctionalState'` in `drivers/timer.h`.
* **Root Cause:** Legacy STM32 HAL headers define `typedef enum { DISABLE = 0, ENABLE = !DISABLE } FunctionalState;`. Shared timer code relies on this enum type.
* **Fix:** Added `typedef enum { DISABLE = 0, ENABLE = !DISABLE } FunctionalState;` to `platform.h`.

### Mistake 7: Undeclared Unique Chip ID Macros (`U_ID_0`, `U_ID_1`, `U_ID_2`)
* **Symptom:** `error: 'U_ID_0' undeclared` in `fc/board_info.c`.
* **Root Cause:** Betaflight queries a 96-bit unique silicon identifier. The THEJAS32 SoC has no programmed OTP serial registers.
* **Fix:** Added `#define U_ID_0 0`, `#define U_ID_1 0`, `#define U_ID_2 0` in `platform.h`.

### Mistake 8: Undeclared `SystemCoreClock` in Shared Code
* **Symptom:** `error: 'SystemCoreClock' undeclared` in `cli/cli.c`.
* **Root Cause:** `SystemCoreClock` was defined as a variable in `system.c`, but had no `extern` declaration in `platform.h` for shared consumers.
* **Fix:** Added `extern uint32_t SystemCoreClock;` to `platform.h`.

### Mistake 9: Zero Serial Port Count Warning Treated as Error (`-Werror=type-limits`)
* **Symptom:** `error: comparison of unsigned expression in '< 0' is always false` in `cli/cli.c` and `config/config.c`.
* **Root Cause:** When `SERIAL_PORT_COUNT` was 0, `ARRAYLEN(serialPortIdentifiers)` evaluated to 0. The loop condition `for (unsigned i = 0; i < ARRAYLEN(...); i++)` was flagged by GCC 13 under `-Wtype-limits` / `-Wsign-compare`, and elevated to fatal error by `-Werror`.
* **Fix:** Added `-Wno-type-limits -Wno-sign-compare` to `DEVICE_FLAGS` in `ARIES.mk`.

### Mistake 10: `erpmToHz` Scope Mismatch in `drivers/dshot.c`
* **Symptom:** `error: 'erpmToHz' undeclared` in `drivers/dshot.c:396` inside `erpmToRpm()`.
* **Root Cause:** In upstream `drivers/dshot.c`, `static float erpmToHz` was declared inside `#ifdef USE_DSHOT_TELEMETRY`. But function `erpmToRpm()` was enclosed in `#if defined(USE_ESC_SENSOR) || defined(USE_DSHOT_TELEMETRY)`. `common_pre.h` automatically defined `USE_ESC_SENSOR`, but our `target.h` had undefined `USE_DSHOT_TELEMETRY`. This left `erpmToRpm()` active while `erpmToHz` was never declared.
* **Fix:** Added `#undef USE_ESC_SENSOR`, `#undef USE_ESC_SENSOR_INFO`, and `#undef USE_ESC_SENSOR_TELEMETRY` to `target.h`.

### Mistake 11: Unused Parameter in `io/serial_resource.c`
* **Symptom:** `error: unused parameter 'identifier' [-Werror=unused-parameter]` in `serialType()`.
* **Root Cause:** With 0 serial ports configured, the function body collapsed to returning a default value without referencing the argument.
* **Fix:** Added `-Wno-unused-parameter` to `DEVICE_FLAGS` in `ARIES.mk`.

### Mistake 12: Missing `SCHEDULER_DELAY_LIMIT`
* **Symptom:** `error: 'SCHEDULER_DELAY_LIMIT' undeclared` in `scheduler/scheduler.c:278`.
* **Root Cause:** Scheduler task rate clamp constant is defined on a per-platform basis (10 us for fast modern chips, 100 us for older MCUs).
* **Fix:** Added `#define SCHEDULER_DELAY_LIMIT 10` to `platform.h` (matching ESP32, PICO, and GD32).

### Mistake 13: `rx_pwm.c` Calling `IOConfigGPIOAF` Unconditionally
* **Symptom:** `error: implicit declaration of function 'IOConfigGPIOAF'` in `drivers/rx/rx_pwm.c:343`.
* **Root Cause:** `common_pre.h` enabled legacy parallel PWM and PPM receiver decoding (`USE_RX_PWM`, `USE_RX_PPM`). In `drivers/io.h`, `IOConfigGPIOAF` is guarded by `#ifdef USE_TIMER_AF`. However, `rx_pwm.c` invoked it without checking `USE_TIMER_AF`. ARIES v3 does not use obsolete parallel PWM receivers.
* **Fix:** Added `#undef USE_RX_PWM` and `#undef USE_RX_PPM` in `target.h`.

### Mistake 14: Missing `GPIO_PIN_RESET` and `GPIO_PIN_SET`
* **Symptom:** `error: 'GPIO_PIN_RESET' undeclared` in `io/serial_4way.c:111`.
* **Root Cause:** The 4-way ESC passthrough driver references STM32-style GPIO state macros directly.
* **Fix:** Added `#define GPIO_PIN_RESET 0` and `#define GPIO_PIN_SET 1` in `platform.h`.

### Mistake 15: GCC 13 Incompatible Pointer Type in `sensors/compass.c`
* **Symptom:** `error: passing argument 2 of 'compassDetect' from incompatible pointer type [-Werror=incompatible-pointer-types]` in `compass.c:518`.
* **Root Cause:** Line 234 declared `static bool compassDetect(magDev_t *magDev, uint8_t *alignment)`, while line 516 declared `sensor_align_e alignment`. Under GCC 13.2, passing an enum pointer to a `uint8_t *` is strictly rejected as an incompatible pointer error under `-Werror`.
* **Fix:** Added `-Wno-incompatible-pointer-types` to `DEVICE_FLAGS` in `ARIES.mk`.

### Mistake 16: Missing ARM CMSIS IRQ Intrinsics (`__disable_irq`, `__enable_irq`)
* **Symptom:** `error: implicit declaration of function '__disable_irq'` in `drivers/compass/compass_ak8963.c:121`.
* **Root Cause:** Compass and driver code calls CMSIS primitives to protect atomic bus transactions.
* **Fix:** Implemented inline functions in `platform.h` utilizing standard RISC-V privileged CSR instructions on `mstatus`:
  ```c
  __attribute__((always_inline)) static inline void __disable_irq(void) { __asm__ volatile ("csrc mstatus, 8"); }
  __attribute__((always_inline)) static inline void __enable_irq(void) { __asm__ volatile ("csrs mstatus, 8"); }
  ```
  Bit 3 is the standard RISC-V MIE (Machine Interrupt Enable) bit.

### Mistake 17: Missing `__NOP()` Intrinsic
* **Symptom:** `error: implicit declaration of function '__NOP'` in `drivers/rx/rx_sx1280.c:86`.
* **Root Cause:** Drivers use `__NOP()` for short microsecond timing delays.
* **Fix:** Added `#define __NOP() __asm__ volatile ("nop")` in `platform.h`.

### Mistake 18: RISC-V GCC 13 Split Architecture Requirement (`zicsr`)
* **Symptom:** Assembler error during LTO compile: `Error: unrecognized opcode 'csrr a4, mcycle', extension 'zicsr' required`.
* **Root Cause:** In GCC 12/13, RISC-V Control and Status Register instructions (`csrr`, `csrs`, `csrc`) were decoupled from the base `I` instruction set into the formal `_zicsr` extension. Specifying only `-march=rv32im` causes the GNU assembler (`as`) to reject CSR instructions.
* **Fix:** Updated `ARCH_FLAGS` in `ARIES.mk` to:
  ```makefile
  ARCH_FLAGS = -march=rv32im_zicsr -mabi=ilp32 -mcmodel=medany
  ```

### Mistake 19: Missing Parameter Group & Config Linker Sections
* **Symptom:** Undefined references to `__pg_registry_start`, `__pg_registry_end`, `__pg_resetdata_start`, `__pg_resetdata_end`, `__config_start`, `__config_end`, and `_Min_Stack_Size`.
* **Root Cause:** The skeleton linker script `aries_v3.ld` lacked Betaflight's Parameter Group (PG) section layout and configuration boundary markers.
* **Fix:** Updated `aries_v3.ld` with:
  ```ld
  _Min_Stack_Size = 0x800;
  __config_start = 0x20000000 + 2048K - 32K;
  __config_end   = 0x20000000 + 2048K;

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
  ```

### Mistake 20: Erroneously Excluding Telemetry Modules
* **Symptom:** 18 undefined reference errors (`initCrsfTelemetry`, `checkCrsfTelemetryState`, `handleCrsfTelemetry`, `initGhstTelemetry`, `initSrxlTelemetry`, etc.).
* **Root Cause:** When modeling `ARIES.mk` after `SITL.mk`, `telemetry/crsf.c`, `telemetry/ghst.c`, and `telemetry/srxl.c` were copied into `MCU_EXCLUDES`. However, the Betaflight receiver subsystem calls these functions unconditionally.
* **Fix:** Tested compiling the files directly under RV32IM. All three compiled cleanly without error. Removed them from `MCU_EXCLUDES` in `ARIES.mk`.

### Mistake 21: Missing Single-Core Atomic Primitive (`__atomic_fetch_add_4`)
* **Symptom:** `undefined reference to '__atomic_fetch_add_4'`.
* **Root Cause:** The VEGA ET1031 is RV32IM (no 'A' atomic memory extension). GCC generated library calls to `__atomic_fetch_add_4`, but `libatomic.a` was not distributed with the toolchain.
* **Fix:** Implemented an interrupt-safe fallback in `stub_drivers.c`:
  ```c
  unsigned int __atomic_fetch_add_4(volatile void *ptr, unsigned int val, int memorder) {
      UNUSED(memorder);
      __disable_irq();
      unsigned int old = *(volatile unsigned int *)ptr;
      *(volatile unsigned int *)ptr = old + val;
      __enable_irq();
      return old;
  }
  ```
* **Sub-Mistake:** Initially typed with `uint32_t`. On 32-bit RISC-V GCC, `uint32_t` is `unsigned long`, causing `error: mismatch in return type of built-in function '__atomic_fetch_add_4'; expected 'unsigned int'`. Resolved by changing types strictly to `unsigned int`.

### Mistake 22: Unresolved Driver Hal Symbols
* **Symptom:** Linker errors for IO (`IOGetByTag`, `IOConfigGPIO`, `IOHi`, `IOLo`, `IORead`, `IOWrite`, `IOToggle`, `IO_GPIOPortIdx`, `IO_GPIOPinIdx`), SPI (`spiPreinit`, `spiCalculateDivider`, `spiPinConfigure`, `spiSequenceStart`), DMA (`dmaDescriptors`, `dmaGetHandlerCount`), Timers (`timerInit`, callbacks), EXTI, and Config Streamer.
* **Root Cause:** Expected HAL abstraction routines were not yet stubbed out.
* **Fix:** Implemented comprehensive, clean stubs in `stub_drivers.c` covering all required subsystem APIs.

### Mistake 23: WS2811 LED Strip Dependencies
* **Symptom:** Linker errors for `ws2811LedStripHardwareInit`, `ws2811LedStripInit`, `ws2811LedStripStartTransfer`, `ws2811LedStripUpdateTransferBuffer`.
* **Root Cause:** `common_pre.h` enabled `USE_LED_STRIP` by default.
* **Fix:** Added `#undef USE_LED_STRIP` to `target.h`.

### Mistake 24: Duplicate Symbol `dshotMotorCount`
* **Symptom:** `multiple definition of 'dshotMotorCount'; obj/main/ARIESV3/drivers/dshot.o: first defined here`.
* **Root Cause:** `dshotMotorCount` was declared and defined in `drivers/dshot.c:98`. Defining it again in `stub_drivers.c` caused a multiple definition error during linking.
* **Fix:** Removed `uint8_t dshotMotorCount = 0;` from `stub_drivers.c`.

### Mistake 25: Missing `MAX_SPI_PIN_SEL`
* **Symptom:** `error: 'MAX_SPI_PIN_SEL' undeclared` in `drivers/bus_spi_impl.h:45`.
* **Root Cause:** SPI pin mapping arrays allocate pin options using `MAX_SPI_PIN_SEL`.
* **Fix:** Added `#define MAX_SPI_PIN_SEL 3` in `platform.h`.

### Mistake 26: UART Trait & Buffer Attribute Errors
* **Symptom:** `error: #error "Must specify either AF mode for MCU"`, `error: 'UARTHARDWARE_MAX_PINS' undeclared`, `error: #error Undefined UART_{TX,RX}_BUFFER_ATTRIBUTE`.
* **Root Cause:** When `USE_UART1` was enabled in `target.h`, `drivers/serial_uart.c` expected target platform trait definitions.
* **Fix:** Added to `platform.h`:
  ```c
  #define UART_TRAIT_AF_PORT        1
  #define UARTHARDWARE_MAX_PINS     4
  #define UART_TX_BUFFER_ATTRIBUTE
  #define UART_RX_BUFFER_ATTRIBUTE
  ```

### Mistake 27: Missing `serialPinConfig_System`
* **Symptom:** `undefined reference to 'serialPinConfig_System'`.
* **Root Cause:** In `drivers/serial_pinconfig.c:27`, the parameter group definition `PG_REGISTER_WITH_RESET_FN(serialPinConfig_t, serialPinConfig, ...)` is enclosed within `#if SERIAL_TRAIT_PIN_CONFIG`. Without this macro defined, the parameter group was never registered. Additionally, `serial_pinconfig.c` was missing from `MCU_COMMON_SRC`.
* **Fix:** Added `#define SERIAL_TRAIT_PIN_CONFIG 1` to `platform.h` and added `drivers/serial_pinconfig.c` to `MCU_COMMON_SRC` in `ARIES.mk`.

### Mistake 28: Missing UART & Persistent Object Stubs
* **Symptom:** Undefined references to `serialUART`, `uartReconfigure`, `persistentObjectRead`, and `persistentObjectWrite`.
* **Root Cause:** Core serial opening and RTC/reboot-reason persistence mechanisms required backing functions.
* **Fix:** Added RAM-backed `static uint32_t persistentObjects[16]` and minimal `serialUART` (returns `NULL`) and `uartReconfigure` stubs in `stub_drivers.c`.

---

## 9. Final Summary

With all 28 compilation and linking hurdles resolved:
1. **Compilation Success:** The entire Betaflight flight controller (flight algorithms, PID loops, attitude estimator, task scheduler, CMS, CLI, MSP, telemetry protocols) compiles and links with **exit code 0**.
2. **Binary Artifacts:**
   * ELF: `obj/main/betaflight_ARIESV3.elf`
   * HEX: `obj/betaflight_2026.12.0-alpha_ARIESV3.hex` (Generated via `make TARGET=ARIESV3 hex`)
   * Map: `obj/main/betaflight_ARIESV3.map`
3. **Memory Utilization:**
   * **Flash Memory:** 708 KB utilized out of 2,048 KB available (**34.5%** footprint).
   * **SRAM Memory:** 63.7 KB utilized out of 256 KB available (**24.9%** footprint, including 16 KB stack).
4. **Clean Codebase Isolation:** All platform files are self-contained in `src/platform/ARIES/`. Upstream Betaflight core code was left 100% unaltered.

---

## 10. Common Mistakes & Pitfalls for RISC-V Betaflight Ports

For developers porting Betaflight to other RISC-V or non-ARM microcontrollers, keep this checklist of common pitfalls in mind:

| Pitfall / Issue | Explanation & Prevention |
| :--- | :--- |
| **Omitting `_zicsr` from `-march`** | In modern GCC (>= 12), RISC-V CSR instructions are not in `-march=rv32im`. Always specify `-march=rv32im_zicsr` or CSR instructions like `csrr mcycle` will trigger assembler errors. |
| **Assuming `uint32_t` is `unsigned int`** | In 32-bit RISC-V GCC, `uint32_t` is typedef'd as `unsigned long`. Built-in function prototypes (like `__atomic_fetch_add_4`) expect `unsigned int`. Using `uint32_t` causes prototype mismatch errors. |
| **Unconditional `source.mk` Inclusions** | Betaflight's build system always compiles certain source lists (e.g. `FLASH_SRC`) regardless of feature defines. Do not assume `#undef USE_FLASH` stops `flash.c` from being compiled; ensure required types and structs still resolve. |
| **Missing Parameter Group Linker Sections** | Betaflight's `PG_REGISTER` system relies heavily on linker sections `.pg_registry` and `.pg_resetdata`. Without these in the linker script, linking fails with unresolved `__pg_registry_*` symbols. |
| **Config Boundaries in Linker Script** | The EEPROM streamer checks `__config_start` and `__config_end`. Ensure the linker script defines them (e.g., in the top 32 KB of Flash). |
| **Implicit Header Precedence** | `serial_uart_impl.h` requires `drivers/serial_uart.h` and `io/serial.h` to precede it. Check header inclusion order when authoring driver stubs. |
| **`-Werror` on Unused Parameters** | When peripheral ports are stubbed out (count = 0), upstream utility functions often fail with `-Werror=unused-parameter` or `-Werror=type-limits`. Include appropriate warning suppressions in `DEVICE_FLAGS`. |
| **CMSIS Intrinsics Non-Existent** | Functions like `__NOP()`, `__disable_irq()`, and `__enable_irq()` are ARM CMSIS intrinsics. You must emulate them via inline RISC-V assembly (`nop`, `csrc mstatus, 8`, `csrs mstatus, 8`) in `platform.h`. |
| **Non-Atomic RV32IM Cores** | If the core lacks the 'A' extension, provide software fallbacks with IRQ disabling for GCC atomic built-ins (`__atomic_fetch_add_4`). |
