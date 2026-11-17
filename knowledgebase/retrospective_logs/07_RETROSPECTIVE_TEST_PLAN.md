# Retrospective Log 07: Master Test & Validation Plan

> **Source File:** [`oldfiles/logs/TEST_PLAN.md`](file:///home/ivan/Desktop/betaflight/oldfiles/logs/TEST_PLAN.md)  
> **Target Platform:** C-DAC THEJAS32 SoC (VEGA ET1031 RV32IM @ 100 MHz, 256 KB SRAM, No XIP)  
> **Analysis Date:** 2026-11-17  
> **Status:** Completed

---

## 1. Executive Summary & Purpose

`TEST_PLAN.md` is the high-level Master Test & Validation Plan for the Betaflight ARIES v3.0 port. While `SOFTWARE_TEST_PLAN.md` focuses on register assertions and GTest test suites, `TEST_PLAN.md` defines:
1. **Physical & Electrical Safety Protocols:** Bench safety rules, power conditioning requirements, and mechanical vibration isolation.
2. **Historical Roadblocks Watch-Items:** Failure modes and verification methods across trap vectors, clock trees, timebases, SPI, flash, and soft-float.
3. **5-Layer Software Verification Hierarchy:** System Core, Real-Time Scheduler, Peripherals, Safety Logic, and Control Loops.
4. **Physical Bench & Oscilloscope Verification Procedures:** Step-by-step procedures for power-on, 5-minute soak tests, IMU 3D model validation, flash persistence, motor PWM capture, and receiver failsafe cutoffs.
5. **Development Ergonomics & Best Practices:** Workflow tips including 1-click flash scripts, non-root USB access, SOS LED patterns, and logic analyzer harnesses.
6. **Software Feature Restriction Matrix:** Explicit list of heavy Betaflight features permanently disabled for the 100 MHz RV32IM core.
7. **Master Sign-Off Checklist (TST-01 to TST-14).**

---

## 2. Decision & Action Scoring Matrix (0 to 10)

| ID | Section / Topic | Decision / Action Description | Category | Score (0–10) | Justification & Technical Impact |
| :--- | :--- | :--- | :--- | :---: | :--- |
| **D01** | Safety (§2.1) | "The Propellers Off Rule": Mandatory bare motor shaft testing on bench | Physical Safety | **10.0 / 10** | **Critical Life Safety Rule.** Prevents catastrophic injuries or lab destruction caused by runaway motors during driver bring-up, unhandled traps, or failsafe bugs. |
| **D02** | Electrical (§2.2) | 5V Regulated BEC, Low-ESR Capacitor ($470\text{--}1000\,\mu\text{F}$ 35V), and 3.3V Logic Ceiling | Electrical Protection | **10.0 / 10** | **Essential Hardware Safeguard.** ARIES v3 dev board lacks onboard TVS diodes. ESC inductive back-EMF spikes (>30V on 4S) will blow the SoC without an external BEC, low-ESR capacitor, and current limiter. |
| **D03** | Mechanical (§2.3) | External IMU soft-mounting with 3M VHB foam tape & $< 10\text{ cm}$ SPI jumper wires | Mechanical / Signal Integrity | **9.5 / 10** | **High-Frequency Noise Isolation.** Dev board lacks built-in gyro dampeners; foam mounting isolates 200–600 Hz motor vibration, while short leads prevent SPI signal degradation. |
| **D04** | Feature Scoping (§7) | Permanently disabling Dynamic Notch, RPM Filtering, Cascaded Biquads, and GPS Rescue | Performance Scoping | **9.5 / 10** | **Ruthless, Realistic Scoping.** Acknowledged the physical limits of 100 MHz RV32IM soft-float: eliminated FFT algorithms (>15,000 cycles) and microsecond bit-banging, preserving single-precision PT1 filters. |
| **D05** | Bench Procedure (§5.2) | 5-Minute Timebase Soak Test crossing 42.95s boundary | Verification Procedure | **9.5 / 10** | **Empirical Rollover Proof.** Running a continuous 5-minute bench test proved that `mcycle`/`mcycleh` double-reading completely eliminated scheduler panic at ~43 seconds. |
| **D06** | Historical Table (§3) | Roadblock Watch-Items Table cataloging past root causes | Institutional Knowledge | **9.0 / 10** | **Valuable Troubleshooting Guide.** Clear summary of trap routing, baud divisors, Timer 0 EOI clears, SPI `Bits.CSAAT`, and double promotion warnings. |
| **D07** | Ergonomics (§6) | Development Best Practices (1-click flash, non-root USB, SOS LED, ribbon harnesses) | Developer Experience | **9.0 / 10** | **Friction Reduction.** Substantially sped up test cycles and debugging on physical hardware. |
| **D08** | Memory Desync (§4) | Layer 1 still referencing `0x1000FEF0` SP and `0x20000000` Flash | Documentation Maintenance | **2.5 / 10** | **Partial Update Oversight.** Updated the checklist at the end with TST-13/TST-14 (in-SRAM execution), but left `0x1000FEF0` and `0x20000000` in the Layer 1 text. |

---

## 3. Deep-Dive Critique: The Masterpieces vs. The Residual Baggage

### 3.1 The Masterpieces (Score: 10.0/10)
1. **The Safety & Electrical Protocols (§2)**:
   - This section is written with seasoned, battle-tested hardware wisdom.
   - Drone development boards are fragile: connecting raw LiPo power to a board without TVS diodes guarantees blown linear regulators and dead silicon from ESC regenerative braking spikes.
   - Demanding series current limiting ("Smoke Stopper"), low-ESR electrolytic capacitors, clean 5V BECs, and bare motor shafts saved the physical ARIES v3 board from electrical destruction.
2. **Software Feature Restriction Matrix (§7)**:
   - Too many embedded porting projects fail because developers try to enable every modern feature (dynamic notch FFT filters, bi-directional DShot, graphical OSD, GPS navigation).
   - This matrix clearly stated: **NO**. On a 100 MHz soft-float RISC-V core, dynamic notch FFT alone takes 15,000 cycles ($150\,\mu\text{s}$), which would consume almost half of the 1.0 kHz flight loop budget. Clamping down to PT1 low-pass filters and basic PWM/OneShot was the exact right engineering call.

---

### 3.2 The Residual Baggage (Score: 2.5/10)
- In the Master Sign-Off Checklist (lines 226–227):
  ```text
  | TST-13 | Zero RVC Instructions Audit | 0 compressed 16-bit opcodes in code | PASS |
  | TST-14 | In-SRAM Execution & Memory Safety | Size ≤ 248 KB, pure 32-bit memset | PASS |
  ```
- The author clearly validated the physical in-SRAM execution reality!
- Yet in Section 4 (Layer 1, lines 84–86):
  - *"Paint the 16 KB stack (`_sstack` to `_estack` at `0x1000FEF0`)..."*
  - *"Confirm `.data` relocates cleanly from Flash (`0x20000000`) to SRAM (`0x10000000`)..."*
- This is the fourth consecutive old log file where we observe the exact same issue: **patching new findings at the bottom or in callouts, while leaving obsolete, broken memory addresses intact in the body**.

---

## 4. Key Takeaways for the Minimal 128 KB SRAM Build

1. **Safety Rules Remain in Full Effect:**
   - Even when testing only serial MSP and Configurator handshakes, keep the dev board on bench power with current limiting.
2. **Feature Scoping Confirmed for 128 KB:**
   - If a 205 KB build had to disable dynamic notch and GPS, our **128 KB build** must disable even more:
     - Exclude Blackbox completely.
     - Exclude motor mixer and PWM output (stubs only for handshake).
     - Exclude receiver protocols (CRSF/ELRS/SBUS).
     - Exclude non-volatile flash storage drivers (`CONFIG_IN_RAM` only).
     - Retain only: `systemInit()`, UART0 serial driver, MSP parser, Parameter Group defaults in RAM, and scheduler skeleton.
3. **5-Minute Soak Test as Final Acceptance Gate:**
   - Once Betaflight Configurator connects, leave it streaming live status for 5 continuous minutes to guarantee zero serial lockups, zero timebase wraps, and zero memory leaks.

---
*Retrospective log compiled and verified against historical `oldfiles/logs/TEST_PLAN.md` records.*
