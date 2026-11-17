# Retrospective Log 06: Low-Level Software Test Plan & Verification Specs

> **Source File:** [`oldfiles/logs/SOFTWARE_TEST_PLAN.md`](file:///home/ivan/Desktop/betaflight/oldfiles/logs/SOFTWARE_TEST_PLAN.md)  
> **Target Platform:** C-DAC THEJAS32 SoC (VEGA ET1031 RV32IM @ 100 MHz, 256 KB SRAM, No XIP)  
> **Analysis Date:** 2026-11-17  
> **Status:** Completed

---

## 1. Executive Summary & Purpose

`SOFTWARE_TEST_PLAN.md` defines the low-level unit, integration, and target hardware test cases for the ARIES v3.0 flight controller across three environments:
1. **Environment A (Host Unit Tests):** Algorithmic verification, circular ring buffers, filter math, and timebase scaling.
2. **Environment B (Headless Renode Simulation):** Reset sequence, context frames, and MMIO access.
3. **Environment C (Target Silicon & GDB):** Real-world peripheral validation, JTAG register checks, oscilloscope pulse timing, and interactive CLI commands.

It covers 8 core modules (`SW-BOOT`, `SW-UART`, `SW-TIME`, `SW-GPIO`, `SW-SPI`, `SW-FLASH`, `SW-MOTOR`, `SW-PERF`/`SW-SAFE`) and provides an automated bash test runner script.

---

## 2. Decision & Action Scoring Matrix (0 to 10)

| ID | Test / Spec | Decision / Action Description | Category | Score (0–10) | Justification & Technical Impact |
| :--- | :--- | :--- | :--- | :---: | :--- |
| **D01** | Environments (§1) | Three-Tier Testing Strategy: Host Mocks, Renode Emulation, Target Hardware | Testing Architecture | **9.0 / 10** | **Comprehensive Testing Pyramid.** Clear division of responsibility: algorithmic logic on host x86, instruction flow in Renode, and electrical timing on physical silicon. |
| **D02** | SW-BOOT-01 | GPR Zero-Initialization Verification (`x1`–`x31` cleared before C runtime) | Low-Level Initialization | **9.0 / 10** | **Essential Bare-Metal Hygiene.** Prevents non-deterministic behavior and uninitialized register state from leaking into C code. |
| **D03** | SW-BOOT-02 | Verifying Stack Pointer at `0x1000FEF0` | Memory Architecture | **1.0 / 10** | **Fatal Specification Error.** Explicitly asserted that stack pointer must point to `0x1000FEF0`. On THEJAS32, `0x10000000` is peripheral MMIO! Executing this writes stack frames into hardware registers. |
| **D04** | SW-BOOT-04 | Machine Trap Vector Routing (`mtvec == 0x200000F0`) | Trap Configuration | **2.0 / 10** | **Unexecutable Address.** Asserted `mtvec` in unmapped Flash space (`0x200000F0`) instead of internal SRAM (`0x002020F0`). |
| **D05** | SW-BOOT-05 | 32-Register Context Frame (128-byte aligned stack allocation) | ABI Compliance | **9.5 / 10** | **Robust Trap Handler.** Correctly implemented 16-byte aligned 128-byte stack frame (`addi sp, sp, -128`), preserving all caller and callee-saved GPRs across interrupts and exceptions. |
| **D06** | SW-BOOT-07 | Stack High-Water Mark Canary Check (`0xA5A5A5A5` pattern) | Safety & Diagnostics | **9.0 / 10** | **Pragmatic Memory Profiling.** Memory-scanning canary pattern accurately established that peak stack depth remained under 2 KB, proving 4–8 KB stack allocation was sufficient. |
| **D07** | SW-BOOT-08 | Bare-Metal RV32I `memset`/`memcpy`/`memmove` Verification | Runtime Safety | **9.5 / 10** | **Critical Silicon Protection.** Ensured standard memory routines execute without pulling in RVC 16-bit compressed instructions or triggering recursive loop optimization bugs. |
| **D08** | SW-BOOT-09 | Toolchain Multilib RVC Exclusion Audit in CI | Static Analysis | **10.0 / 10** | **Flawless Quality Gate.** Automated objdump verification guaranteeing exactly 0 compressed instructions across all executable code. |
| **D09** | SW-BOOT-10 | Early UART Bounded Timeout (50,000 cycles) & Stackless Hex Print | Diagnostic Safety | **9.5 / 10** | **Bulletproof Boot Diagnostics.** Prevented early UART FIFO saturation from locking the CPU, and used pure ALU nibble math to eliminate stack `memcpy` frames. |
| **D10** | SW-BOOT-11 | Physical In-SRAM Execution Limit ($\le 253,952$ bytes) Enforcement | Memory Safety | **9.5 / 10** | **Silicon Boundary Gate.** Strict assertion ensuring raw binary fits 100% within the C-DAC bootloader's 248 KB copy limit. |
| **D11** | SW-UART-05 | High-Speed MSP Frame Integrity Test (10,000 requests, 0% error) | Protocol Testing | **9.5 / 10** | **Exhaustive Protocol Stress.** 10,000 consecutive `MSP_STATUS` and `MSP_RAW_IMU` requests over CP2102N USB-UART verifying zero packet drops or CRC errors. |
| **D12** | SW-TIME-01/02 | Atomic CSR Double-Read & Reciprocal Multiplication Verification | Timebase Engineering | **9.5 / 10** | **Zero-Overhead Monotonic Clock.** Verified monotonicity across 1,000,000 reads and audited disassembly for zero `__udivdi3` software division calls. |
| **D13** | SW-SPI-01 | Chip Select Active After Transfer (`Bits.CSAAT`) Glitch Verification | Bus Driver Integrity | **9.0 / 10** | **Hardware Protocol Compliance.** Verified with logic analyzer that CS stays continuously low across 14-byte IMU burst reads without mid-burst deassertion glitches. |
| **D14** | SW-MOTOR-01 | Timer 1 IRQ 8 Sub-Microsecond Pulse Deadline Verification ($< 2\,\mu\text{s}$ jitter) | Actuator Precision | **9.0 / 10** | **Precision Actuator Control.** Validated 4-motor soft PWM with $< 0.5\%$ pulse width error and $< 2\,\mu\text{s}$ jitter on physical oscilloscope. |
| **D15** | SW-SAFE-01/02 | 4-Point Arming Lockout & 1.0s Failsafe Stage 2 Motor Cutoff | Safety Engineering | **9.5 / 10** | **Non-Negotiable UAV Safety.** Verified mandatory arming prevention flags (CLI active, throttle raised, gyro calibrating, no gyro) and immediate motor shutdown upon RC signal loss. |
| **D16** | Script Step 4 (§3) | Hardcoded `Expected: 0x20000000` assertion in automated runner | Script Consistency | **1.5 / 10** | **Regression Trap.** Even though `SW-BOOT-09` and `SW-BOOT-11` were updated with `0x00202000` in the text, the automated bash script at the bottom still checked `"${RESET_ADDR}" == "20000000"`. |

---

## 3. Deep-Dive: Brilliant Additions vs. The Internal Contradiction

### 3.1 The Brilliant Post-Silicon Additions (Score: 9.5/10)
`SOFTWARE_TEST_PLAN.md` contains some of the highest-quality test specifications in the repository:
1. **The Comprehensive Arming & Failsafe Battery (`SW-SAFE-01` & `SW-SAFE-02`)**:
   - Testing UAV safety is paramount: enforcing that the flight controller will refuse to arm if the CLI is active, if throttle is non-zero, or if the IMU is detached.
   - Verifying that when transmitter telemetry drops, Stage 2 failsafe completely zeroes motor PWM lines in $< 1.0\text{ second}$.
2. **Hardware Protocol Stress (`SW-UART-05`)**:
   - Streaming 10,000 binary MSP frames over UART0 to verify that the 16550 circular ring buffer and baud rate divisor (54) do not suffer from buffer overruns or byte framing drift.
3. **The Post-Hardware Hardening Tests (`SW-BOOT-08` through `SW-BOOT-11`)**:
   - Written specifically after the physical hardware reality emerged, these four test cases addressed the exact silicon traps: RVC multilib injection, stack `memcpy` crashes in early UART, and the 248 KB binary boundary.

---

### 3.2 The Glaring Internal Contradiction (Score: 1.5/10)
Look at the stark inconsistency between Module 1 and Section 3:
- In `SW-BOOT-09` (lines 174–179):
  > *"Enforce zero 16-bit compressed instructions across all executable code (`0x00202000` to `_etext`)."*
- In `SW-BOOT-11` (lines 197–205):
  > *"Ensure total flash binary size strictly respects the 248 KB physical SRAM bootloader boundary (`253,952 bytes`)."*
- BUT in `SW-BOOT-02` (lines 74–76):
  > *"Expected Result: `$sp` must equal `0x1000FEF0`."*
- AND in `SW-BOOT-04` (line 107):
  > *"Pass/Fail Criteria: `mtvec == 0x200000F0`."*
- AND in Section 3 Automated Test Runner (lines 628–636):
  ```bash
  echo "Reset Vector Address: 0x${RESET_ADDR} (Expected: 0x20000000)"
  if [ "${RESET_ADDR}" == "20000000" ] && [ "${TRAP_ADDR}" == "200000f0" ]; then
      echo "✅ PASS"
  else
      echo "❌ FAIL: Vector address mismatch!"
      exit 1
  fi
  ```
The author added new test cases (`SW-BOOT-08` to `SW-BOOT-11`) acknowledging `0x00202000` and the 248 KB limit, but **failed to update the earlier test cases and the test runner script**! If a developer executed the script in Section 3 against the working in-SRAM binary, the script failed and aborted compilation!

---

## 4. Key Takeaways for Our 128 KB SRAM Build

1. **Adopt `SW-UART-05` (MSP Frame Integrity):**
   - For our minimal handshake build, running a loop of 1,000 to 10,000 `MSP_STATUS` / `MSP_IDENT` requests will prove rock-solid USB-UART stability.
2. **Automated Vector Check Update:**
   - Any test runner script must strictly verify:
     - `_start == 0x00202000`
     - `trap_entry == 0x002020f0`
     - `_estack <= 0x0023FFFF`
     - `Binary size <= 131,072 bytes (128 KB)`
3. **Preserve Arming Lockouts & Failsafe Integrity:**
   - Even in minimal builds, Betaflight's arming prevention logic must report valid status over MSP (`ARMING_DISABLED_CLI`, etc.).

---
*Retrospective log compiled and verified against historical `oldfiles/logs/SOFTWARE_TEST_PLAN.md` records.*
