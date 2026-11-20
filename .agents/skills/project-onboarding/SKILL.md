---
name: project-onboarding
description: >-
  Bootstrap and orient an agent working on the Betaflight port for C-DAC ARIES v3.0 (THEJAS32 / VEGA ET1031).
  Automatically activate at the start of every session, on new task kickoff, when checking toolchain setup,
  verifying active git branch ('rebuild' vs 'master'), or reviewing the 8 inviolable silicon ground truths.
---

# Project Onboarding Skill: ARIES v3.0 (THEJAS32 / VEGA ET1031)

This skill provides the mandatory onboarding protocol and orientation runbook for any agent working on the Betaflight port to the C-DAC ARIES v3.0 board.

---

## 1. Mission & Strategic Context

* **Competition:** **NIDAR (National Innovation Challenge for Drone Application and Research)**
* **Track:** **Mission 3: VEGAPilot Track**
* **Initiative:** **Digital India RISC-V (DIR-V)** program under MeitY (Government of India).
* **Goal:** Replace foreign STM32 flight controllers with an indigenous Indian RISC-V flight controller based on C-DAC's THEJAS32 SoC (VEGA ET1031 core @ 100 MHz).
* **Primary Reference Document:** [`AGENT.md`](../../../AGENT.md)

---

## 2. Git Branch Verification (Critical Rule)

The repository has two distinct branches:
* **`master`:** Historical failed codebase containing pre-silicon simulation fallacies (assumed 2 MB external XIP flash, 770 KB bloat, empty `BASEPRI` stubs, RVC instruction traps masked by Renode). **DO NOT TOUCH.**
* **`rebuild`:** Active, verified working codebase designed for physical silicon reality ($< 248\text{ KB}$ SRAM execution, `0x00202000` base, 0 RVC instructions, `#define CONFIG_IN_RAM`, Timer 1 non-blocking PWM).

### Onboarding Step 1: Verify Active Branch
Run:
```bash
git branch --show-current
```
* If the output is not `rebuild`, immediately switch:
  ```bash
  git checkout rebuild
  ```

---

## 3. The 8 Inviolable Silicon Ground Truths

Every agent must memorize and respect these physical hardware constraints:

1. **RV32IM Only (Strictly 0 RVC Compressed Instructions):**
   - The VEGA ET1031 core does **NOT** support the Compressed ('C') instruction extension.
   - Any 16-bit compressed instruction triggers an **Illegal Instruction Fault (`mcause = 2`)**.
   - Build flags must specify `-march=rv32im_zicsr -mabi=ilp32`.
   - Toolchain multilib must strictly point to `rv32i_zicsr_zifencei/ilp32`.
2. **Boot & Execute from Internal SRAM (No XIP):**
   - The external SPI flash is **not memory-mapped** for instruction execution.
   - Execution begins at `0x00202000` in internal SRAM.
   - The total binary footprint (`.text` + `.rodata` + `.data` + `.bss` + stack) **must not exceed 248 KB** (`0x0023FFFF`).
3. **Parameter Groups Stored in RAM (`CONFIG_IN_RAM`):**
   - Parameter Groups are stored in internal SRAM via `#define CONFIG_IN_RAM`.
   - `EEPROM_SIZE` must be **`8192` (8 KB)**. Never lower it to 4096 (triggers boot panic `FAILURE_CONFIG_STORE_FAILURE`).
4. **Soft-Float Architecture & 1.0 kHz Loop Clamping:**
   - The core has no hardware FPU. Single-precision floating point math is emulated via software routines.
   - The flight loop is clamped to **1.0 kHz** (`DEFAULT_PID_PROCESS_DENOM 8`), consuming $\approx 290\,\mu\text{s}$ WCET and leaving $> 65\%$ idle CPU headroom at 100 MHz.
5. **Atomic Operations via CSR `mstatus.MIE`:**
   - THEJAS32 lacks ARM NVIC `BASEPRI` registers and RISC-V Atomic ('A') instructions (`lr.w`/`sc.w`).
   - Atomic blocks must disable and restore `mstatus.MIE` (bit 3) via CSR instructions (`csrrci` / `csrs`).
6. **Non-Blocking Timer 1 PWM Motor Scheduler:**
   - Overcomes lack of multi-channel PWM timer hardware by using Hardware Timer 1 (IRQ 8) ascending deadline sequencing on pins `PA1`–`PA4`, consuming $< 3\,\mu\text{s}$ CPU overhead.
7. **Polled SPI0 Transfer Engine:**
   - SPI0 (Header J1) operates in polled mode without DMA.
   - SCK frequency must be clamped to **12.5 MHz** (Divisor 1). Divisor 0 (25.0 MHz) violates the ICM-42688P IMU 24.0 MHz maximum rating.
8. **16550 UART0 Communication:**
   - Mapped to pins `PA30` (TX) and `PA31` (RX), routed to the onboard CP2102N USB-UART bridge.
   - RX interrupt ring buffer; synchronous bounded TX drain.

---

## 4. Step-by-Step Agent Onboarding Runbook

When joining a session or starting a new task, follow this exact procedure:

1. **Verify Git Branch:** Ensure `git branch --show-current` reports `rebuild`.
2. **Review Knowledge Base Index & Gaps:** Open [`knowledgebase/INDEX.md`](../../../knowledgebase/INDEX.md), [`PORTING_KNOWLEDGE_GRAPH.md`](../../../knowledgebase/PORTING_KNOWLEDGE_GRAPH.md), and check [`CRITICAL_KNOWLEDGE_GAPS_AND_ASSUMPTIONS.md`](../../../knowledgebase/CRITICAL_KNOWLEDGE_GAPS_AND_ASSUMPTIONS.md) to avoid making assumptions on pending hardware confirmations.
3. **Check Target Drivers:** Inspect files in `src/platform/ARIES/` and corresponding evaluation notes in [`knowledgebase/codebase_evaluations/`](../../../knowledgebase/codebase_evaluations).
4. **Consult Before Modifying:** If tasked with altering any peripheral, driver, or build setting, use the `knowledge-search` skill first.
5. **Verify Electrical Safety:** If physical bench testing is planned:
   - Propellers **MUST BE OFF**.
   - External 5V BEC must power the board/peripherals.
   - Low-ESR capacitor ($470\text{--}1000\,\mu\text{F}$) must be installed to protect 3.3V LVCMOS pins from ESC voltage spikes.
6. **Strict Relative File Path Policy:**
   - Ensure all documentation, phase logs (`logs/dev/`), code comments, and cross-references strictly use POSIX relative file paths (`../...` or repo-relative).
   - Hardcoded host filesystem paths (such as `/home/ivan/...` or `file:///home/...`) are strictly forbidden across all commits.
7. **VM Environment & Manual Host Flashing:**
   - The development environment runs inside a Virtual Machine (VM) without direct USB device or hardware debug passthrough.
   - The agent **MUST NEVER** attempt direct device flashing or OpenOCD commands.
   - After compiling each time, copy the generated `.bin` to `tempfiles/` (e.g. `cp obj/main/betaflight_bootstrap.bin tempfiles/`) so the user can easily flash it manually from the host machine.
8. **Every-Phase Build Briefing & Verification Feedback Loop:**
   - At every phase, provide compiled binary details (path, exact size, origin address, 0 RVC count), specify what to look for in the build/serial output, and wait for the user to flash and provide serial logs.
   - Heavily instrument diagnostic firmware with detailed ASCII UART logs and status LED0 toggling.
