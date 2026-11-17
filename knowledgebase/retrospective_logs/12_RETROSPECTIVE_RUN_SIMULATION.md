# Retrospective Log 12: Renode Simulation Runner Script

> **Source File:** [`oldfiles/logs/runSimulation.sh`](file:///home/ivan/Desktop/betaflight/oldfiles/logs/runSimulation.sh)  
> **Target Platform:** C-DAC THEJAS32 SoC (VEGA ET1031 RV32IM @ 100 MHz, 256 KB SRAM, No XIP)  
> **Analysis Date:** 2026-11-17  
> **Status:** Completed

---

## 1. Executive Summary & Purpose

`runSimulation.sh` was authored as a one-click launcher for jury evaluations to demonstrate Betaflight booting in the Antmicro Renode RISC-V emulator. It provides:
1. Both headless console execution and graphical GUI simulation modes.
2. Parsing of CPU registers (`PC`, `SP`, `MTVEC`) after stepping 2,000 instructions from reset.
3. Formatted jury output declaring proof of execution on THEJAS32.

---

## 2. Decision & Action Scoring Matrix (0 to 10)

| ID | Section / Feature | Decision / Action Description | Category | Score (0–10) | Justification & Technical Impact |
| :--- | :--- | :--- | :--- | :---: | :--- |
| **D01** | Runner UX (§4) | Headless batch mode (`--plain --console`) and interactive GUI launcher | Tooling Ergonomics | **8.5 / 10** | **Clean Script Architecture.** Enabled automated CI runs while allowing human presenters to launch visual Renode windows for juries. |
| **D02** | Emulator Config (§3) | Running Renode with platform description `rv32im_zicsr_c` | Simulation Integrity | **1.0 / 10** | **Simulating the Wrong Silicon.** The physical ET1031 core lacks the 'C' extension. Enabling 'C' in the emulator allowed compressed instructions to execute without triggering the illegal instruction trap that physical silicon caught. |
| **D03** | Memory Model (§3) | Emulating 2 MB Flash at `0x20000000` and 256 KB SRAM at `0x10000000` | Architectural Model | **1.0 / 10** | **Emulating Non-Existent Hardware.** The script hardcoded checks asserting `PC = 0x20000000` and `SP = 0x10010b30` (peripheral MMIO). |
| **D04** | Output Fallbacks (§3) | Hardcoded Fallback Values when Renode Output Grep Fails (`[ -z "$PC_VAL" ] && PC_VAL="0x200000b0"`) | Integrity & Diagnostics | **0.0 / 10** | **Catastrophic Deception.** If Renode crashed, hung, or produced empty output, the bash script silently assigned hardcoded dummy register values (`0x200000b0`, `0x10010b30`, `0x200000f0`) and printed `✔ PROOF OF EXECUTION: PASSED`. The test was literally incapable of reporting a failure! |
| **D05** | Proof Claims (§1, §3) | Claiming `SP = 0x10010b30` proves "SRAM allocated correctly" to judges | Technical Accuracy | **0.5 / 10** | **Completely False Claim.** `0x10010b30` is in the peripheral register aperture, not SRAM. Presenting this to a jury as proof of SRAM allocation was factually fraudulent. |

---

## 3. Deep-Dive Post-Mortem: The Fallback Falsification Anti-Pattern

### 3.1 What Was Discovered in Lines 91–98?
Look at the actual bash code in lines 91–98:
```bash
PC_VAL=$(echo "${OUTPUT}" | grep -E "^0x2000[0-9a-fA-F]{4}$" | head -n 1)
[ -z "${PC_VAL}" ] && PC_VAL="0x200000b0"

SP_VAL=$(echo "${OUTPUT}" | grep -E "^0x10[0-9a-fA-F]{6}$" | head -n 1)
[ -z "${SP_VAL}" ] && SP_VAL="0x10010b30"

MTVEC_VAL=$(echo "${OUTPUT}" | grep -E "^0x200000f0$" | head -n 1)
[ -z "${MTVEC_VAL}" ] && MTVEC_VAL="0x200000f0"
```
And then in lines 104–112:
```bash
echo -e "  • Reset Vector Execution:   PASSED (Entry at 0x20000000, PC advanced to ${PC_VAL})"
echo -e "  • Stack Pointer Initialized: PASSED (SP = ${SP_VAL} inside internal SRAM)"
echo -e "  • Machine Trap Vector (mtvec): PASSED (Routed to trap_entry at ${MTVEC_VAL})"
echo -e "  • Synchronous Traps / Panics: 0 Traps (Zero illegal instruction or memory faults)"
echo -e "✔ PROOF OF EXECUTION: Betaflight boots cleanly on THEJAS32 RISC-V!"
```

### 3.2 Technical Critique (Score: 0.0/10)
- **Why is this so dangerous?**
  - If Renode encountered a fatal bus error and exited with empty output, `PC_VAL`, `SP_VAL`, and `MTVEC_VAL` were empty strings (`-z`).
  - The script caught the empty string and populated it with `0x200000b0` and `0x10010b30`!
  - It then printed `0 Traps` and `PASSED`!
- **Impact on the Previous Team:**
  - The team ran `./runSimulation.sh auto`.
  - It returned green checkmarks: `✔ PROOF OF EXECUTION: Betaflight boots cleanly on THEJAS32 RISC-V!`
  - The team assumed the bootloader, memory layout, stack pointer, and traps were completely proven.
  - In reality, the simulation was either not executing or executing a completely fictitious platform model.
- **Rule of Testing:**
  - **NEVER use default fallbacks to report success when a query fails.**
  - If a grep or register query returns empty, the test must throw an immediate error and fail with a non-zero exit code (`exit 1`).

---

## 4. Key Rules for Our 128 KB SRAM Build

1. **Rule 1: No Synthetic Fallbacks in Test Scripts:**
   - Test assertions must strictly evaluate real toolchain and hardware outputs. If output is missing or empty, fail immediately.
2. **Rule 2: Accurate Simulation Models Only:**
   - If Renode is used, the platform description must specify `rv32im_zicsr` (strictly no 'C' extension) and map SRAM at `0x00202000`.
3. **Rule 3: Ground Truth is Silicon:**
   - Real serial telemetry from UART0 over the CP2102N bridge is the only true proof of execution.

---
*Retrospective log compiled and verified against historical `oldfiles/logs/runSimulation.sh` records.*
