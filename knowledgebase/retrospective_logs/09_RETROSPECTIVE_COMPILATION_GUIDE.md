# Retrospective Log 09: Compilation & Flashing Guide

> **Source File:** [`oldfiles/logs/COMPILATION_GUIDE.md`](../../oldfiles/logs/COMPILATION_GUIDE.md)  
> **Target Platform:** C-DAC THEJAS32 SoC (VEGA ET1031 RV32IM @ 100 MHz, 256 KB SRAM, No XIP)  
> **Analysis Date:** 2026-11-17  
> **Status:** Completed

---

## 1. Executive Summary & Purpose

`COMPILATION_GUIDE.md` was authored on 2026-10-26 as an onboarding and operational flashing guide for developers and evaluators working with the ARIES v3.0 board. It covers:
1. Compilation artifact paths (`.elf`, `.hex`, `.bin`, `.map`).
2. RISC-V toolchain prerequisites (`riscv32-esp-elf-gcc` v13.2.0).
3. Build commands, cleanup rules, and sizing checks.
4. Flashing methods: Method A (OpenOCD JTAG) and Method B (VEGA Flasher Python CLI).
5. Post-flash serial verification via `picocom`.
6. Troubleshooting FAQ (GCC `_zicsr` flag requirements, `__atomic_fetch_add_4` software stubs, lack of DFU).
7. Automated build and benchmarking script integration (`buildAndBenchmark.sh`).

---

## 2. Decision & Action Scoring Matrix (0 to 10)

| ID | Section / Topic | Decision / Action Description | Category | Score (0–10) | Justification & Technical Impact |
| :--- | :--- | :--- | :--- | :---: | :--- |
| **D01** | Toolchain Architecture (§2) | Standardizing on `riscv32-esp-elf-gcc` v13.2.0 (`rv32im` / `ilp32`) | Toolchain Strategy | **8.5 / 10** | **Solid Toolchain Baseline.** Chose a well-supported modern GCC 13.2 toolchain capable of generating bare-metal RISC-V binaries. |
| **D02** | GCC 12/13 CSR Flags (§6) | Enforcing `-march=rv32im_zicsr` instead of bare `-march=rv32im` | Compiler Configuration | **9.0 / 10** | **Accurate GCC Standard Compliance.** In GCC 12+, RISC-V CSR instructions (`csrr`, `csrw`) were split out of the base 'I' ISA into the `_zicsr` extension. Without this flag, builds fail immediately on assembly CSR reads. |
| **D03** | Atomics Handling (§6) | Explaining software stubs for missing hardware 'A' atomic extensions | Core Emulation | **8.5 / 10** | **Pragmatic RV32IM Solution.** Clarified that missing `__atomic_fetch_add_4` instructions are wrapped in interrupt-safe software primitives in `stub_drivers.c`. |
| **D04** | DFU Clarification (§6) | Explicitly clarifying that direct USB DFU flashing is impossible | Hardware Boundary | **9.0 / 10** | **Clear Hardware Reality.** Prevented wasted efforts trying to enter DFU mode through Configurator by explaining that CP2102N is a UART bridge and the SoC has no USB DFU ROM. |
| **D05** | Memory Map & Sizing (§1, §3, §4, §5) | Instructing flashing of 708 KB binary at offset `0x20000000` | Flashing & Memory Reality | **1.0 / 10** | **Complete Hardware Fiction.** Instructed users to flash a 708 KB binary to `0x20000000`. On physical silicon, THEJAS32 cannot boot from `0x20000000` (no XIP), and the bootloader truncates any flash payload to 248 KB, leading to immediate CPU halts. |
| **D06** | OpenOCD Directives (§4) | Recommending `flash write_image erase ... 0x20000000` | Tooling Configuration | **2.0 / 10** | **Non-Functional Flashing Command.** OpenOCD commands writing to `0x20000000` write to an unmapped SPI controller address. Physical flashing must use the C-DAC bootloader over UART or load SRAM directly via JTAG. |
| **D07** | Script Automation (§7) | Providing `buildAndBenchmark.sh` with interactive and CLI flags | Developer Experience | **8.5 / 10** | **Standardized CI Workflow.** Gave developers a 1-command build, footprint check, and soft-float audit tool with explicit manual fallback commands. |

---

## 3. Deep-Dive Critique: Pragmatic Tooling vs. The Legacy Flash Fallacy

### 3.1 The Strong Tooling & Compiler Advice
1. **The `_zicsr` Insight (§6)**:
   - When developers migrate from GCC 8/9 to modern GCC 12/13, RISC-V builds frequently fail with:
     ```text
     Error: unrecognized opcode `csrr'
     ```
   - The guide clearly explains that RISC-V modularized its ISA specifications, requiring `-march=rv32im_zicsr`.
2. **Clear Distinction on USB Capabilities (§6)**:
   - Developers coming from STM32 expect to hold a "BOOT0" button, plug in USB, and see "DFU" in Betaflight Configurator.
   - The guide correctly shuts this down: the board has a CP2102N USB-UART bridge; all firmware flashing is external (JTAG or serial bootloader).

---

### 3.2 The Pre-Silicon Legacy Baggage
- Like `PORT_GUIDE.md`, this guide was written on 2026-10-26, before physical hardware bench testing occurred on 2026-11-14.
- As a result:
  - It tells users the binary is 708 KB and fits comfortably in 2 MB Flash.
  - It tells users to flash `.hex` or `.bin` at `0x20000000`.
  - It assumes the chip executes in place from external flash.
- Anyone following Section 4 on physical hardware would fail to flash or boot the board.

---

## 4. Key Takeaways for Our 128 KB SRAM Build

1. **Compilation Flags Confirmed:**
   - Always use `-march=rv32im_zicsr -mabi=ilp32`.
   - Always link against the `rv32i_zicsr_zifencei/ilp32` toolchain multilib.
2. **Correct Flashing Procedure for ARIES v3.0:**
   - Jumper J12 (`BOOT-SEL`) shorted.
   - Flash strictly **raw binary (`.bin`)** via C-DAC `flasher.bat` over UART0 at 115200 baud.
   - Target size: **$\le 128\,\text{KB}$** (well below the 248 KB SRAM limit).
3. **Execution Verification:**
   - Serial monitor connects at 115200 baud (`DLL=54`, `DLH=0`).
   - `#` prompt activates Betaflight CLI.

---
*Retrospective log compiled and verified against historical `oldfiles/logs/COMPILATION_GUIDE.md` records.*
