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
| **Phase 2** | Console & Communication | UART0 (CLI/MSP), Ring Buffer, RX Interrupt | *In Progress* | — |
| **Phase 3** | High-Resolution Timebase | Hardware Timer 0/1, Monotonic `micros()`, Jitter Check | *Pending* | — |
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

## 🚀 Next Steps: Phase 2 (Console & Communication)
1. Implement `src/platform/ARIES/serial_uart_aries.c` utilizing the THEJAS32 UART0 registers (`0x10000100`).
2. Integrate a transmit/receive ring buffer and hook UART0 RX into `thejas32_interrupt_register(THEJAS32_IRQ_UART0, ...)`.
3. Enable `#define USE_UART0` and connect UART0 to Betaflight's CLI / MSP serial engine.
4. Verify interactive CLI response (`# status`) over the CP2102N USB-UART bridge at 115200 baud.
