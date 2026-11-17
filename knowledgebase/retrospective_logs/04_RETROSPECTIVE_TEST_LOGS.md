# Retrospective Log 04: Test Logs & Hardware Reality Verification

> **Source File:** [`oldfiles/logs/TEST_LOGS.md`](file:///home/ivan/Desktop/betaflight/oldfiles/logs/TEST_LOGS.md)  
> **Target Platform:** C-DAC THEJAS32 SoC (VEGA ET1031 RV32IM @ 100 MHz, 256 KB SRAM, No XIP)  
> **Analysis Date:** 2026-11-17  
> **Status:** Completed

---

## 1. Executive Summary & Purpose

`TEST_LOGS.md` records the execution and progression of unit tests, integration tests, binary audits, and bench validations across the entire Betaflight ARIES v3.0 port:
1. **Phases 1 through 8 (Host GTest Suites):** 130 unit/integration tests running on host x86 with mocked MMIO and CSRs, plus 8 binary audit checks in `build_verify.sh` (138/138 checks PASSED).
2. **The 2026-11-14 Physical Silicon Collision:** First live bench flashing on actual ARIES v3.0 hardware, where the entire 138-test "100% pass" status collapsed due to physical hardware realities (no Flash XIP, `0x10000000` MMIO corruption, `.hex` flashing corruption, toolchain multilib RVC injection).
3. **Hardware Bench Remediation:** Documenting new tests (`INT-10`–`INT-12`, `T1-24`–`T1-28`, `T8-09`, `T8-10`), bare-metal memory primitives, early UART telemetry, and automated opcode audits.

---

## 2. Decision & Action Scoring Matrix (0 to 10)

| ID | Test / Event | Decision / Action Description | Category | Score (0–10) | Justification & Technical Impact |
| :--- | :--- | :--- | :--- | :---: | :--- |
| **D01** | Phase 1–6 Suites | Building 130 GTest Unit & Integration tests using `mock_mmio` & `mock_csr` | Test Architecture | **8.5 / 10** | **Comprehensive Logic Coverage.** Thoroughly verified baud calculations, circular ring buffer math, GPIO bit offsets, EXTI routing, and Timer 1 motor PWM deadline queue logic in isolated unit tests. |
| **D02** | Phase 7 `BUILD-04` | Asserting `_start == 0x20000000` in `build_verify.sh` | Validation Architecture | **1.0 / 10** | **Institutionalizing a Fatal Fallacy.** Built an automated test asserting that entry point is at `0x20000000` (XIP Flash), guaranteeing that the test passed only if the code was compiled with addresses that would crash physical silicon. |
| **D03** | Host Testing Strategy | Relying 100% on host x86 mocks without memory map validation | Test Strategy | **3.0 / 10** | **False Green Mirage.** Because all MMIO was mocked in normal RAM arrays on x86, the tests could not detect that `0x10000000` was peripheral MMIO instead of SRAM, or that external flash lacked an XIP bus interface. |
| **D04** | `BUILD-06` Disassembly Audit | Automated regex audit of disassembly for double-precision routines (`__adddf3`, etc.) | Performance Audit | **9.5 / 10** | **Exceptional Verification.** Automatically ensured that soft-float PID loops (`subTaskPidController`, etc.) had zero 64-bit double-precision operations, preventing scheduler overruns on RV32IM. |
| **D05** | `BUILD-07` Remediation | Pure 32-bit modular reciprocal division in `micros()` replacing `__udivdi3` | Algorithm Optimization | **10.0 / 10** | **Masterful Optimization.** Eliminated software 64-bit integer division call, converting `micros()` into a pure leaf function without stack allocation, reducing execution time from ~140 cycles to ~25 cycles (~4% CPU recovered). |
| **D06** | Hardware Bench Collision (2026-11-14) | Empirical testing on physical ARIES v3 silicon with C-DAC `flasher.bat` | Hardware Bring-up | **9.5 / 10** | **Crucial Ground Truth.** Revealed the bootloader's true behavior: copying 248 KB from flash offset 0 into SRAM `0x00202000` and executing `jalr 0x00202000`. |
| **D07** | Automated RVC Audit (`T8-09`) | Scripted objdump check guaranteeing 0 compressed 16-bit instructions | Toolchain Safety | **10.0 / 10** | **Brilliant Defensive Assertion.** Created automated test `T8-09` that greps disassembly for `c.` opcodes, ensuring the multilib toolchain bug never silently re-enters the binary. |
| **D08** | Binary Ceiling Test (`T8-10`) | Automated test asserting raw binary footprint $\le 253,952$ bytes (248 KB) | Memory Safety | **9.5 / 10** | **Strict Boundary Enforcement.** Hard failure if `.bin` exceeds bootloader DMA limit, preventing truncated binary flashing. |
| **D09** | Bare-Metal Memory Tests (`T1-24`–`T1-26`) | Unit tests for custom bare-metal `memset`, `memcpy`, `memmove` | Core Runtime | **9.0 / 10** | **Solid Core Verification.** Verified overlapping forward/backward `memmove` and zero-length boundaries, ensuring custom memory routines operate safely. |
| **D10** | Diagnostic Milestone Tags (`[INIT] 1..7`) | Step-by-step early UART telemetry before full system initialization | Diagnostics | **9.0 / 10** | **Effective Hardware Visibility.** Enabled precise isolation of boot freezes without requiring a JTAG debugger. |

---

## 3. Deep-Dive Analysis: The Testing Paradox

### 3.1 The "100% Passing Tests on a Non-Working Platform" Paradox (Score: 2.0/10)
`TEST_LOGS.md` presents a classic software engineering anti-pattern:
- The team wrote 138 tests.
- All 138 tests reported **PASS (100.0% Success Rate)**.
- Management and reviewers were shown a green dashboard.
- Yet on the physical ARIES v3.0 board, **the firmware did not run for even a single microsecond**!

#### Why Did This Happen?
1. **Mock Fidelity Gap:**
   - The unit tests ran on a 64-bit x86 host Linux machine using GCC.
   - `mock_mmio.c` allocated static RAM buffers (e.g. `uint8_t peripheral_space[0x100000]`).
   - When the code under test wrote to `0x1000FEF0` (the stack pointer), the x86 test wrote into a valid mocked RAM array. On physical THEJAS32 silicon, `0x1000FEF0` was an unmapped peripheral register that triggered hardware bus faults!
2. **Institutionalized Fallacies in Test Assertions:**
   - In `build_verify.sh`, check `BUILD-04` asserted:
     ```bash
     grep -E "([0-9a-f]{8}) T _start" | awk '{print $1}' == 20000000
     ```
   - Instead of checking if `_start` aligned with the hardware bootloader specification (`0x00202000`), the test asserted that `_start` was at `0x20000000`! The test would have failed if someone correctly fixed the address!

---

### 3.2 The Brilliant Turnaround (Score: 10.0/10)
When the team moved to physical hardware on 2026-11-14, their engineering response was exemplary:
1. **They did not hack around the hardware:**
   - They accepted the 248 KB SRAM limit and the no-XIP reality.
   - They disassembled `lib/vega-bsp/bootloaders/arduflasher/flasher_arduino.bin` to discover the exact `jalr 0x00202000` instruction.
2. **They automated regression guards for silicon realities:**
   - **`T8-09`:** Automated audit verifying exactly **0 compressed 16-bit instructions** in executable sections.
   - **`T8-10`:** Automated check verifying raw binary size $\le 248\,\text{KB}$ ($253,952$ bytes).
   - **`T1-27` & `T1-28`:** Tested bounded UART loops and pure ALU nibble hex conversion.
3. **Leaf Function Optimization for `micros()` (`BUILD-07`):**
   - Soft-float RV32IM cores cannot afford software 64-bit division. By rewriting `micros()` using pure 32-bit modular arithmetic and reciprocal multiplication, they reduced cycle count by 82% (from 140 to 25 cycles), eliminating stack frames and making it a true leaf function.

---

## 4. Key Lessons & Rules for the 128 KB SRAM Build

1. **Rule 1: Never Trust Mocks for Memory Maps:**
   - Mocks validate algorithm correctness and state transitions; they DO NOT validate hardware memory architectures. Always cross-reference against vendor linker scripts (`link1.lds`).
2. **Rule 2: Automated Opcode & Sizing Audits are Mandatory:**
   - Retain `T8-09` (0 compressed instructions) and `BUILD-06` (0 double precision in flight paths) in our build pipeline.
   - For our target build, update `T8-10` to assert binary size $\le 131,072$ bytes (**128 KB**).
3. **Rule 3: Keep `micros()` as a Zero-Stack Leaf Function:**
   - The modular reciprocal math implementation from `BUILD-07` must be preserved in `system.c`.
4. **Rule 4: Diagnostic Milestones Must Be Standard:**
   - Early boot banners and non-blocking UART output (`thejas32_early_uart_puts`) are critical for quick fault isolation.

---
*Retrospective log compiled and verified against historical `oldfiles/logs/TEST_LOGS.md` records.*
