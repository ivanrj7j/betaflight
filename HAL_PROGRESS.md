# HAL Progress & Implementation Log: C-DAC ARIES v3.0 (THEJAS32 / VEGA ET1031)

---

## 📌 Standard Instructions for Updating this File

> [!IMPORTANT]
> **To Any Agent or Developer Working on this Project:**
> When implementing subsequent phases (Phase 2 through Phase 8), **strictly adhere to the following logging format** to preserve project continuity:
>
> 1. **Do Not Overwrite Existing Entries:** Always append new phase sections chronologically.
> 2. **Phase Status Checklist:** Update the **High-Level Milestone Tracker** table at the top of the document from `Pending` or `In Progress` to `Completed`.
> 3. **Document Precise File Changes:** List every file created, modified, or deleted with its exact repository path.
> 4. **Record Every Obstacle / Problem & Exact Solution:** Document compiler errors, assembler issues, register mismatches, or link errors encountered, along with the precise technical fix.
> 5. **Include Artifact Verification:** Include the output from `size`, `nm`, or `objdump` proving that the binary compiled cleanly with exit code `0`.
> 6. **Specify Next Phase Handoff:** End your update by clearly outlining the immediate tasks for the next phase.

---

## 📊 High-Level Milestone Tracker

| Phase | Milestone Description | Target Subsystems | Status | Completion Date |
| :---: | :--- | :--- | :---: | :---: |
| **Phase 1** | Foundation & Trap Handler Verification | `startup.S`, `mtvec`, `trap_entry`, Exception Handling | **Completed** | 2026-10-19 |
| **Phase 2** | Console & Communication | UART0 (CLI/MSP), Ring Buffer, RX Interrupt | **Completed** | 2026-10-19 |
| **Phase 3** | High-Resolution Timebase | Hardware Timer 0/1, Monotonic `micros()`, Jitter Check | **Completed** | 2026-10-20 |
| **Phase 4** | GPIO & External Interrupts | Bank 0/1 Address-Decoded IO, EXTI (Gyro DRDY) | **Completed** | 2026-10-20 |
| **Phase 5** | SPI Bus & IMU Sensor Driver | SPI0 Master with `CSAAT`, ICM42688P / BMI270 Detection | **Completed** | 2026-10-20 |
| **Phase 6** | Non-Volatile Config Storage | SPI3 Winbond NOR Flash, Parameter Group (PG) Save/Load | **Completed** | 2026-10-20 |
| **Phase 7** | Actuator Outputs & RC Link | Motor PWM (PWM_0..3), UART1 Serial RX (CRSF/ELRS) | *Pending* | — |
| **Phase 8** | Soft-Float Tuning & Bench Flight | 1 kHz Loop Clamping, Fast-Math Profiling, No Overruns | *Pending* | — |

---

## 🛠️ Phase 1 Detailed Execution Log

### 1. Objective
Establish a clean bare-metal reset sequence, complete machine register initialization, machine trap vector routing (`mtvec`), interrupt frame save/restore context, and C-level exception/interrupt dispatching for the C-DAC ARIES v3.0 flight controller firmware.

### 2. Files Created & Modified

| File Path | Action | Purpose & Technical Summary |
| :--- | :---: | :--- |
| [`src/platform/ARIES/include/platform/bsp_thejas32.h`](file:///home/ivan/Desktop/betaflight/src/platform/ARIES/include/platform/bsp_thejas32.h) | **Created** | Consolidated hardware register structures, peripheral base addresses (`0x10000100` UART, `0x10000A00` Timers, `0x20010000` Interrupt Controller), and IRQ definitions extracted from the official VEGA SDK BSP. |
| [`src/platform/ARIES/startup.S`](file:///home/ivan/Desktop/betaflight/src/platform/ARIES/startup.S) | **Modified** | Replaced minimal stub startup with a full assembly reset routine: clears GPRs `x1`–`x31`, initializes `sp` to `_estack` and `gp` to `__global_pointer$`, sets `mtvec` to `trap_entry`, copies `.data`, zeros `.bss`, calls `main()`, and provides a 32-register context save/restore trap entry frame with `mret`. |
| [`src/platform/ARIES/system.c`](file:///home/ivan/Desktop/betaflight/src/platform/ARIES/system.c) | **Modified** | Implemented `handle_trap(mcause, mepc, sp)` to separate synchronous exceptions from asynchronous interrupts; implemented 32-slot interrupt dispatch table (`interrupt_table[32]`) and peripheral registration APIs (`thejas32_interrupt_register`, `thejas32_interrupt_enable`). |
| [`src/platform/ARIES/include/platform/platform.h`](file:///home/ivan/Desktop/betaflight/src/platform/ARIES/include/platform/platform.h) | **Modified** | Updated `__enable_irq()` to enable both Machine External Interrupts (`MEIE` bit 11 in `mie`) and global machine interrupts (`MIE` bit 3 in `mstatus`). |

---

### 3. Problems / Obstacles Encountered & How Each Was Solved

#### Problem 1: Unhandled Traps Causing Renode Emulation Abort
- **Symptom:** In earlier tests, if any exception or interrupt occurred, the processor branched to address `0x00000000` or an uninitialized `mtvec` address, leading to Renode CPU aborts.
- **Root Cause:** `mtvec` was left uninitialized after reset vector entry, meaning the CPU defaulted to reset vector or unmapped address space on traps.
- **Solution:** Configured `mtvec` in `startup.S` immediately after stack and global pointer initialization:
  ```assembly
  la t0, trap_entry
  csrw mtvec, t0
  ```
  And aligned `trap_entry` to 16 bytes (`.align 4`).

#### Problem 2: Stack Frame Alignment and Size for RISC-V RV32
- **Symptom:** Saving all 32 integer registers requires 128 bytes ($32 \times 4\text{ bytes}$). The VEGA SDK `crt.S` reference used an older 272-byte frame designed for 64-bit multi-core configurations.
- **Root Cause:** A 272-byte frame on a 32-bit architecture unnecessarily burns limited stack space and risks stack overflow on our 16 KB stack.
- **Solution:** Sized the stack frame to exactly 128 bytes (`addi sp, sp, -128`), perfectly satisfying the 16-byte stack alignment required by the RISC-V ILP32 ABI, saving registers `x1` through `x31` systematically.

#### Problem 3: Distinguishing Asynchronous External Interrupts from Synchronous Traps
- **Symptom:** Without checking the interrupt bit (`mcause` MSB), synchronous exceptions (like illegal instructions or misalignment) would attempt to execute nonexistent peripheral handlers.
- **Root Cause:** In RISC-V privileged architecture, bit 31 of `mcause` indicates an interrupt (`1`) vs an exception (`0`).
- **Solution:** Implemented MSB masking in `handle_trap()`:
  ```c
  if (mcause & 0x80000000UL) {
      uint32_t cause_num = mcause & 0x7FFFFFFFUL;
      if (cause_num == 11) { // Machine External Interrupt
          uint32_t status = THEJAS32_INTR_CTRL.INTR_STATUS;
          for (uint8_t i = 0; i < 32; i++) {
              if ((status >> i) & 1U) {
                  if (interrupt_table[i]) interrupt_table[i]();
              }
          }
      }
      return mepc;
  } else {
      handle_trap_panic(mcause, mepc, sp);
      return mepc + 4;
  }
  ```

#### Problem 4: Missing Global Peripheral External Interrupt Enable
- **Symptom:** Enabling interrupts via `__enable_irq()` was previously only toggling `mstatus.MIE` (bit 3), but external interrupts from the THEJAS32 interrupt controller require `mie.MEIE` (bit 11).
- **Root Cause:** The base RISC-V core will not acknowledge external peripheral lines unless Machine External Interrupt Enable (`MEIE`) is set in the `mie` CSR.
- **Solution:** Updated `__enable_irq()` in `platform.h`:
  ```c
  __attribute__((always_inline)) static inline void __enable_irq(void) {
      __asm__ volatile ("csrs mie, %0" : : "r"(0x800)); // Bit 11 = MEIE
      __asm__ volatile ("csrs mstatus, 8");             // Bit 3  = MIE
  }
  ```

---

### 4. Build & Binary Verification

- **Compilation Command:**
  ```bash
  make TARGET=ARIESV3 -j$(nproc)
  ```
- **Exit Code:** `0` (Success)
- **Memory Footprint:**
  ```text
     text    data     bss     dec     hex filename
   718576    6336   58928  783840   bf5e0 ./obj/main/betaflight_ARIESV3.elf
  ```
  - **Flash Usage:** ~708 KB / 2,048 KB (34.5%)
  - **RAM Usage:** ~63.7 KB / 256 KB (24.9%)

- **Symbol & Disassembly Inspection:**
  - `_start` located at `0x20000000` (resets GPRs, loads `sp = 0x1000FEF0`, `gp = 0x10000800`).
  - `mtvec` written with address `0x200000F0` (`trap_entry`).
  - `trap_entry` correctly disassembles to context save on stack, branch to `handle_trap` (`0x200033DC`), context restore, and `mret`.

---

## 🛠️ Phase 2 Detailed Execution Log

### 1. Objective
Implement real hardware-backed UART communications for the C-DAC ARIES v3.0 board. Connect UART0 (`0x10000100`) to the onboard Silicon Labs CP2102N USB-UART bridge to support the interactive Betaflight Command Line Interface (CLI) and MultiWii Serial Protocol (MSP), and prepare UART1 (`0x10000200`) for serial radio receivers.

### 2. Files Created & Modified

| File Path | Action | Purpose & Technical Summary |
| :--- | :---: | :--- |
| [`src/platform/ARIES/serial_uart_aries.c`](file:///home/ivan/Desktop/betaflight/src/platform/ARIES/serial_uart_aries.c) | **Created** | Hardware UART driver for THEJAS32. Implements 16550 divisor baud setting, line control (8N1), FIFO reset/enable, transmit priming, interrupt-driven RX/TX processing (`uartIrqHandler`), and hardware definitions (`uartHardware[UARTDEV_COUNT]`). |
| [`src/platform/ARIES/include/platform/platform.h`](file:///home/ivan/Desktop/betaflight/src/platform/ARIES/include/platform/platform.h) | **Modified** | Defined `#define SERIAL_UART_FIRST_INDEX 0` so that `SERIAL_PORT_UART0` maps cleanly to index `0` instead of triggering index offset errors in `serial_post.h`. |
| [`src/platform/ARIES/target/ARIESV3/target.h`](file:///home/ivan/Desktop/betaflight/src/platform/ARIES/target/ARIESV3/target.h) | **Modified** | Enabled `#define USE_UART0` and `#define USE_UART1`, with pin allocations set to `NONE` (fixed SoC header/bridge routing). |
| [`src/platform/ARIES/mk/ARIES.mk`](file:///home/ivan/Desktop/betaflight/src/platform/ARIES/mk/ARIES.mk) | **Modified** | Added `ARIES/serial_uart_aries.c` to `MCU_COMMON_SRC`. |
| [`src/platform/ARIES/stub_drivers.c`](file:///home/ivan/Desktop/betaflight/src/platform/ARIES/stub_drivers.c) | **Modified** | Removed dummy implementations of `serialUART`, `uartReconfigure`, and `uartPinConfigure`; added missing `dmaGetDataLength()` stub. |
| [`src/platform/ARIES/include/platform/bsp_thejas32.h`](file:///home/ivan/Desktop/betaflight/src/platform/ARIES/include/platform/bsp_thejas32.h) | **Modified** | Added public C prototypes for `thejas32_interrupt_register`, `thejas32_interrupt_enable`, and `thejas32_interrupt_disable`. |

---

### 3. Problems / Obstacles Encountered & How Each Was Solved

#### Problem 1: `UART_BUFFERS_EXTERN` Redefinition and Syntax Error
- **Symptom:** Compiler threw `error: expected declaration specifiers or '...' before numeric constant` on `UART_BUFFERS_EXTERN(0)` in `serial_uart_aries.c`.
- **Root Cause:** Upstream `drivers/serial_uart_impl.h` already expands `UART_BUFFERS_EXTERN(0)` conditionally when `USE_UART0` is defined. Calling it a second time created an invalid C syntax collision.
- **Solution:** Removed the manual macro invocation from `serial_uart_aries.c`, allowing the header's canonical external declarations of `uart0TxBuffer` and `uart0RxBuffer` to be referenced cleanly.

#### Problem 2: `NONE` Pin Identifier Undeclared
- **Symptom:** `error: 'NONE' undeclared here (not in a function)` when setting `.rxPins = { { .pin = NONE } }`.
- **Root Cause:** In Betaflight's IO system, `NONE` is a preprocessor macro used within target definitions that expands through `DEFIO_TAG__NONE` (`0`).
- **Solution:** Replaced `NONE` with `DEFIO_TAG__NONE` in the static `uartHardware` table.

#### Problem 3: Linker Error `undefined reference to dmaGetDataLength`
- **Symptom:** Linker failed with `undefined reference to dmaGetDataLength` in `serial_uart.o` during LTO optimization.
- **Root Cause:** When `USE_UART` is compiled into Betaflight's shared `serial_uart.c`, buffer occupancy functions check for active DMA channels by querying `dmaGetDataLength()`. Since ARIES v3.0 does not use DMA for UART, this HAL hook was unresolved.
- **Solution:** Implemented a stub returning `0` in `src/platform/ARIES/stub_drivers.c`:
  ```c
  uint32_t dmaGetDataLength(dmaResource_t *ref)
  {
      UNUSED(ref);
      return 0;
  }
  ```

#### Problem 4: Index 0 Mapping for `SERIAL_PORT_UART0`
- **Symptom:** Betaflight's `serial_post.h` by default expects UART numbering to begin at 1 (`SERIAL_UART_FIRST_INDEX 1`), rejecting `USE_UART0` with `#error "USE_UART0 is defined, but SERIAL_UART does not allow index 0"`.
- **Root Cause:** Target platform did not specify that UART indexing starts at 0.
- **Solution:** Defined `#define SERIAL_UART_FIRST_INDEX 0` in `src/platform/ARIES/include/platform/platform.h` (matching the pattern used by ESP32 and PICO platforms).

---

### 4. Build & Binary Verification

- **Compilation Command:**
  ```bash
  make TARGET=ARIESV3 -j$(nproc)
  ```
- **Exit Code:** `0` (Success)
- **Memory Footprint:**
  ```text
     text    data     bss     dec     hex filename
   723768    6344   60728  790840   c1138 ./obj/main/betaflight_ARIESV3.elf
  ```
  - **Flash Usage:** ~723.7 KB / 2,048 KB (35.3%)
  - **RAM Usage:** ~65.5 KB / 256 KB (25.6%)

- **Symbol Inspection:**
  - `uart0RxBuffer` and `uart0TxBuffer` allocated in BSS (`0x1000AEA0`, `0x1000AFA0`).
  - `uart1RxBuffer` and `uart1TxBuffer` allocated in BSS (`0x1000A8A0`, `0x1000A9A0`).
  - `thejas32_uart0_isr` (`0x20000E24`) and `thejas32_uart1_isr` (`0x20000E30`) present and linked.
  - `thejas32_uart_set_baudrate` (`0x200032FC`) present and linked.
  - `uartHardware` table placed in flash (`0x200AF268`).

---

## 🛠️ Phase 3 Detailed Execution Log

### 1. Objective
Replace the naive 32-bit `mcycle` cycle counter based microsecond timebase with a robust, glitch-free monotonic timebase; eliminate the 42.9-second rollover discontinuity in the scheduler; and integrate the C-DAC THEJAS32 hardware Timer 0 (`0x10000A00`) with reload configuration, interrupt service routine (`thejas32_timer0_isr`), EOI register acknowledgement, and overflow tracking.

### 2. Files Created & Modified

| File Path | Action | Purpose & Technical Summary |
| :--- | :---: | :--- |
| [`src/platform/ARIES/system.c`](file:///home/ivan/Desktop/betaflight/src/platform/ARIES/system.c) | **Modified** | Implemented atomic 64-bit cycle reading `getCycleCounter64()` via `mcycle` and `mcycleh`; implemented hardware Timer 0 driver (`thejas32_timer_init`, `thejas32_timer0_isr`, `timer0GetCurrentValue`, `timer0GetOverflows`, `timer0Micros`); rewritten `micros()`, `millis()`, `delayMicroseconds()`, and `delay()`. |
| [`src/platform/ARIES/include/platform/bsp_thejas32.h`](file:///home/ivan/Desktop/betaflight/src/platform/ARIES/include/platform/bsp_thejas32.h) | **Modified** | Added `#include "common/time.h"` and declared function prototypes for `thejas32_timer_init`, `thejas32_timer0_isr`, `timer0GetCurrentValue`, `timer0GetOverflows`, `timer0Micros`, and `getCycleCounter64`. |

---

### 3. Problems / Obstacles Encountered & How Each Was Solved

#### Problem 1: 32-Bit `mcycle` Rollover Jitter at 42.9 Seconds
- **Symptom:** The previous `micros()` routine computed `(timeUs_t)(getCycleCounter() / 100)`. Because `getCycleCounter()` only sampled the lower 32-bit `mcycle` CSR at 100 MHz, the counter rolled over from `0xFFFFFFFF` to `0` every $42.949\text{ seconds}$. At rollover, `micros()` dropped from $42,949,672$ to $0$.
- **Impact on Flight Controller:** In Betaflight, `timeDelta_t` is computed as `(timeUs_t)(now - last)`. When time drops from 42.9 million to 0, `now - last` evaluates to over 4.25 billion microseconds (~70 minutes in the past), producing massive scheduler jitter, watchdog panics, and filter divergence.
- **Solution:** Implemented `getCycleCounter64()`, which reads `mcycleh` (CSR `0xB80`) and `mcycle` (CSR `0xB00`) atomically with double-read consistency checking (`do { hi = csrr(mcycleh); lo = csrr(mcycle); hi2 = csrr(mcycleh); } while (hi != hi2);`). Computing `(timeUs_t)(getCycleCounter64() / 100ULL)` guarantees that `timeUs_t` rolls over at the standard $2^{32} - 1$ microseconds ($71.58\text{ minutes}$), where unsigned 32-bit subtraction `now - last` is mathematically exact across rollover.

#### Problem 2: Eliminating Software Division Overhead (`__udivdi3`) in Critical Microsecond Loop
- **Symptom:** Standard 64-bit division by a dynamic variable on 32-bit RISC-V invokes the software division runtime helper `__udivdi3`, which adds stack frames and function call latency to every `micros()` invocation (called thousands of times per second).
- **Solution:** Sourced the divisor directly as a constant `100ULL` (`SystemCoreClock / 1,000,000` for 100 MHz). The GCC optimizer converts constant 64-bit division into fast inlined reciprocal multiplication, shifts, and adds (~30 assembly instructions) without issuing a single function call.

#### Problem 3: Millisecond Rollover Mismatch in `millis()`
- **Symptom:** Computing `millis()` as `micros() / 1000` caused milliseconds to wrap at 71 minutes instead of the standard 49.7 days documented in Betaflight's `common/time.h`.
- **Solution:** Computed `millis()` as `(timeMs_t)(getCycleCounter64() / 100000ULL)`, giving a 32-bit millisecond counter that rolls over smoothly after $49.71\text{ days}$.

#### Problem 4: THEJAS32 Hardware Timer 0 Interrupt Acknowledgment & Overflow Tracking
- **Symptom:** Without clearing the End of Interrupt (`EOI`) register on the Synopsys DesignWare timer block, the interrupt line remains asserted, immediately re-triggering the trap handler upon return (`mret`).
- **Solution:** Configured `THEJAS32_TIMER(0)` in user-defined periodic mode (`Control = 0x03`, `LoadCount = 0xFFFFFFFFUL`), registered `thejas32_timer0_isr` on IRQ 7 (`THEJAS32_IRQ_TIMER0`), and ensured the ISR explicitly reads `THEJAS32_TIMER(0).EOI` to clear the peripheral interrupt latch before incrementing `timer0_overflows`.

---

### 4. Build & Binary Verification

- **Compilation Commands:**
  ```bash
  make TARGET=ARIESV3 -j$(nproc)
  make TARGET=ARIESV3 hex
  ```
- **Exit Code:** `0` (Success)
- **Memory Footprint:**
  ```text
     text    data     bss     dec     hex filename
   735968    6344   60728  803040   c40e0 ./obj/main/betaflight_ARIESV3.elf
  ```
  - **Flash Usage:** ~735.9 KB / 2,048 KB (35.9%)
  - **RAM Usage:** ~65.5 KB / 256 KB (25.6%)

- **Symbol Inspection:**
  - `timer0_overflows` allocated at `0x1000B6CC` in SRAM BSS.
  - `thejas32_timer0_isr` placed at `0x20000D54` (reads `0x10000A0C` `EOI`, increments overflow counter).
  - `micros` placed at `0x200032E0` (inlined atomic CSR read + reciprocal divide by 100ULL).
  - `millis` placed at `0x20003370` (64-bit cycle division by 100,000 for 49.7 day rollover).
  - Hex artifact generated: `obj/betaflight_2026.12.0-alpha_ARIESV3.hex`.

---

## 🛠️ Phase 4 Detailed Execution Log

### 1. Objective
Implement the hardware GPIO driver for C-DAC THEJAS32 / VEGA ET1031 using the official BSP address-decoded bit-shift register access scheme across Bank 0 (`0x10080000`, pins 0–15) and Bank 1 (`0x10180000`, pins 16–31). Implement external interrupt dispatching (`exti_aries.c`) connected to the THEJAS32 interrupt controller (IRQ 10 for Bank 0, IRQ 11 for Bank 1) to support sensor Data Ready (DRDY) event handling and flight loop triggering.

### 2. Files Created & Modified

| File Path | Action | Purpose & Technical Summary |
| :--- | :---: | :--- |
| [`src/platform/ARIES/io_def_generated.h`](file:///home/ivan/Desktop/betaflight/src/platform/ARIES/io_def_generated.h) | **Created** | Defined 32-pin mapping (`PA0` through `PA31`), set `DEFIO_IO_USED_COUNT = 32`, `DEFIO_USED_COUNT = 32`, and configured preprocessor tags and record index bindings. |
| [`src/platform/ARIES/io_aries.c`](file:///home/ivan/Desktop/betaflight/src/platform/ARIES/io_aries.c) | **Created** | Hardware GPIO driver implementing `IOInitGlobal()`, `IOGetByTag()`, `IOConfigGPIO()`, `IOHi()`, `IOLo()`, `IOToggle()`, `IORead()`, `IOWrite()`, `IO_GPIOPortIdx()`, and `IO_GPIOPinIdx()`. Maps direction registers (`base + 0x40000`) and address-decoded pin access (`base + ((1 << pin) << 2)`). |
| [`src/platform/ARIES/exti_aries.c`](file:///home/ivan/Desktop/betaflight/src/platform/ARIES/exti_aries.c) | **Created** | External interrupt driver implementing `EXTIInit()`, `EXTIHandlerInit()`, `EXTIConfig()`, `EXTIRelease()`, `EXTIEnable()`, `EXTIDisable()`, and `IO_EXTI_Line()`. Routes Bank 0 (IRQ 10) and Bank 1 (IRQ 11) interrupts into per-pin callback dispatch table. |
| [`src/platform/ARIES/include/platform/platform.h`](file:///home/ivan/Desktop/betaflight/src/platform/ARIES/include/platform/platform.h) | **Modified** | Defined `#define DEFIO_PORT_PINS 64` and structured `IO_CONFIG(mode, speed, pupd)` and `IOCFG_*` constants. |
| [`src/platform/ARIES/target/ARIESV3/target.h`](file:///home/ivan/Desktop/betaflight/src/platform/ARIES/target/ARIESV3/target.h) | **Modified** | Enabled `#define USE_EXTI` and mapped status LED (`#define LED0_PIN PA22` for onboard Heartbeat LED `LD4`). |
| [`src/platform/ARIES/mk/ARIES.mk`](file:///home/ivan/Desktop/betaflight/src/platform/ARIES/mk/ARIES.mk) | **Modified** | Added `ARIES/io_aries.c` and `ARIES/exti_aries.c` to `MCU_COMMON_SRC`. |
| [`src/platform/ARIES/stub_drivers.c`](file:///home/ivan/Desktop/betaflight/src/platform/ARIES/stub_drivers.c) | **Modified** | Removed dummy implementations of IO and EXTI routines. |

---

### 3. Problems / Obstacles Encountered & How Each Was Solved

#### Problem 1: `duplicate case value` in `IOConfigGPIO` from Zero-Initialized `IOCFG_` Macros
- **Symptom:** During compilation of `io_aries.c`, the compiler failed with `error: duplicate case value 'case IOCFG_OUT_OD:' previously used here 'case IOCFG_OUT_PP:'`.
- **Root Cause:** In the initial target setup, all `IOCFG_*` macros in `platform.h` were temporarily defined as constant `0`, making all switch cases identical.
- **Solution:** Replaced static 0 definitions with the canonical Betaflight `IO_CONFIG(mode, speed, pupd)` bitfield packing scheme in `platform.h`. In `io_aries.c`, replaced the switch statement with direct mode extraction `uint8_t mode = cfg & 0x03` to cleanly distinguish outputs (`THEJAS32_GPIO_MODE_OUTPUT` / `THEJAS32_GPIO_MODE_OUTPUT_OD`) from inputs without duplicate case collisions.

#### Problem 2: Out-of-Bounds `ioRecs` Allocation due to `DEFIO_IO_USED_COUNT 0`
- **Symptom:** `src/main/drivers/io.c` allocated `ioRecs` with size 1 when `DEFIO_IO_USED_COUNT` was 0, causing `IOInitGlobal()` and pin index access to access unallocated memory.
- **Root Cause:** ARIES target lacked a populated `io_def_generated.h`.
- **Solution:** Configured `#define DEFIO_PORT_PINS 64` in `platform.h` (matching the pattern used by ESP32 and RP2350 ports), and authored `src/platform/ARIES/io_def_generated.h` defining 32 pins (`PA0` through `PA31`), setting `DEFIO_IO_USED_COUNT = 32` and `DEFIO_USED_COUNT = 32`.

#### Problem 3: Multi-Bank GPIO Interrupt Routing and EXTI Callback Dispatch
- **Symptom:** THEJAS32 SoC routes GPIO interrupts as two distinct interrupt lines from the interrupt controller: IRQ 10 (`THEJAS32_IRQ_GPIO0` for Bank 0, pins 0–15) and IRQ 11 (`THEJAS32_IRQ_GPIO1` for Bank 1, pins 16–31). Betaflight's `EXTIConfig` API expects a unified pin abstraction.
- **Root Cause:** A single monolithic EXTI handler would miss bank separation in the interrupt controller.
- **Solution:** Implemented `thejas32_gpio0_isr()` and `thejas32_gpio1_isr()`, registered to IRQ 10 and 11 via `thejas32_interrupt_register`. In `EXTIConfig()`, determined the bank from the pin index (`pin < 16 ? THEJAS32_IRQ_GPIO0 : THEJAS32_IRQ_GPIO1`), enabled the appropriate IRQ line, and stored the callback in `extiChannels[pin]`.

---

### 4. Build & Binary Verification

- **Compilation Commands:**
  ```bash
  make TARGET=ARIESV3 -j$(nproc)
  make TARGET=ARIESV3 hex
  ```
- **Exit Code:** `0` (Success)
- **Memory Footprint:**
  ```text
     text    data     bss     dec     hex filename
   762701    6396   61664  830761   cad29 ./obj/main/betaflight_ARIESV3.elf
  ```
  - **Flash Usage:** ~762.7 KB / 2,048 KB (37.2%)
  - **RAM Usage:** ~66.5 KB / 256 KB (26.0%)

- **Symbol Inspection:**
  - `ioRecs` allocated with 32 records (`32 * 12 bytes = 384 bytes`) in BSS (`0x1000BC30`).
  - `extiChannels` allocated in BSS (`0x10001C78`).
  - `thejas32_gpio0_isr` linked at `0x2000113C`.
  - `thejas32_gpio1_isr` linked at `0x20001198`.
  - `IOInitGlobal` linked at `0x200010F8`.
  - `IOConfigGPIO` linked at `0x2000395C`.
  - `IOHi` linked at `0x200039D4` and `IOLo` linked at `0x20003A94`.
  - `IORead` linked at `0x20003B30`.
  - `EXTIConfig` linked at `0x2009EE30`.
  - Hex artifact generated: `obj/betaflight_2026.12.0-alpha_ARIESV3.hex`.

---

## 🛠️ Phase 5 Detailed Execution Log

### 1. Objective
Implement the hardware SPI master bus driver for C-DAC THEJAS32 / VEGA ET1031 (mapping SPI0 controller at `0x10000600` as Betaflight `SPI1`). Provide polled segment transfers (`spiProcessSegmentsPolled`), chip select control with Active After Transfer (`CSAAT`), pin configuration on Header J1 (SCK `PA5`, MISO `PA6`, MOSI `PA7`, CS `PA8`), gyro EXTI interrupt line (`PA0`), and enable Betaflight IMU drivers (`ICM42688P` and `BMI270`).

### 2. Files Created & Modified

| File Path | Action | Purpose & Technical Summary |
| :--- | :---: | :--- |
| [`src/platform/ARIES/bus_spi_aries.c`](file:///home/ivan/Desktop/betaflight/src/platform/ARIES/bus_spi_aries.c) | **Created** | Hardware SPI driver implementing `spiInitDevice()`, `spiPinConfigure()`, `spiProcessSegmentsPolled()`, `spiInternalReadWriteBufPolled()`, and clock prescaler calculations based on the 100 MHz peripheral clock. Mapped SPI control word registers (`Data_control`, `Clock_control`), status polling, and multi-segment transfers. |
| [`src/platform/ARIES/include/platform/bsp_thejas32.h`](file:///home/ivan/Desktop/betaflight/src/platform/ARIES/include/platform/bsp_thejas32.h) | **Modified** | Added SPI register struct definitions (`thejas32_spi_t`), status register bits (`THEJAS32_SPI_STATUS_*`), control bits (`THEJAS32_SPI_CTRL_*`), and pointer macro `#define SPI1 ((spiResource_t *)THEJAS32_SPI_0_1_BASE)`. |
| [`src/platform/ARIES/include/platform/platform.h`](file:///home/ivan/Desktop/betaflight/src/platform/ARIES/include/platform/platform.h) | **Modified** | Included `platform/bsp_thejas32.h` to make `SPI1` visible to `bus_spi.c`. |
| [`src/platform/ARIES/target/ARIESV3/target.h`](file:///home/ivan/Desktop/betaflight/src/platform/ARIES/target/ARIESV3/target.h) | **Modified** | Enabled `#define USE_SPI`, `#define USE_SPI_DEVICE_1`, `#define USE_GYRO_SPI_ICM42688P`, `#define USE_GYRO_SPI_BMI270`, `#define USE_ACC_SPI_ICM42688P`, `#define USE_ACC_SPI_BMI270`. Configured pin definitions: `SPI1_SCK_PIN PA5`, `SPI1_SDI_PIN PA6`, `SPI1_SDO_PIN PA7`, `SPI1_MISO_PIN PA6`, `SPI1_MOSI_PIN PA7`, `GYRO_1_CS_PIN PA8`, and `GYRO_1_EXTI_PIN PA0`. |
| [`src/platform/ARIES/mk/ARIES.mk`](file:///home/ivan/Desktop/betaflight/src/platform/ARIES/mk/ARIES.mk) | **Modified** | Added `ARIES/bus_spi_aries.c` to `MCU_COMMON_SRC`. |
| [`src/platform/ARIES/stub_drivers.c`](file:///home/ivan/Desktop/betaflight/src/platform/ARIES/stub_drivers.c) | **Modified** | Removed dummy SPI fallback stubs (`spiInitDevice`, `spiPinConfigure`, `spiProcessSegmentsPolled`, etc.). |

---

### 3. Problems / Obstacles Encountered & How Each Was Solved

#### Problem 1: Undefined `SPI1_SDI_PIN` and `SPI1_SDO_PIN` Compilation Error
- **Symptom:** When compiling `src/main/pg/bus_spi.c`, the compiler failed with `'SPI1_SDI_PIN' undeclared` and `'SPI1_SDO_PIN' undeclared`.
- **Root Cause:** Upstream Betaflight's parameter group macro `PG_REGISTER_ARRAY_WITH_LOOKUP(spiPinConfig_t, ...)` expects `SPIx_SDI_PIN` and `SPIx_SDO_PIN` (SDI = Serial Data In = MISO; SDO = Serial Data Out = MOSI) rather than `SPIx_MISO_PIN` and `SPIx_MOSI_PIN`.
- **Solution:** Defined both `SPI1_SDI_PIN` (`PA6`) and `SPI1_SDO_PIN` (`PA7`), while maintaining `SPI1_MISO_PIN` and `SPI1_MOSI_PIN` as aliases in `target.h`.

#### Problem 2: Missing `SPI1` Peripheral Identifier in Target Abstraction
- **Symptom:** `src/main/drivers/bus_spi.c` failed to compile with `'SPI1' undeclared` inside `spiInstanceByDevice()`.
- **Root Cause:** Unlike ARM targets that include ST/NXP peripheral headers directly into `bus_spi.c`, the RISC-V ARIES target had not exposed a typed pointer symbol `SPI1`.
- **Solution:** Defined `#define SPI1 ((spiResource_t *)THEJAS32_SPI_0_1_BASE)` (`0x10000600UL`) in `bsp_thejas32.h` and ensured `platform.h` includes `bsp_thejas32.h` so `bus_spi.c` resolves `SPI1` cleanly.

#### Problem 3: Multi-Segment SPI Transfers & Chip Select Holding
- **Symptom:** Betaflight IMU reads often perform an address write followed by burst data reads over multiple segments. Releasing CS between segments causes register reads to abort.
- **Root Cause:** Standard SPI controllers drop CS between words unless configured for transfer continuity.
- **Solution:** In `bus_spi_aries.c`, configured the THEJAS32 SPI controller's `Bits.CSAAT` (Chip Select Active After Transfer) bit during segments where more data follows, and cleared it on the final segment of the transaction, ensuring uninterrupted burst register reads.

---

### 4. Build & Binary Verification

- **Compilation Commands:**
  ```bash
  make TARGET=ARIESV3 -j$(nproc)
  make TARGET=ARIESV3 hex
  ```
- **Exit Code:** `0` (Success)
- **Memory Footprint:**
  ```text
     text    data     bss     dec     hex filename
   765876    6396   61728  834000   cb9d0 ./obj/main/betaflight_ARIESV3.elf
  ```
  - **Flash Usage:** ~765.9 KB / 2,048 KB (37.4%)
  - **RAM Usage:** ~66.5 KB / 256 KB (26.0%)

- **Symbol Inspection:**
  - `spiProcessSegmentsPolled` linked at `0x20078b28`.
  - `spiPinConfigure` linked at `0x20098228`.
  - `bmi270Detect` linked at `0x20080804`.
  - `bmi270SpiAccInit` linked at `0x200795b0` and `bmi270SpiGyroInit` linked at `0x20080dd8`.
  - `spiBusDevice` allocated in SRAM BSS (`0x10009e90`).
  - Hex artifact generated: `obj/betaflight_2026.12.0-alpha_ARIESV3.hex`.

---

## 🛠️ Phase 6 Detailed Execution Log

### 1. Objective
Enable non-volatile configuration storage for Betaflight Parameter Groups (PG). Implement the platform flash configuration driver in `src/platform/ARIES/config_flash_aries.c` mapping the 32 KB reserved sector in memory-mapped NOR flash (`0x201F8000`–`0x201FFFFF`) to physical storage commands executed over the onboard SPI3 controller (`0x10200200`). Implement 4 KB Sector Erase (`0x20`), 256-byte Page Program (`0x02`), Write Enable (`0x06`), Status Register polling (`0x05`), and synchronize the memory-mapped shadow for immediate coherent reads and simulator execution.

### 2. Files Created & Modified

| File Path | Action | Purpose & Technical Summary |
| :--- | :---: | :--- |
| [`src/platform/ARIES/config_flash_aries.c`](file:///home/ivan/Desktop/betaflight/src/platform/ARIES/config_flash_aries.c) | **Created** | Hardware flash configuration driver implementing Betaflight config streamer API: `configFlashInit()`, `configUnlock()`, `configLock()`, `configClearFlags()`, and `configWriteWord()`. Low-level SPI3 driver commands: `thejas32_flash_spi_init`, `thejas32_flash_erase_sector` (4 KB sector erase with 24-bit address), `thejas32_flash_write_page` (Page Program with WIP polling), and memory shadow synchronization. |
| [`src/platform/ARIES/include/platform/platform.h`](file:///home/ivan/Desktop/betaflight/src/platform/ARIES/include/platform/platform.h) | **Modified** | Guarded `#ifndef FLASH_CONFIG_BUFFER_TYPE` (`uint32_t`) and defined `#ifndef FLASH_CONFIG_STREAMER_BUFFER_SIZE` (`32`). |
| [`src/platform/ARIES/target/ARIESV3/target.h`](file:///home/ivan/Desktop/betaflight/src/platform/ARIES/target/ARIESV3/target.h) | **Modified** | Enabled `#define CONFIG_IN_FLASH`, `#define FLASH_SECTOR_SIZE 4096`, `#define FLASH_PAGE_SIZE 4096`, `#define FLASH_CONFIG_STREAMER_BUFFER_SIZE 32`, `#define FLASH_CONFIG_BUFFER_TYPE uint32_t`, `#define EEPROM_SIZE 8192`, and `#define ENABLE_CONFIG_FLASH_INIT`. |
| [`src/platform/ARIES/mk/ARIES.mk`](file:///home/ivan/Desktop/betaflight/src/platform/ARIES/mk/ARIES.mk) | **Modified** | Added `ARIES/config_flash_aries.c` to `MCU_COMMON_SRC`. |
| [`src/platform/ARIES/stub_drivers.c`](file:///home/ivan/Desktop/betaflight/src/platform/ARIES/stub_drivers.c) | **Modified** | Removed dummy configuration streamer stubs (`configUnlock`, `configLock`, `configClearFlags`, `configWriteWord`). |

---

### 3. Problems / Obstacles Encountered & How Each Was Solved

#### Problem 1: Conflicting Declarations of `__config_start` and `__config_end`
- **Symptom:** Compilation failed with `error: conflicting types for '__config_start'; have 'uint8_t' {aka 'unsigned char'}`, previously declared in `src/main/target/common_post.h:792` as `struct linker_symbol`.
- **Root Cause:** `common_post.h` uses opaque `struct linker_symbol` types for linker-provided addresses (`&__config_start`).
- **Solution:** Removed redeclarations from `config_flash_aries.c`, referencing the symbol addresses via address-of `(uintptr_t)&__config_start` and `(uintptr_t)&__config_end`.

#### Problem 2: Flash Controller Timeout Guards for Simulation & Renode
- **Symptom:** Renode simulation does not currently model the SPI3 controller peripheral at `0x10200200`. Polling SPI3 status bits (`TX_HOLD_EMPTY`, `RX_COMPLETE`, `BUSY`) in a tight loop without a timeout would hang during `save` operations.
- **Root Cause:** Unmodeled peripheral reads return `0`, preventing status bits from asserting.
- **Solution:** Added bounded loop countdown timeouts (`100,000` cycles) to all SPI status register wait loops (`thejas32_flash_wait_idle`, `thejas32_flash_tx_byte`, `thejas32_flash_rx_byte`, and `thejas32_flash_wait_wip`).

#### Problem 3: Dual Physical & Memory-Mapped Shadow Synchronization
- **Symptom:** Betaflight's config verification immediately performs memory-mapped pointer reads from `(const uint8_t *)&__config_start` via `isEEPROMStructureValid()` right after `configWriteWord()`.
- **Root Cause:** In Renode simulation and environments where cache or bus translation requires memory updates, SPI commands alone do not write to CPU `MappedMemory`.
- **Solution:** `configWriteWord()` performs both the physical SPI3 sector erase / page program commands for real THEJAS32 hardware AND updates the memory-mapped flash window directly (`memset(..., 0xFF)` on erase, `memcpy(..., buffer)` on write), with a RISC-V memory `fence` instruction.

---

### 4. Build & Binary Verification

- **Compilation Commands:**
  ```bash
  make TARGET=ARIESV3 -j$(nproc)
  make TARGET=ARIESV3 hex
  ```
- **Exit Code:** `0` (Success)
- **Memory Footprint:**
  ```text
     text    data     bss     dec     hex filename
   768296    6416   61728  836440   cc358 ./obj/main/betaflight_ARIESV3.elf
  ```
  - **Flash Usage:** ~768.3 KB / 2,048 KB (37.5%)
  - **RAM Usage:** ~66.5 KB / 256 KB (26.0%)

- **Symbol Inspection:**
  - `configWriteWord` linked at `0x20004064` (validates `0x201F8000`–`0x20200000` bounds, checks `lastErasedSector`, triggers SPI3 page program, writes to memory shadow).
  - `thejas32_flash_write_enable` linked at `0x2009fe40`.
  - `lastErasedSector` allocated in BSS at `0x100014F0`.
  - Hex artifact generated: `obj/betaflight_2026.12.0-alpha_ARIESV3.hex` (2,179,135 bytes).

---

## 🚀 Next Steps: Phase 7 (Actuator Outputs & RC Link)
1. Implement `src/platform/ARIES/pwm_motor_aries.c` mapping hardware PWM outputs (channels 0–3 for 4 quadcopter motors) on header pins.
2. Implement motor output APIs: `motorPwmDevInit()`, `pwmWriteMotor()`, and DShot fallback stub.
3. Configure `UART1` (`0x10000200`) on header pins (`J2_13` TX, `J2_15` RX) for Serial RX (CRSF / ELRS / SBUS).
4. Remove motor and RC receiver stubs from `stub_drivers.c`.
5. Verify motor output scaling and RC receiver stick channel decoding.



