# Retrospective Log 05: Phased Software Test Plan Architecture

> **Source File:** [`oldfiles/logs/SOFTWARE_TEST_PLAN_PHASES.md`](../../oldfiles/logs/SOFTWARE_TEST_PLAN_PHASES.md)  
> **Target Platform:** C-DAC THEJAS32 SoC (VEGA ET1031 RV32IM @ 100 MHz, 256 KB SRAM, No XIP)  
> **Analysis Date:** 2026-11-17  
> **Status:** Completed

---

## 1. Executive Summary & Purpose

`SOFTWARE_TEST_PLAN_PHASES.md` is an extensive, 1,396-line master test specification defining the modular testing infrastructure and 8-phase execution roadmap for the ARIES v3.0 port:
1. **Part A: Modular Test Infrastructure (Modules M0–M3):**
   - `M0`: Memory-Mapped I/O (MMIO) Mock Engine (`mock_mmio.h/.c`) with shadow RAM arrays and access logging.
   - `M1`: RISC-V Control and Status Register (CSR) Mock (`mock_csr.h/.c`) simulating 64-bit `mcycle` counters.
   - `M2`: THEJAS32 Peripheral Mocks (`mock_thejas32.h/.c`) for interrupt controllers and handlers.
   - `M3`: Test Helpers & Utilities (`test_helpers.h/.c`) for sequential pattern verification and address formulas.
2. **Part B: Driver Test Modules (T1–T8):**
   - Unit tests covering System Core (`T1`), UART (`T2`), GPIO (`T3`), EXTI (`T4`), SPI (`T5`), Flash Config (`T6`), Motor PWM (`T7`), and Build Verification (`T8`).
3. **Part C: Phased Execution Roadmap (Phases 1–8):**
   - Step-by-step implementation guide with C test shims (`aries_*_testable.c`), Google Test assertions, and `src/test/Makefile` registrations.

---

## 2. Decision & Action Scoring Matrix (0 to 10)

| ID | Module / Phase | Decision / Action Description | Category | Score (0–10) | Justification & Technical Impact |
| :--- | :--- | :--- | :--- | :---: | :--- |
| **D01** | M0 Infrastructure | Host MMIO Mocking with Shadow RAM Buffers & Access Logging | Test Infrastructure | **8.5 / 10** | **Clean Software Abstraction.** Allowed driver functions to read/write hardware registers without segmentation faults on x86 host, recording read/write counts for verification. |
| **D02** | M0 Memory Map | Designing mock memory around `0x10000000` (RAM) and `0x20000000` (Flash) | Memory Model | **1.5 / 10** | **Codifying an Architectural Myth.** Institutionalized the false belief that `0x10000000` was SRAM and `0x20000000` was XIP flash in the test harness itself. |
| **D03** | M1 Infrastructure | Cycle Counter Simulation with `mock_csr_advance()` & `mock_csr_set()` | Test Double | **9.0 / 10** | **Effective Timebase Simulation.** Enabled deterministic testing of timebase functions, micros/millis scaling, and rollover behavior without requiring live hardware timers. |
| **D04** | Test Shims Strategy | Compiling test-specific shims (`aries_system_testable.c`) to isolate logic from hardware assembly | Test Architecture | **8.5 / 10** | **Pragmatic Host Compilation.** Extracted pure calculation and state-machine logic away from inline assembly (`csrr`, `fence`), allowing Google Test compilation on x86 Linux. |
| **D05** | T1 Tests | Verification of 32-bit rollover, conversion math, and IRQ table bounds in `system.c` | Logic Verification | **9.0 / 10** | **Exhaustive Unit Testing.** Verified edge cases including 32-bit `micros()` wrap at $2^{32}-1$, clock cycles to micros conversions, and `handle_trap` exception vs interrupt routing. |
| **D06** | T2 Tests | Testing UART baud divisors, 256-byte ring buffer wraparound, and 300-byte streaming | Buffer Verification | **9.0 / 10** | **Robust Ring Buffer Testing.** Verified that streaming 300 sequential bytes through a 256-byte circular FIFO wrapped cleanly without memory corruption. |
| **D07** | T3 Tests | Explicit verification of GPIO address-decode bit-shift formulas for Bank 0 & Bank 1 | Hardware Formula | **9.0 / 10** | **Accurate Address Verification.** Validated the THEJAS32 bit-shift address decode math: `base + ((1U << pin) << 2)` for Bank 0 and Bank 1 (Heartbeat LED `PA22` = `base + 0x100`). |
| **D08** | T7 Tests | Motor PWM sorting and Timer 1 sub-microsecond pulse deadline queue simulation | Actuator Testing | **8.5 / 10** | **Clever State Machine Test.** Verified that motors are sorted in ascending pulse order and that successive Timer 1 ISR executions drop pins sequentially and coalesce deltas $< 50$ cycles. |
| **D09** | T8-02 / T8-03 Checks | Setting Flash ceiling at 800 KB and RAM ceiling at 80 KB in `build_verify.sh` | Budget Verification | **1.5 / 10** | **Grossly Disconnected from Hardware Limits.** Permitted a binary up to 800 KB, when physical THEJAS32 hardware can only load 248 KB into SRAM! |
| **D10** | T8-04 Check | Asserting `_start == 0x20000000` in `build_verify.sh` | Regression Assertion | **1.0 / 10** | **Automated Gatekeeper for Broken Code.** The test script literally failed if the entry point was set to the correct physical SRAM boot address (`0x00202000`). |
| **D11** | T8-06 Check | Automated objdump regex audit ensuring zero double-precision routines in PID loops | Static Binary Audit | **9.5 / 10** | **Brilliant Automated Quality Gate.** Scripted verification ensuring that soft-float RV32IM never executed expensive `__adddf3`/`__muldf3` calls in time-critical control paths. |
| **D12** | T8-07 Check | Automated objdump check ensuring zero `__udivdi3` software division calls in `micros()` | Static Binary Audit | **9.5 / 10** | **Zero-Overhead Enforcement.** Ensured `micros()` remained a fast, pure leaf function with stack allocation eliminated. |

---

## 3. Deep-Dive Critique: Architectural Highlights vs. Blind Spots

### 3.1 The Brilliant Architectural Achievements
1. **The Static Binary Audit Checks (`T8-06` and `T8-07`)**:
   - Rather than hoping developers wouldn't accidentally use double-precision literals (e.g. `1.0` instead of `1.0f`), the plan integrated automated assembly auditing:
     ```bash
     PID_DOUBLES=$("${TOOLCHAIN}/riscv32-esp-elf-objdump" -d "$ELF" | \
         sed -n '/subTaskPidController>:/,/^$/p' | \
         grep -cE "(__adddf3|__subdf3|__muldf3|__divdf3)" || true)
     [ "$PID_DOUBLES" -eq 0 ] && pass "Zero double-precision in PID" || fail
     ```
   - This guaranteed that software floating-point performance remained deterministic.
2. **Modular Test Infrastructure Architecture (M0–M3)**:
   - Defining clean, reusable test doubles allowed writing over 130 tests with zero code duplication across test suites.
   - Access logging in `M0` (`mock_mmio_log`) made it possible to verify exact register sequences (e.g. setting DLAB=1, writing DLL/DLH, then clearing DLAB=0).
3. **Motor Pulse Coalescing Test (`T7-14`)**:
   - The test verified that motor pulses differing by $< 50$ cycles ($< 0.5\,\mu\text{s}$) are coalesced and dropped in the same interrupt handler, avoiding impossible timer reloads smaller than interrupt overhead.

---

### 3.2 The Critical Flaws and Blind Spots
1. **The 800 KB Mirage**:
   - The document repeatedly specifies:
     ```text
     [ "$FLASH_KB" -le 800 ] && pass "Flash ${FLASH_KB} KB ≤ 800 KB"
     ```
   - Why 800 KB? Because STM32F405 has 1 MB flash! The team simply ported STM32 testing assumptions without checking THEJAS32 bootloader constraints.
   - Result: A 760 KB binary happily passed CI, but when flashed to actual hardware, the bootloader copied the first 250 KB and jumped into a chopped-off binary.
2. **Host-Mocking Concealed Memory Architecture Errors**:
   - Because `mock_periph_mem` was just a C array in x86 RAM, dereferencing `0x1000FEF0` as the stack pointer worked perfectly in the unit test!
   - On physical silicon, writing to `0x1000FEF0` attempted an invalid bus transaction in the peripheral address aperture.
3. **Missing Toolchain & Opcode Audits**:
   - The original plan had checks for soft-float double precision, but **zero checks for RISC-V Compressed instructions (RVC)**.
   - When the compiler toolchain linked `libc.a` with RVC opcodes, the test suite had no way to catch it, leading to immediate illegal instruction crashes on physical silicon.

---

## 4. Actionable Directives for Our 128 KB SRAM Build

1. **Update Binary Ceiling Assertions in `build_verify.sh`:**
   - Flash/SRAM Footprint ceiling: Set to **$\le 131,072$ bytes (128 KB)**.
   - Entry point assertion: Assert `_start == 0x00202000` (never `0x20000000`).
   - Trap entry assertion: Assert `trap_entry == 0x002020f0` (never `0x200000f0`).
2. **Preserve Static Binary Audits in CI:**
   - Keep `T8-06` (0 double-precision calls in PID).
   - Keep `T8-07` (0 `__udivdi3` calls in `micros()`).
   - Add `T8-09` (0 compressed 16-bit instructions via `grep -E '\b(c\.[a-z]+)'`).
   - Add `T8-10` (raw binary artifact $\le 128\,\text{KB}$).
3. **Retain Fast Test Shims:**
   - Reuse the `aries_*_testable.c` patterns to verify MSP packet parsing, ring buffer wrap-around, and baud rate math.

---
*Retrospective log compiled and verified against historical `oldfiles/logs/SOFTWARE_TEST_PLAN_PHASES.md` records.*
