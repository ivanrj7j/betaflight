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
| **Phase 4** | GPIO & External Interrupts | Bank 0/1 Address-Decoded IO, EXTI (Gyro DRDY) | *Pending* | — |
| **Phase 5** | SPI Bus & IMU Sensor Driver | SPI0 Master with `CSAAT`, ICM42688P / BMI270 Detection | *Pending* | — |
| **Phase 6** | Non-Volatile Config Storage | SPI3 Winbond NOR Flash, Parameter Group (PG) Save/Load | *Pending* | — |
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

## 🚀 Next Steps: Phase 4 (GPIO & External Interrupts)
1. Implement `src/platform/ARIES/io_aries.c` mapping THEJAS32 address-decoded GPIO (`0x10080000` for Bank 0, `0x10180000` for Bank 1).
2. Implement `IOInitGlobal()`, `IOConfigGPIO()`, `IOHi()`, `IOLo()`, `IOToggle()`, `IORead()`, and `IOWrite()`.
3. Implement `src/platform/ARIES/exti_aries.c` routing GPIO 0 interrupt (Gyro DRDY pin on `J2_11`) through the interrupt controller (`0x20010000`).
4. Connect EXTI callback to Betaflight gyro task trigger and verify status LED toggling.


