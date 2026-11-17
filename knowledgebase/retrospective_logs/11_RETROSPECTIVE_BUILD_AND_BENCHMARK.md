# Retrospective Log 11: Automated Build & Benchmark Script

> **Source File:** [`oldfiles/logs/buildAndBenchmark.sh`](file:///home/ivan/Desktop/betaflight/oldfiles/logs/buildAndBenchmark.sh)  
> **Target Platform:** C-DAC THEJAS32 SoC (VEGA ET1031 RV32IM @ 100 MHz, 256 KB SRAM, No XIP)  
> **Analysis Date:** 2026-11-17  
> **Status:** Completed

---

## 1. Executive Summary & Purpose

`buildAndBenchmark.sh` is an interactive and scriptable demonstration tool developed for the NIDAR VEGAPilot jury evaluation. It automates:
1. Parallel firmware builds (`make TARGET=ARIESV3 -j$(nproc) hex`).
2. Sizing benchmarks calculating ROM and RAM utilization.
3. Automated static assembly audits inspecting `subTaskPidController` for double-precision math.
4. An interactive ANSI menu system supporting live presentation demos as well as non-interactive CLI flags (`compile`, `benchmark`, `audit`, `all`).

---

## 2. Decision & Action Scoring Matrix (0 to 10)

| ID | Section / Feature | Decision / Action Description | Category | Score (0–10) | Justification & Technical Impact |
| :--- | :--- | :--- | :--- | :---: | :--- |
| **D01** | Tooling UX (§6) | Dual-mode Interface: Interactive TUI Menu + Non-Interactive CLI arguments | Developer Ergonomics | **9.5 / 10** | **Exceptional Usability.** Allows human developers to navigate an interactive presentation menu while enabling CI/CD pipelines and automated agent workflows to call non-interactive subcommands. |
| **D02** | Static Audit (§4) | Automated Disassembly Inspection of `subTaskPidController` via `nm` & `objdump` | Performance Verification | **9.5 / 10** | **Masterful Automated Audit.** Automatically resolves function entry and exit addresses using `nm`, disassembles the exact address window, and asserts zero double-precision soft-float calls. |
| **D03** | Parallel Make (§2) | Automatic core detection using `make -j"$(nproc)"` | Build Performance | **9.0 / 10** | **Fast Iteration.** Maximizes host CPU core utilization, reducing clean compilation times to under 15 seconds. |
| **D04** | Memory Baseline (§3) | Computing Flash utilization against 2,048 KB external Winbond Flash | Footprint Metrics | **2.0 / 10** | **Invalid Silicon Baseline.** Baseline `MAX_FLASH_BYTES` was set to $2048\,\text{KB}$. On physical THEJAS32 silicon, the bootloader can load at most 248 KB into SRAM. Showing 770 KB / 2048 KB (37.6%) presented an unbootable binary as a success. |
| **D05** | Fallback Rule (Gemini/Compilation Guides) | Documented manual command fallbacks if the shell script fails | Workflow Resilience | **9.0 / 10** | **Safe Engineering Discipline.** Ensured agents and evaluators never get blocked if bash environment variables or paths differ. |

---

## 3. Deep-Dive Critique: Presentation Polish vs. Baseline Reality

### 3.1 The Brilliant Presentation Polish (Score: 9.5/10)
`buildAndBenchmark.sh` is one of the cleanest presentation scripts in the repository:
- **Zero Double-Precision Proof:**
  ```bash
  PID_START=$("${NM_BIN}" "${ELF_FILE}" | grep " T subTaskPidController$" | awk '{print "0x"$1}')
  PID_END=$("${NM_BIN}" "${ELF_FILE}" | grep " T pidInit$" | awk '{print "0x"$1}')
  PID_DOUBLES=$("${OBJDUMP_BIN}" -d --start-address="${PID_START}" --stop-address="${PID_END}" "${ELF_FILE}" | \
      grep -E "<__adddf3>|<__subdf3>|<__muldf3>|<__divdf3>" | wc -l || true)
  ```
- This directly proved to technical evaluators that the flight control loop was 100% single precision, eliminating any skepticism regarding soft-float latency on the RV32IM core.

---

### 3.2 The Pre-Silicon Flash Mirage (Score: 2.0/10)
- In lines 64–74:
  ```bash
  MAX_FLASH_BYTES=$((2048 * 1024))
  FLASH_PERCENT=$(awk "BEGIN {printf \"%.1f\", (${TOTAL_FLASH}/${MAX_FLASH_BYTES})*100}")
  echo "Flash (ROM) Usage: ${TOTAL_FLASH} bytes (${FLASH_KB} KB) / 2,048 KB [${FLASH_PERCENT}% utilized]"
  ```
- If the binary grew to 770 KB, the script reported:
  `Flash Usage: 770.7 KB / 2,048 KB [37.6% utilized] - Free Flash Headroom: 1,277 KB`
- But on physical hardware, **the free headroom was negative 522 KB**! The bootloader copied only the first 250 KB and crashed.
- Once the in-SRAM execution reality was discovered, this script should have been updated with `MAX_IMAGE_BYTES = 248 * 1024` (253,952 bytes).

---

## 4. Key Takeaways for Our 128 KB SRAM Build

1. **Adopt the Script Pattern for Our Target:**
   - Clone this script structure into our rebuild workspace.
2. **Update the Metric Baselines:**
   - Change `MAX_TARGET_BYTES` to **131,072 bytes (128 KB)**.
   - Sizing check must assert that `.text + .rodata + .data + .bss + .stack <= 128 KB`.
3. **Retain the SubTask PID Disassembly Audit:**
   - Keep the `subTaskPidController` zero-double-precision audit check in the pipeline.

---
*Retrospective log compiled and verified against historical `oldfiles/logs/buildAndBenchmark.sh` records.*
