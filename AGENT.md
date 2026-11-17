# AGENT.md: Operational Directives & Onboarding Guide

> **MANDATORY NOTICE FOR ALL AI AGENTS AND DEVELOPERS:**  
> Read this entire document before inspecting, modifying, building, or discussing the codebase. Every decision, hardware assumption, and architectural choice in this repository is governed by physical silicon realities that diverge sharply from standard flight controller targets.

---

## 1. Project Mission & Strategic Context

* **Competition:** **NIDAR (National Innovation Challenge for Drone Application and Research)**
* **Track:** **Mission 3: VEGAPilot Track**
* **National Initiative:** Aligned with the **Digital India RISC-V (DIR-V)** program overseen by the Ministry of Electronics and Information Technology (MeitY), Government of India.
* **Core Objective:** To port **Betaflight**—the industry-standard open-source quadcopter flight controller firmware—onto the **ARIES v3.0** embedded platform powered by the **C-DAC THEJAS32 SoC (VEGA ET1031 RISC-V RV32IM core @ 100 MHz)**.
* **Strategic Value:** Replace foreign, proprietary, and STM32-dominated flight controllers in Indian UAVs with a sovereign, production-grade flight stack running on Indian-designed silicon, fully compatible with the cross-platform **Betaflight Configurator GUI**.

---

## 2. Git Branch Architecture: Failed vs. Rebuild

The repository maintains two fundamentally different codebase tracks:

| Branch | Status | Architectural Reality |
| :--- | :--- | :--- |
| **`master`** | **FAILED CODEBASE (Historical Reference Only)** | Built on unverified simulation assumptions: assumed 2 MB external memory-mapped SPI flash (XIP) at `0x20000000`, imaginary SRAM at `0x10000000`, empty `BASEPRI` atomic stubs, unmasked 16550 THRE interrupt storms, Renode simulation masking 16-bit compressed (RVC) opcode traps, and an un-runnable 770 KB binary footprint. **Do NOT write new code here.** |
| **`rebuild`** | **ACTIVE WORKING CODEBASE** | The current development branch. Built strictly around physical THEJAS32 silicon constraints: in-SRAM execution starting at `0x00202000` (strict 248 KB budget), pure 32-bit RV32IM multilib isolation (0 RVC instructions), `#define CONFIG_IN_RAM` (8 KB EEPROM emulation), non-blocking Timer 1 ascending deadline PWM scheduler, locked 1.0 kHz flight loop, and tested/running on physical ARIES v3 hardware in hand. |

> [!CAUTION]
> **Active Branch Rule:** Always ensure your working branch is **`rebuild`**. Never commit feature code to `master`.

---

## 3. Mandatory Agent Skills (To Load on Project Entry)

Every AI agent assigned to this repository must load and utilize the following three core skills (located in the project skill registry):

1. **`project-onboarding`**:
   - Initializes the agent's context with hardware topology, peripheral base addresses, toolchain paths, and the 10 Golden Rules of THEJAS32 porting.
2. **`knowledge-search`**:
   - **MANDATORY FOR CODE ACCURACY:** Must be invoked **EVERY SINGLE TIME** before proposing, reviewing, planning, or writing any code, driver, register configuration, or build setting.
   - Queries the 50+ node knowledge base to find verified register maps, comparative logs (STM32 vs ESP32 vs THEJAS32), historical failure post-mortems, and evaluation scores before writing any code. Guessing or hallucinating hardware registers without searching is strictly prohibited.
3. **`knowledge-update`**:
   - Enforces documentation discipline: logs findings, evaluates technical decisions (0–10 score), creates retrospective nodes, and updates both [`knowledgebase/INDEX.md`](file:///home/ivan/Desktop/betaflight/knowledgebase/INDEX.md) and [`knowledgebase/PORTING_KNOWLEDGE_GRAPH.md`](file:///home/ivan/Desktop/betaflight/knowledgebase/PORTING_KNOWLEDGE_GRAPH.md).

---

## 4. The Knowledge Base First Rule

> [!IMPORTANT]
> **MANDATORY POLICY:** Before proposing, planning, or writing ANY C code, assembly, linker scripts, or Makefile changes, the agent **MUST CONSULT THE KNOWLEDGE BASE FIRST**. Guesswork and assumptions have repeatedly caused hardware failure on this project.

### Core Knowledge Base Anchors
* **Central Directory Index:** [`knowledgebase/INDEX.md`](file:///home/ivan/Desktop/betaflight/knowledgebase/INDEX.md) — The comprehensive master catalog linking all specs, inspection logs, retrospective analyses, and component nodes.
* **Master Architectural Blueprint:** [`knowledgebase/PORTING_KNOWLEDGE_GRAPH.md`](file:///home/ivan/Desktop/betaflight/knowledgebase/PORTING_KNOWLEDGE_GRAPH.md) — Component-by-component hardware abstraction blueprint and end-to-end Mermaid system graph.
* **Codebase Evaluations (01–15):** [`knowledgebase/codebase_evaluations/`](file:///home/ivan/Desktop/betaflight/knowledgebase/codebase_evaluations/) — Line-by-line technical audits and 0–10 decision scoring of the ported files in `src/platform/ARIES/`:
  - [`01_EVAL_STARTUP_S.md`](file:///home/ivan/Desktop/betaflight/knowledgebase/codebase_evaluations/01_EVAL_STARTUP_S.md): Reset handler, trap frame `x2` (`sp`) preservation, vector alignment.
  - [`02_EVAL_LINKER_ARIES_V3_LD.md`](file:///home/ivan/Desktop/betaflight/knowledgebase/codebase_evaluations/02_EVAL_LINKER_ARIES_V3_LD.md): SRAM layout (`0x00202000`), 248 KB ceiling, `.pg_registry`.
  - [`03_EVAL_BSP_THEJAS32_H.md`](file:///home/ivan/Desktop/betaflight/knowledgebase/codebase_evaluations/03_EVAL_BSP_THEJAS32_H.md): APB peripheral addresses, 100 MHz crystal definition.
  - [`04_EVAL_PLATFORM_H.md`](file:///home/ivan/Desktop/betaflight/knowledgebase/codebase_evaluations/04_EVAL_PLATFORM_H.md): `BASEPRI` bug analysis and `mstatus.MIE` CSR atomic sections.
  - [`05_EVAL_SYSTEM_C.md`](file:///home/ivan/Desktop/betaflight/knowledgebase/codebase_evaluations/05_EVAL_SYSTEM_C.md): 64-bit `mcycle` timebase, pure 32-bit `memset`/`memcpy` shims, soft reboot jump.
  - [`06_EVAL_SERIAL_UART_ARIES_C.md`](file:///home/ivan/Desktop/betaflight/knowledgebase/codebase_evaluations/06_EVAL_SERIAL_UART_ARIES_C.md): 16550 UART0, bounded TX polling, RX interrupt ring buffer.
  - [`07_EVAL_IO_ARIES_C_AND_DEF.md`](file:///home/ivan/Desktop/betaflight/knowledgebase/codebase_evaluations/07_EVAL_IO_ARIES_C_AND_DEF.md): Flat 32-bit pin tag indexing, atomic bit set/clear.
  - [`08_EVAL_EXTI_ARIES_C.md`](file:///home/ivan/Desktop/betaflight/knowledgebase/codebase_evaluations/08_EVAL_EXTI_ARIES_C.md): GPIO Bank 0/1 IRQ 10/11 demuxing, `__builtin_ctz` optimization.
  - [`09_EVAL_BUS_SPI_ARIES_C.md`](file:///home/ivan/Desktop/betaflight/knowledgebase/codebase_evaluations/09_EVAL_BUS_SPI_ARIES_C.md): SPI0 Header J1, Mode 0/3, 24 MHz clock clamping for ICM-42688P.
  - [`10_EVAL_CONFIG_FLASH_ARIES_C.md`](file:///home/ivan/Desktop/betaflight/knowledgebase/codebase_evaluations/10_EVAL_CONFIG_FLASH_ARIES_C.md): Memory-mapped flash pointer trap (`mcause = 7`) and `CONFIG_IN_RAM` solution.
  - [`11_EVAL_PWM_MOTOR_ARIES_C.md`](file:///home/ivan/Desktop/betaflight/knowledgebase/codebase_evaluations/11_EVAL_PWM_MOTOR_ARIES_C.md): Hardware Timer 1 ascending deadline soft PWM scheduler.
  - [`12_EVAL_STUB_DRIVERS_C.md`](file:///home/ivan/Desktop/betaflight/knowledgebase/codebase_evaluations/12_EVAL_STUB_DRIVERS_C.md): RV32IM `__atomic_fetch_add_4`, ADC 0V battery lockout guard.
  - [`13_EVAL_MAKEFILES_ARIES_MK_TARGET_MK.md`](file:///home/ivan/Desktop/betaflight/knowledgebase/codebase_evaluations/13_EVAL_MAKEFILES_ARIES_MK_TARGET_MK.md): `TOOLCHAIN_RV32I_LIB` multilib isolation, GCC 13+ `-march=rv32im_zicsr`.
  - [`14_EVAL_TARGET_H_AND_CONFIG.md`](file:///home/ivan/Desktop/betaflight/knowledgebase/codebase_evaluations/14_EVAL_TARGET_H_AND_CONFIG.md): 1.0 kHz loop clamping (`DEFAULT_PID_PROCESS_DENOM 8`), `EEPROM_SIZE 8192`.
  - [`15_EVAL_CORE_DIFFS_MAIN_CONFIG_PG_MSP.md`](file:///home/ivan/Desktop/betaflight/knowledgebase/codebase_evaluations/15_EVAL_CORE_DIFFS_MAIN_CONFIG_PG_MSP.md): Instant CLI activation on Enter (`\r`), core repository isolation boundaries.
* **Historical Retrospective Post-Mortems (01–14):** [`knowledgebase/retrospective_logs/`](file:///home/ivan/Desktop/betaflight/knowledgebase/retrospective_logs/) — Comprehensive evaluations of the failure cascades from the previous attempt.
* **Hardware Inspection Logs (01–25):** [`logs/hardware_inspection/`](file:///home/ivan/Desktop/betaflight/logs/hardware_inspection/) — Deep architectural dissections of STM32 and ESP32 reference implementations.
* **Component Implementation Nodes:** [`knowledgebase/components/`](file:///home/ivan/Desktop/betaflight/knowledgebase/components/) — Modules 01 through 11 defining the modular porting architecture.

---

## 5. Inviolable Silicon Ground Truths (THEJAS32 / VEGA ET1031)

Any code change that violates these 8 hardware laws will cause immediate board failure or fatal CPU traps:

1. **RV32IM Only — Strictly Zero Compressed Instructions:**
   - The VEGA ET1031 core implements standard integer, multiply, and divide extensions (`RV32IM`). It does **NOT** support the Compressed (`C`) instruction extension.
   - Any 16-bit instruction emitted by toolchain runtime libraries (`libc.a`, `libgcc.a`) will trigger an **Illegal Instruction Trap (`mcause = 2`)**.
   - Always link with `rv32i_zicsr_zifencei/ilp32` multilib libraries.
2. **No Hardware XIP — Boot & Execute from SRAM:**
   - The external SPI flash is **not memory-mapped** for instruction execution or coherent pointer dereferencing.
   - Firmware is transferred by the bootloader into internal SRAM at `0x00202000`.
   - The total binary footprint (`.text` + `.rodata` + `.data` + `.bss` + stack) **must not exceed 248 KB** (`0x0023FFFF`).
3. **Parameter Groups Stored in RAM (`CONFIG_IN_RAM`):**
   - Non-volatile configuration is stored in internal SRAM via `#define CONFIG_IN_RAM`.
   - `EEPROM_SIZE` must strictly be set to **`8192` (8 KB)**. Setting it to 4096 triggers a buffer panic (`FAILURE_CONFIG_STORE_FAILURE`) on boot because active Parameter Groups occupy ~5.1 KB.
4. **Soft-Float Architecture & 1.0 kHz Loop Clamping:**
   - The ET1031 core has no floating-point unit (FPU). All floating-point operations use software emulation routines.
   - To prevent CPU overload and scheduler stalls, the PID loop is locked to **1.0 kHz** (`DEFAULT_PID_PROCESS_DENOM 8`).
   - At 100 MHz, the complete PID task finishes in $\approx 290\,\mu\text{s}$ Worst-Case Execution Time (WCET), preserving $> 65\%$ idle headroom.
5. **Atomic Operations via CSR `mstatus.MIE`:**
   - THEJAS32 lacks ARM NVIC `BASEPRI` registers and RISC-V Atomic (`A`) extension instructions (`lr.w`/`sc.w`).
   - Atomic blocks must disable and restore Machine Interrupt Enable (`mstatus.MIE`, bit 3) via inline CSR assembly (`csrrci` / `csrs`). Never leave empty stubs!
6. **Non-Blocking Timer 1 PWM Motor Scheduler:**
   - The SoC does not feature dedicated advanced PWM timers with multi-channel compare outputs.
   - Motor outputs (`PA1`–`PA4`) are modulated by an ascending deadline event queue driven by **Hardware Timer 1 (IRQ 8)**, executing with $< 3\,\mu\text{s}$ CPU overhead. Never use busy-wait delay loops.
7. **Polled SPI0 Transfer Engine:**
   - SPI0 (Header J1) operates in polled mode without DMA.
   - Maximum sensor SPI clock for ICM-42688P must be clamped to **12.5 MHz** (Divisor 1). Divisor 0 generates 25.0 MHz, violating the IMU's 24.0 MHz absolute maximum rating.
8. **16550 UART0 Communication:**
   - Connected to the onboard Silicon Labs CP2102N USB-UART bridge on pins `PA30` (TX) and `PA31` (RX).
   - Operates with RX interrupts feeding software ring buffers, and synchronous bounded TX drain to prevent flight loop starvation.

---

## 6. Pre-Commit Quality & Bench Verification Checklist

Before reporting any task complete or submitting code changes:

- [ ] **Opcode Audit:** Run `riscv32-esp-elf-objdump -d <binary>.elf | grep -E "^\s*[0-9a-f]+:\s+[0-9a-f]{4}\s+"` and verify that the compressed instruction count is **strictly 0**.
- [ ] **Memory Bounds Check:** Verify that the total binary artifact is **$\le 248\text{ KB}$** and execution origin is `0x00202000`.
- [ ] **Target Branch Check:** Confirm changes are on branch **`rebuild`** (not `master`).
- [ ] **Knowledge Base Synchronization:** If any driver or architectural behavior was changed or discovered, document it in an evaluation/retrospective log and link it in [`knowledgebase/INDEX.md`](file:///home/ivan/Desktop/betaflight/knowledgebase/INDEX.md).
- [ ] **Physical Hardware Safety:** Ensure propellers are removed, an external 5V BEC supplies the flight receiver/peripherals, and a low-ESR capacitor ($470\text{--}1000\,\mu\text{s}$) is installed across the ESC battery leads to protect 3.3V LVCMOS pins.

---

## 7. Master Planning Suite & Navigation
Before writing or proposing any code:
* Consult the **[Master Macro Plan](file:///home/ivan/Desktop/betaflight/MACRO_PLAN.md)** to identify the current development phase and gating criteria.
* Access the granular micro-plans in [`PLAN/`](file:///home/ivan/Desktop/betaflight/PLAN/) for component-specific registers, algorithms, expected inputs, and expected outputs.
* Verify all hardware assumptions against [`knowledgebase/CRITICAL_KNOWLEDGE_GAPS_AND_ASSUMPTIONS.md`](file:///home/ivan/Desktop/betaflight/knowledgebase/CRITICAL_KNOWLEDGE_GAPS_AND_ASSUMPTIONS.md).
