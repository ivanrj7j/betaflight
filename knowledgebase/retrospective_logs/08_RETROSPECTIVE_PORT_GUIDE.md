# Retrospective Log 08: Initial Porting Guide & Feasibility Analysis

> **Source File:** [`oldfiles/logs/PORT_GUIDE.md`](../../oldfiles/logs/PORT_GUIDE.md)  
> **Target Platform:** C-DAC THEJAS32 SoC (VEGA ET1031 RV32IM @ 100 MHz, 256 KB SRAM, No XIP)  
> **Analysis Date:** 2026-11-17  
> **Status:** Completed

---

## 1. Executive Summary & Historical Context

`PORT_GUIDE.md` was authored at the inception of the port (2026-10-19) as the foundational architectural blueprint and roadmap. It evaluated the hardware feasibility of running Betaflight on the ARIES v3.0 board, defined the hardware pinout mappings, mapped out the 8 implementation phases, and proposed initial soft-float flight controller tuning.

It provides a fascinating historical window into what was understood on Day 1 versus the silicon realities uncovered weeks later.

---

## 2. Decision & Action Scoring Matrix (0 to 10)

| ID | Section / Topic | Decision / Action Description | Category | Score (0–10) | Justification & Technical Impact |
| :--- | :--- | :--- | :--- | :---: | :--- |
| **D01** | USB Architecture (§1) | Leveraging onboard CP2102N USB-UART bridge on UART0 instead of USB CDC stack | Architecture Strategy | **10.0 / 10** | **Masterstroke Simplification.** Eliminating the entire USB device stack, endpoint descriptors, and interrupts saved 25–35 KB of code and hundreds of hours of debugging. Standard 16550 UART connects directly to Configurator. |
| **D02** | Feasibility Loop Rate (§1) | Targeting 500 Hz–1.0 kHz PID loop instead of standard 4 kHz/8 kHz | Performance Scoping | **9.5 / 10** | **Accurate Day-1 Realism.** Recognized immediately that a 100 MHz RV32IM core without hardware floating point could never run Betaflight's default 8 kHz loop rate. Clamping to 1.0 kHz gave the needed cycle headroom. |
| **D03** | Memory Model Assumption (§1, §4) | Assuming 2 MB External Flash XIP at `0x20000000` & SRAM at `0x10000000` | Architectural Planning | **1.0 / 10** | **The Original Sin of the Port.** Established the fatal misconception that external SPI flash was memory-mapped XIP and that `0x10000000` was SRAM. This single assumption contaminated every linker script and test for the next month. |
| **D04** | Pinout Mapping (§2) | Mapping Header J1 (SPI0, IMU CS/DRDY) and Header J2 (UART1 RX, PWM motors) | Hardware Design | **9.0 / 10** | **Clean Physical Interface.** Grouped high-speed SPI on Header J1 and motor signals / receiver on Header J2, matching physical board layout. |
| **D05** | Naive `micros()` Spec (§4.3) | Proposing `micros() { return (timeUs_t)TIMER0_COUNT_REG; }` | Timebase Design | **2.0 / 10** | **Ignorance of Peripheral IP.** Synopsys DW_apb_timers Timer 0 is a 32-bit *down-counter* that counts down to zero, triggers an interrupt, and reloads. Naively returning the register would produce a backward, discontinuous clock. |
| **D06** | Actuator Protocol (§1, §5) | Standard Analog PWM (400 Hz) / OneShot125 over Timer PWM | Motor Strategy | **8.5 / 10** | **Pragmatic Compromise.** Acknowledged that THEJAS32 lacks DShot bit-banging DMA timers; selected standard PWM and OneShot125 as reliable non-blocking alternatives. |
| **D07** | CLI Dynamic Notch (§5) | Leaving `set dyn_notch_count = 1` in CLI recommendations | Flight Loop Tuning | **3.0 / 10** | **Underestimating FFT Load.** Even a single dynamic notch filter computes real-time FFTs consuming >15,000 cycles, which causes task starvation on 100 MHz RV32IM. |
| **D08** | I2C Battery ADC (§1, §4.7) | Background task reading onboard I2C ADC chip (U9) for battery voltage | Power Monitoring | **8.5 / 10** | **Clever Peripheral Integration.** ARIES v3 board routes 4 ADC channels to an external I2C ADC chip (`U9`). Reading it via I2C2 avoids MCU analog pin contention. |

---

## 3. Deep-Dive Critique: Visionary Insight vs. Day-1 Blindness

### 3.1 The Visionary Insight: The CP2102N Advantage (Score: 10.0/10)
- In STM32 flight controllers, implementing USB VCP requires thousands of lines of USB core code, endpoint ring buffers, USB suspension/resume states, and complex interrupt priority schemes. USB packet drops often brick Configurator connections.
- The author realized on Day 1: **We do not need a USB stack.** The ARIES v3 board routes USB-C through a hardware Silicon Labs CP2102N USB-to-UART transceiver directly to UART0.
- All Betaflight needs to talk to the Betaflight Configurator is a standard 16550 UART driver running at 115200 baud! This single insight made the entire port feasible.

---

### 3.2 The Day-1 Blindness: The Memory Map Fallacy (Score: 1.0/10)
- Section 4 (Phase 1) stated:
  > *"Linker Script (`aries_v3.ld`): Define Flash base (`0x20000000` or per memory map, 2 MB) and internal SRAM (`0x10000000`, 256 KB)."*
- Because this was written before reading the C-DAC BSP linker script (`link1.lds`), it made two catastrophic errors:
  1. It assumed Flash was mapped at `0x20000000` with hardware XIP (Execute-In-Place).
  2. It assumed `0x10000000` was SRAM.
- On THEJAS32:
  - SRAM is at `0x00200000`–`0x00240000`.
  - `0x10000000` is peripheral MMIO register space.
  - Flash is connected via SPI3 and has **zero XIP capability**.
- This error persisted across 7 phases of development before hardware bench testing exposed it.

---

## 4. Key Takeaways for Our 128 KB SRAM Build

1. **Reaffirm CP2102N UART0 Primacy:**
   - For our minimal handshake target, UART0 at 115200 baud via the CP2102N is our sole communication lifeline.
2. **Discard All Early Memory Layouts:**
   - Linker scripts must exclusively use `RAM (wxa!ri) : ORIGIN = 0x00202000, LENGTH = 128K`.
3. **Preserve OneShot125 / Analog PWM Focus:**
   - Do not attempt digital DShot or bi-directional DShot; stick to basic soft PWM or stubs for handshake.

---
*Retrospective log compiled and verified against historical `oldfiles/logs/PORT_GUIDE.md` records.*
