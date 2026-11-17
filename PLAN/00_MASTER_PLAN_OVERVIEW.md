# Betaflight ARIES v3.0: Master Micro-Planning Suite Index

> **Target Platform**: C-DAC ARIES v3.0 (THEJAS32 / VEGA ET1031 RISC-V RV32IM @ 100 MHz)  
> **Repository Active Branch**: `rebuild`  
> **Master Knowledge Graph**: [`knowledgebase/PORTING_KNOWLEDGE_GRAPH.md`](file:///home/ivan/Desktop/betaflight/knowledgebase/PORTING_KNOWLEDGE_GRAPH.md)  
> **Knowledge Base Master Index**: [`knowledgebase/INDEX.md`](file:///home/ivan/Desktop/betaflight/knowledgebase/INDEX.md)  
> **Critical Knowledge Gaps**: [`knowledgebase/CRITICAL_KNOWLEDGE_GAPS_AND_ASSUMPTIONS.md`](file:///home/ivan/Desktop/betaflight/knowledgebase/CRITICAL_KNOWLEDGE_GAPS_AND_ASSUMPTIONS.md)

---

## 1. Executive Suite Summary

This micro-planning suite constitutes the exhaustive, zero-assumption engineering blueprint for porting Betaflight 4.x to physical C-DAC ARIES v3.0 hardware. Every plan is strictly anchored in verified laboratory findings, C-DAC VEGA SDK implementations, and the 8 inviolable silicon ground truths.

The micro-planning suite is partitioned into six cohesive domain architecture documents, each structured with the mandatory 4-part specification (**Why**, **How**, **Expected Inputs**, **Expected Results**) for every microcomponent:

```
PLAN/
 ├── 00_MASTER_PLAN_OVERVIEW.md                    <-- (This Document) Master Architecture & Suite Index
 ├── 01_BOOT_LINKER_AND_CORE_PRIMITIVES.md        <-- Machine boot, GPR sanitization, linker script, atomics & clocks
 ├── 02_BUILD_SYSTEM_AND_MEMORY_OPTIMIZATION.md   <-- Toolchain isolation, multilib RVC prevention, MCU_EXCLUDES, PG bounds
 ├── 03_SERIAL_COMMUNICATION_MSP_AND_CLI.md       <-- 16550 UART0/1, bounded TX burst drain, zero-allocation MSP & CLI
 ├── 04_GPIO_EXTI_AND_HARDWARE_TIMERS.md          <-- Flat 32-bit GPIO, O(1) EXTI ctz fast-path, Timer 1 soft-PWM
 ├── 05_SPI_BUS_IMU_AND_FLASH_PERSISTENCE.md      <-- Polled SPI0 (12.5 MHz), ICM-42688P sync, CONFIG_IN_RAM & MDP_SPI_3
 └── 06_SENSORS_RC_TELEMETRY_AND_SCHEDULER.md     <-- Non-inverted CRSF, ADC fallback, 1.0 kHz cooperative scheduler
```

---

## 2. The 8 Inviolable Silicon Ground Truths & Domain Map

| # | Silicon Ground Truth | Architectural Constraint | Enforcing Domain Plan | Primary Source Nodes |
| :-: | :--- | :--- | :--- | :--- |
| **1** | **RV32IM Only — Strictly 0 Compressed Instructions** | VEGA ET1031 core lacks 'C' extension. Any 16-bit opcode triggers `mcause = 2`. Link strictly `rv32i_zicsr_zifencei/ilp32`. | [`PLAN-DOM-01`](file:///home/ivan/Desktop/betaflight/PLAN/01_BOOT_LINKER_AND_CORE_PRIMITIVES.md)<br>[`PLAN-DOM-02`](file:///home/ivan/Desktop/betaflight/PLAN/02_BUILD_SYSTEM_AND_MEMORY_OPTIMIZATION.md) | `01_EVAL_STARTUP_S.md`<br>`13_EVAL_MAKEFILES_ARIES_MK_TARGET_MK.md` |
| **2** | **In-SRAM Execution at `0x00202000` (No XIP)** | External SPI flash is not memory-mapped. Total firmware image must fit within $\le 248\text{ KB}$ SRAM (`0x0023FFFF`). | [`PLAN-DOM-01`](file:///home/ivan/Desktop/betaflight/PLAN/01_BOOT_LINKER_AND_CORE_PRIMITIVES.md)<br>[`PLAN-DOM-02`](file:///home/ivan/Desktop/betaflight/PLAN/02_BUILD_SYSTEM_AND_MEMORY_OPTIMIZATION.md) | `02_EVAL_LINKER_ARIES_V3_LD.md`<br>`11_TARGET_OPTIMIZATION_BUDGET.md` |
| **3** | **Parameter Groups in RAM (`CONFIG_IN_RAM`)** | Physical flash writes trigger `mcause = 7` traps. Store PGs in 8 KB RAM buffer (`EEPROM_SIZE 8192`). 4096 triggers boot panic. | [`PLAN-DOM-02`](file:///home/ivan/Desktop/betaflight/PLAN/02_BUILD_SYSTEM_AND_MEMORY_OPTIMIZATION.md)<br>[`PLAN-DOM-05`](file:///home/ivan/Desktop/betaflight/PLAN/05_SPI_BUS_IMU_AND_FLASH_PERSISTENCE.md) | `10_EVAL_CONFIG_FLASH_ARIES_C.md`<br>`14_EVAL_TARGET_H_AND_CONFIG.md` |
| **4** | **Soft-Float Architecture & 1.0 kHz Loop Clamping** | No hardware FPU. Clamping loop to 1.0 kHz (`DEFAULT_PID_PROCESS_DENOM 8`) keeps PID math to $\approx 290\,\mu\text{s}$ ($> 65\%$ idle). | [`PLAN-DOM-02`](file:///home/ivan/Desktop/betaflight/PLAN/02_BUILD_SYSTEM_AND_MEMORY_OPTIMIZATION.md)<br>[`PLAN-DOM-06`](file:///home/ivan/Desktop/betaflight/PLAN/06_SENSORS_RC_TELEMETRY_AND_SCHEDULER.md) | `14_EVAL_TARGET_H_AND_CONFIG.md`<br>`10_SCHEDULER_AND_TASK_ARCHITECTURE.md` |
| **5** | **Atomic Operations via CSR `mstatus.MIE`** | No ARM `BASEPRI` and no RISC-V 'A' extension. Manipulate `mstatus.MIE` (bit 3) via state-preserving `csrrci` sequence. | [`PLAN-DOM-01`](file:///home/ivan/Desktop/betaflight/PLAN/01_BOOT_LINKER_AND_CORE_PRIMITIVES.md)<br>[`PLAN-DOM-06`](file:///home/ivan/Desktop/betaflight/PLAN/06_SENSORS_RC_TELEMETRY_AND_SCHEDULER.md) | `04_EVAL_PLATFORM_H.md`<br>`12_EVAL_STUB_DRIVERS_C.md` |
| **6** | **Non-Blocking Timer 1 PWM Motor Scheduler** | Motors `PA1`–`PA4` use Hardware Timer 1 (IRQ 8) ascending deadline event scheduling ($< 3\,\mu\text{s}$ CPU total). Never busy-wait. | [`PLAN-DOM-04`](file:///home/ivan/Desktop/betaflight/PLAN/04_GPIO_EXTI_AND_HARDWARE_TIMERS.md) | `11_EVAL_PWM_MOTOR_ARIES_C.md`<br>`05_TIMER_AND_MOTOR_PWM.md` |
| **7** | **Polled SPI0 Transfer Engine** | Header J1 SPI0 operates polled without DMA. SCK must be clamped to 12.5 MHz (Divisor 1). Divisor 0 (25 MHz) overclocks IMU. | [`PLAN-DOM-05`](file:///home/ivan/Desktop/betaflight/PLAN/05_SPI_BUS_IMU_AND_FLASH_PERSISTENCE.md) | `09_EVAL_BUS_SPI_ARIES_C.md`<br>`07_BUS_SPI_AND_I2C.md` |
| **8** | **16550 UART0 Communication** | Dedicated pins `PA30` (TX) and `PA31` (RX) to CP2102N USB bridge. RX interrupt ring buffer; bounded synchronous FIFO TX drain. | [`PLAN-DOM-03`](file:///home/ivan/Desktop/betaflight/PLAN/03_SERIAL_COMMUNICATION_MSP_AND_CLI.md) | `06_EVAL_SERIAL_UART_ARIES_C.md`<br>`03_SERIAL_UART_AND_MSP.md` |

---

## 3. Detailed Domain Plan Directory

### [01. Boot, Linker & Core Primitives](file:///home/ivan/Desktop/betaflight/PLAN/01_BOOT_LINKER_AND_CORE_PRIMITIVES.md)
* **Microcomponent 01_A**: Machine Startup, GPR Sanitization & Trap Vector (`startup.S`)
* **Microcomponent 01_B**: Linker Script & SRAM Memory Budget Enforcement (`aries_v3.ld`)
* **Microcomponent 01_C**: Atomic Operations & Platform Primitives (`platform.h`, `stub_drivers.c`)
* **Microcomponent 01_D**: System Timebase, Microsecond Clocks & Monotonicity (`system.c`)

### [02. Build System & Memory Optimization](file:///home/ivan/Desktop/betaflight/PLAN/02_BUILD_SYSTEM_AND_MEMORY_OPTIMIZATION.md)
* **Microcomponent 02_A**: Dynamic Toolchain Detection & Multilib RVC Isolation Engine (`ARIES.mk`)
* **Microcomponent 02_B**: Target Makefile Compilation Hierarchy & Subsystem Exclusion (`target.mk`)
* **Microcomponent 02_C**: Target Header Architecture & Silicon Boundary Enforcement (`target.h`, `config.h`)
* **Microcomponent 02_D**: Parameter Group Budgeting, RAM Storage & Compile-Time Static Assertions
* **Microcomponent 02_E**: Binary Size Auditing, Opcode Verification Pipeline & Artifact Generation

### [03. Serial Communication, MSP & CLI](file:///home/ivan/Desktop/betaflight/PLAN/03_SERIAL_COMMUNICATION_MSP_AND_CLI.md)
* **Microcomponent 03_A**: 16550 APB UART Hardware Driver & Clock Baud Engine (UART0 & UART1)
* **Microcomponent 03_B**: Bounded Synchronous FIFO Transmit Drain & Anti-Starvation Engine
* **Microcomponent 03_C**: Zero-Allocation RX Interrupt Ring Buffer & Overrun Error Recovery
* **Microcomponent 03_D**: Zero-Allocation MSP Protocol State Machine & 7-Step Handshake Engine
* **Microcomponent 03_E**: High-Usability Serial CLI Engine & Clean Platform Isolation Hooks

### [04. GPIO, EXTI & Hardware Timers](file:///home/ivan/Desktop/betaflight/PLAN/04_GPIO_EXTI_AND_HARDWARE_TIMERS.md)
* **Microcomponent 04_A**: Flat Dual-Bank Address-Decoded Atomic GPIO Driver (`io_aries.c`)
* **Microcomponent 04_B**: Open-Drain Simulation & RISC-V Memory Ordering Fences
* **Microcomponent 04_C**: EXTI Controller, PLIC IRQ 10/11 Demuxing & `__builtin_ctz` Fast-Path (`exti_aries.c`)
* **Microcomponent 04_D**: Hardware Timer 1 Non-Blocking Ascending Deadline Motor Scheduler (`pwm_motor_aries.c`)
* **Microcomponent 04_E**: Non-Null Motor VTable & Configurator Interactive Control

### [05. SPI Bus, IMU & Flash Persistence](file:///home/ivan/Desktop/betaflight/PLAN/05_SPI_BUS_IMU_AND_FLASH_PERSISTENCE.md)
* **Microcomponent 05_A**: Polled SPI0 Master Controller & Conservative 12.5 MHz Clock Engine (`bus_spi_aries.c`)
* **Microcomponent 05_B**: Segmented Bidirectional Bus Engine & Bus-Timeout Error Recovery
* **Microcomponent 05_C**: ICM-42688P High-Rate Sensor Synchronization & Header J1 Pinout Binding
* **Microcomponent 05_D**: In-SRAM Configuration Architecture (`CONFIG_IN_RAM` & `EEPROM_SIZE 8192`)
* **Microcomponent 05_E**: JEDEC Command-Based Cold-Reboot Flash Persistence Engine (`MDP_SPI_3`)

### [06. Sensors, RC Telemetry & Scheduler](file:///home/ivan/Desktop/betaflight/PLAN/06_SENSORS_RC_TELEMETRY_AND_SCHEDULER.md)
* **Microcomponent 06_A**: Serial Receiver Subsystem & Non-Inverted CRSF/ELRS Engine (UART1)
* **Microcomponent 06_B**: Analog ADC Architecture & Safe Battery Telemetry Fallback (`adc_aries.c`, `U9`)
* **Microcomponent 06_C**: 1.0 kHz Cooperative Task Scheduler & Phased System Initialization (`main.c`)
* **Microcomponent 06_D**: State-Preserving RV32IM Software Atomics & Linker Isolation Shims (`stub_drivers.c`)
* **Microcomponent 06_E**: Flight Loop Timing Budgets, Soft-Float Math Profiling & CPU Headroom Verification

---

## 4. Anti-Assumption Guardrail Verification Matrix

Every plan incorporates explicit guardrails derived directly from [`knowledgebase/CRITICAL_KNOWLEDGE_GAPS_AND_ASSUMPTIONS.md`](file:///home/ivan/Desktop/betaflight/knowledgebase/CRITICAL_KNOWLEDGE_GAPS_AND_ASSUMPTIONS.md):

| Gap / Assumption Topic | Risk / Failure Mode | Verified Safe Architecture / Bound | Plan Reference |
| :--- | :--- | :--- | :--- |
| **Physical IMU Alignment** | Flyaway on arming due to inverted axes | Configurable orientation in `target.h` / CLI (`set gyro_1_align`); never hardcode assumptions. | [`PLAN-DOM-05`](file:///home/ivan/Desktop/betaflight/PLAN/05_SPI_BUS_IMU_AND_FLASH_PERSISTENCE.md#microcomponent-05_c-icm-42688p-high-rate-sensor-synchronization--header-j1-pinout-binding) |
| **RC Serial Inversion** | 100% framing errors on inverted SBUS | 16550 has no hardware inverters. Use native non-inverted **ExpressLRS (CRSF)** at 420,000 baud. | [`PLAN-DOM-03`](file:///home/ivan/Desktop/betaflight/PLAN/03_SERIAL_COMMUNICATION_MSP_AND_CLI.md#microcomponent-03_a-16550-apb-uart-hardware-driver--clock-baud-engine-uart0--uart1)<br>[`PLAN-DOM-06`](file:///home/ivan/Desktop/betaflight/PLAN/06_SENSORS_RC_TELEMETRY_AND_SCHEDULER.md#microcomponent-06_a-serial-receiver-subsystem--non-inverted-crsfelrs-engine-uart1) |
| **ESC Motor Signaling** | Busy-wait delays lock CPU for 1–2 ms | Timer 1 IRQ 8 ascending deadline event scheduler ($< 3\,\mu\text{s}$ CPU load). | [`PLAN-DOM-04`](file:///home/ivan/Desktop/betaflight/PLAN/04_GPIO_EXTI_AND_HARDWARE_TIMERS.md#microcomponent-04_d-hardware-timer-1-non-blocking-ascending-deadline-motor-scheduler-pwm_motor_ariesc) |
| **Battery ADC Sensing** | Returning 0V causes permanent arming lock | `#undef USE_ADC` or safe nominal cache in `adcGetValue()`; background `U9` I2C reader. | [`PLAN-DOM-06`](file:///home/ivan/Desktop/betaflight/PLAN/06_SENSORS_RC_TELEMETRY_AND_SCHEDULER.md#microcomponent-06_b-analog-adc-architecture--safe-battery-telemetry-fallback-adc_ariesc-u9) |
| **Flash Memory Mapping** | Writing `0x20000000` causes `mcause = 7` | No XIP. `#define CONFIG_IN_RAM` (8 KB SRAM) or command-based `MDP_SPI_3` (`0x10200100`). | [`PLAN-DOM-01`](file:///home/ivan/Desktop/betaflight/PLAN/01_BOOT_LINKER_AND_CORE_PRIMITIVES.md)<br>[`PLAN-DOM-05`](file:///home/ivan/Desktop/betaflight/PLAN/05_SPI_BUS_IMU_AND_FLASH_PERSISTENCE.md#microcomponent-05_e-jedec-command-based-cold-reboot-flash-persistence-engine-mdp_spi_3) |
| **SRAM Memory Budget** | Overflowing 248 KB corrupts execution | Strict linker check `ORIGIN = 0x00202000, LENGTH = 248K`; automated `size -B` check $\le 253,952$. | [`PLAN-DOM-01`](file:///home/ivan/Desktop/betaflight/PLAN/01_BOOT_LINKER_AND_CORE_PRIMITIVES.md#microcomponent-01_b-linker-script--sram-memory-budget-enforcement-aries_v3ld)<br>[`PLAN-DOM-02`](file:///home/ivan/Desktop/betaflight/PLAN/02_BUILD_SYSTEM_AND_MEMORY_OPTIMIZATION.md#microcomponent-02_e-binary-size-auditing-opcode-verification-pipeline--artifact-generation) |

---

## 5. Pre-Commit Verification Checklist for Development

Before declaring any implementation task complete during Phase 3 bringup:
- [ ] **Opcode Audit**: `riscv32-esp-elf-objdump -d betaflight_ARIESV3.elf | grep -E "\sc\.[a-z]" | wc -l` strictly equals **0**.
- [ ] **Binary Size Budget**: Total memory footprint ($\text{text} + \text{data} + \text{bss}$) strictly **$\le 248\text{ KB}$** ($\le 253,952\text{ bytes}$).
- [ ] **Active Git Branch**: Verified current branch is `rebuild` (`git branch --show-current`).
- [ ] **Config Persistence Bounds**: `EEPROM_SIZE >= 8192` verified by compile-time static assertion.
- [ ] **Motor VTable Safety**: `convertExternalToMotor` and `convertMotorToExternal` are non-null passthrough functions.
- [ ] **SPI Clock Safety**: SPI0 SCK divisor verified to produce strictly $\le 12.5\text{ MHz}$.
- [ ] **Serial Handshake**: Betaflight Configurator completes 7-step handshake over `UART0` and unlocks the interactive dashboard.