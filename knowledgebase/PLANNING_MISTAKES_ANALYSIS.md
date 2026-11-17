# Historical Planning Mistakes Analysis: C-DAC ARIES v3.0 Previous Attempt

**Document ID**: `PLAN-MISTAKE-LOG-01`  
**Inspected Source Files**:
* `oldfiles/logs/IMPLEMENTATION_PLAN.md`
* `oldfiles/logs/IN_SRAM_REMEDIATION_PLAN.md`
* `oldfiles/logs/PLAN_TODO.txt`
* `oldfiles/logs/DETAILED_DUMMY_PLAN.md`
* `oldfiles/logs/RISK_ASSESMENT.md`
* `oldfiles/logs/TEST_PLAN.md`

---

## 1. Executive Summary

A rigorous audit of the planning documents from the previous porting attempt reveals that the failure of the initial deployment was **not** caused by low-level syntax bugs or C compiler errors, but by **fundamental architectural and scoping mistakes committed during the planning phase**.

The previous team achieved clean compilation and linking of a "dummy" build early on, which generated a false sense of security. However, the foundational assumptions in their plans regarding **memory topology, flash execution capability, binary size budget, emulation parameters, and peripheral registers** directly contradicted the physical silicon reality of the C-DAC THEJAS32 SoC.

---

## 2. Deep-Dive Taxonomy of Planning Mistakes

### Mistake 1: The Execute-in-Place (XIP) External Flash Assumption
* **The Planning Mistake**:
  In `IMPLEMENTATION_PLAN.md` (Section 1.1) and `DETAILED_DUMMY_PLAN.md` (Section 3), the team planned for code execution to run directly from the 2 MB external SPI NOR Flash via Execute-In-Place (XIP):
  ```ld
  MEMORY
  {
      FLASH (rx)  : ORIGIN = 0x20000000, LENGTH = 2048K
      RAM   (rwx) : ORIGIN = 0x10000000, LENGTH = 256K
  }
  ```
* **The Silicon Reality**:
  The THEJAS32 SoC (incorporating the VEGA ET1031 RV32IM core) **does not support hardware XIP over SPI Flash**. The external Winbond 25Q16 NOR flash is an offline storage peripheral connected via SPI3 (`0x10200200`). It has no hardware cache, no memory-mapped read controller, and the CPU bus cannot fetch instructions from `0x20000000`.
* **The Consequence**:
  The firmware was linked to execute at virtual address `0x20000000`. When the hardware bootloader loaded code into internal SRAM and jumped to `0x00202000`, all function calls and PC-relative branches targeted non-existent flash memory, immediately locking the core into a synchronous instruction bus fault!

---

### Mistake 2: The Imaginary SRAM Base Address (`0x10000000`)
* **The Planning Mistake**:
  Across `IMPLEMENTATION_PLAN.md`, `DETAILED_DUMMY_PLAN.md`, `PLAN_TODO.txt`, and `RISK_ASSESMENT.md`, the planners declared that the 256 KB internal SRAM started at base address `0x10000000`:
  ```
  SRAM (RAM): 0x10000000 - 0x10040000 | 256 KB
  Stack Pointer: _estack = 0x1000FEF0
  ```
* **The Silicon Reality**:
  In the THEJAS32 memory map, `0x10000000` is the **APB Peripheral Register Bus**, NOT internal SRAM:
  * `0x10000000`: GPIO controller
  * `0x10000100`: UART0 controller
  * `0x10000600`: PWM controller
  The actual physical internal SRAM is mapped at **`0x00200000` to `0x0023E7FF`** (250 KB usable).
* **The Consequence**:
  The linker placed `.data`, `.bss`, and the runtime execution stack directly on top of peripheral control registers! The very first function call executed by `Reset_Handler` attempted to push stack frames into unmapped peripheral address space, causing memory bus exceptions before reaching `main()`.

---

### Mistake 3: Unbounded Feature Scope & Sizing Planning (`MCU_FLASH_SIZE := 2048`)
* **The Planning Mistake**:
  In `target.mk` and `DETAILED_DUMMY_PLAN.md`, the planners configured:
  ```makefile
  MCU_FLASH_SIZE := 2048
  ```
  They assumed that because the physical board had a 2 MB SPI flash chip, they had 2,048 KB of program memory.
* **The Consequence (The Bootloader Truncation Catastrophe)**:
  1. In Betaflight's build system, setting `MCU_FLASH_SIZE := 2048` automatically marks the target as a "Large Flash" board, enabling heavy features:
     * Full OSD rendering engine with SD/HD fonts (~120 KB)
     * Blackbox filesystem, SDCard SPI, and FATFS (~60 KB)
     * GPS navigation, return-to-home, and dead-reckoning (~90 KB)
     * Over 15 obsolete serial receiver protocols (Spektrum, FlySky, Futaba, etc.)
  2. The compiled binary grew to **708 KB – 760 KB**.
  3. The C-DAC on-chip ROM bootloader can only copy a maximum of **250 KB** into internal SRAM!
  4. When the user flashed the board, the bootloader copied only the first 250 KB of the 760 KB binary, leaving 510 KB of the code uncopied, and jumped into a severely truncated, fragmented binary that instantly panicked!

---

### Mistake 4: Emulation-Induced Blindness (Renode Simulator vs Physical Silicon)
* **The Planning Mistake**:
  In `RISK_ASSESMENT.md` (Section 4), the team set up a custom Renode emulator script with an imaginary memory map:
  * CPU: `rv32imc` (with compressed instructions enabled!)
  * Flash: 2 MB XIP Flash mapped at `0x20000000`
  * RAM: 256 KB SRAM mapped at `0x10000000`
* **The Consequence (False Sense of Security)**:
  1. In the Renode emulator, the firmware booted cleanly because the emulator gave the code an artificial XIP flash controller and placed RAM at `0x10000000`.
  2. The team celebrated in `PLAN_TODO.txt`:
     > *"We have already completed the two most difficult architectural phases... 100% clean linkage... Memory limits are NOT a problem: Flash 34.5% used, RAM 24.9% used."*
  3. This completely masked the reality that the binary was 300% too large to fit in physical SRAM and linked to the wrong physical addresses!

---

### Mistake 5: Flashing Protocol & File Format Planning
* **The Planning Mistake**:
  The team planned to use the C-DAC batch script `flasher.bat` by passing the standard Betaflight Intel HEX build deliverable:
  ```cmd
  flasher.bat COM<X> betaflight_ARIESV3.hex
  ```
* **The Silicon Reality**:
  `flasher.bat` uses XMODEM stream transfer to dump raw binary bytes directly into SPI Flash. It has no Intel HEX parser.
* **The Consequence**:
  The flasher took the ASCII text representation of the hex file (`:10000000010000...`) and burned ASCII text into flash! When the bootloader copied the flash to SRAM, the CPU attempted to execute ASCII numbers as RISC-V opcodes, causing immediate illegal instruction traps!

---

### Mistake 6: Conflicting Peripheral Register Planning Across Documents
* **The Planning Mistake**:
  Planning documents authored by the previous team contained contradictory register base addresses:
  * **UART0 Base**: `PLAN_TODO.txt` line 145 specified `0x11200000`, while `IMPLEMENTATION_PLAN.md` line 20 specified `0x10000100`.
  * **SPI0 vs PWM**: `IMPLEMENTATION_PLAN.md` line 23 specified `SPI0 = 0x10000600`, but according to the THEJAS32 programmer's manual, `0x10000600` is the dedicated **8-channel PWM controller**!
  * **GPIO Mapping**: `IMPLEMENTATION_PLAN.md` planned a complex address-bit shifting scheme for GPIO (`0x10080000` and `0x10180000`) instead of utilizing the flat 32-bit GPIO controller at `0x10000000`.

---

### Mistake 7: Timebase Counter Rollover Planning
* **The Planning Mistake**:
  In `DETAILED_DUMMY_PLAN.md` (Section 4), the planners implemented `micros()` using a naive 32-bit read of the machine cycle counter `mcycle`:
  ```c
  timeUs_t micros(void) {
      return (timeUs_t)(getCycleCounter() / (SystemCoreClock / 1000000));
  }
  ```
* **The Silicon Reality**:
  At 100 MHz, a 32-bit counter overflows in:
  $$\frac{2^{32} - 1}{100{,}000{,}000} \approx 42.949\,\text{seconds}$$
* **The Consequence**:
  Every 43 seconds, the microsecond timebase jumped backwards to zero. In Betaflight's cooperative scheduler, tasks scheduled with `cmpTimeUs(currentTime, lastExecution)` evaluated backward deltas as massive future deadlines, causing the entire flight loop and sensor filtering pipeline to freeze!
  They then planned complex reload logic for Synopsys `Timer 0`, completely failing to plan around the native 64-bit monotonically increasing `mtime` up-counter (`0x0200BFF8`) provided by the RISC-V CLINT.

---

## 3. Comparative Summary: Previous Attempt vs. New Rebuild Plan

| Planning Dimension | Previous Attempt (Old Files) | New Rebuild Architecture Plan |
| :--- | :--- | :--- |
| **Execution Memory** | External SPI Flash XIP at `0x20000000` (Did not exist!) | **Internal SRAM at `0x00200000`** (Single unified allocator) |
| **RAM Base Address** | `0x10000000` (Actually peripheral register bus!) | **`0x00200000`** (Verified physical internal SRAM) |
| **Binary Sizing Strategy**| `MCU_FLASH_SIZE := 2048` (Bloated to 760 KB $\rightarrow$ Truncated) | **Modular Stripping (<128 KB Target)**: Fits easily in 250 KB SRAM |
| **Validation Strategy** | Renode emulator with fake XIP memory map | **Physical Silicon Alignment**: Backed by datasheets & 25 logs |
| **Flashing Deliverable** | `.hex` file passed to raw XMODEM flasher (Burned ASCII) | **Raw `.bin` binary** passed to flasher |
| **Timebase Implementation**| 32-bit `mcycle` (43-second rollover crash) | **64-bit `mtime` up-counter** at 100 MHz (>5,800 years rollover) |
| **GPIO Driver** | Complex address bit-shift banks (`0x10080000`) | **Flat 32-bit GPIO (`0x10000000`)** with $O(1)$ array indexing |
| **USB Serial** | Unclear USB vs UART planning | **UART0 @ 115200 directly feeding CP2102N** (Zero USB middleware) |

---

## 4. Key Lessons Learned for Current Development

1. **Verify Physical Memory Maps Against Programmer Manuals, Not Assumptions**: Never assume an MCU supports XIP over SPI Flash unless an on-chip memory-mapped cache controller is explicitly documented.
2. **Size the Binary to the Load Target from Day One**: When executing from SRAM, all features not required for basic flight must be stripped upfront (`-Os`, `#undef USE_OSD`, `#undef USE_BLACKBOX`, `#undef USE_GPS`).
3. **Never Trust Simulation Without Real Register Parity**: A simulator that passes code on an unverified memory map is worse than no simulator at all.
4. **Binary Deliverables Must Match Flashing Mechanics**: XMODEM bootloaders require raw machine code `.bin` files, never Intel HEX ASCII text.
