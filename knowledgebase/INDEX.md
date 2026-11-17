# Betaflight Porting Knowledge Graph Index (ARIES v3.0 / THEJAS32)

Welcome to the central Knowledge Graph index for the **Betaflight Port to ARIES v3.0 (THEJAS32 SoC / VEGA ET1031 RISC-V)**.

This index interconnects hardware datasheets, programming manuals, protocol specifications, target optimization strategies, official Betaflight architecture documentation, **25 deep hardware inspection logs**, **11 component knowledge nodes**, and **audited historical planning mistake analyses** into a fully navigable, non-hallucinated knowledge graph.

---

## 1. High-Level Knowledge Graph Overview

```mermaid
graph TD
    INDEX["Central Knowledge Graph Index (INDEX.md)"]

    subgraph HARDWARE_LAYER["Hardware & Silicon Specification"]
        DS_ANALYSIS["Aries v3 Datasheet Analysis<br/>(ARIES_V3_DATASHEET_ANALYSIS.md)"]
        HW_GRAPH["Hardware Visual Architecture<br/>(ARIES_V3_KNOWLEDGE_GRAPH.md)"]
        PROG_MANUAL["THEJAS32 Programmer's Manual<br/>(THEJAS32_PROGRAMMER_MANUAL_ANALYSIS.md)"]
    end

    subgraph HISTORICAL_AUDIT["Historical Planning & Implementation Audit (oldfiles/logs/)"]
        MISTAKE_LOG["Planning Mistakes Analysis<br/>(PLANNING_MISTAKES_ANALYSIS.md)"]
        MISTAKE_GRAPH["Mistake & Rectification Graph<br/>(HISTORICAL_PLANNING_MISTAKES_GRAPH.md)"]
        RETRO_GRAPH["Retrospective Implementation Graph<br/>(HISTORICAL_RETROSPECTIVE_GRAPH.md)"]
        RETRO_LOGS["14 Retrospective Audit Logs<br/>(knowledgebase/retrospective_logs/01..14)"]
    end

    subgraph INSPECTION_LOGS["Hardware Inspection & Comparative Logs (25 Files)"]
        STM_LOGS["STM32 Inspection Logs (01 - 16)<br/>(logs/hardware_inspection/01..16)"]
        ESP_LOGS["ESP32 Comparative Logs (17 - 25)<br/>(logs/hardware_inspection/17..25)"]
    end

    subgraph COMPONENT_NODES["Component Knowledge Nodes (knowledgebase/components/)"]
        C01["01_System_and_Clocks"]
        C02["02_Startup_Boot_Exceptions"]
        C03["03_Serial_UART_MSP"]
        C04["04_GPIO_Pin_IO"]
        C05["05_Timer_Motor_PWM"]
        C06["06_Interrupts_EXTI"]
        C07["07_Bus_SPI_I2C"]
        C08["08_Analog_ADC"]
        C09["09_Build_Linker_Memory"]
        C10["10_Scheduler_Task_Arch"]
        C11["11_Target_Opt_128KB"]
    end

    subgraph PORTING_BLUEPRINT["End-to-End Porting Implementation"]
        PORT_GRAPH["Master Porting Knowledge Graph<br/>(PORTING_KNOWLEDGE_GRAPH.md)"]
        OPT_128K["128KB Optimization Strategy<br/>(TARGET_OPTIMIZATION_128KB.md)"]
        MSP_GRAPH["Configurator Handshake & MSP<br/>(MSP_CONFIGURATOR_HANDSHAKE_GRAPH.md)"]
        BF_ARCH["Subsystems & Task Loop<br/>(BETAFLIGHT_ARCHITECTURE_GRAPH.md)"]
    end

    INDEX <==> PORT_GRAPH
    INDEX <==> DS_ANALYSIS
    INDEX <==> PROG_MANUAL
    INDEX <==> MISTAKE_LOG
    INDEX <==> MISTAKE_GRAPH

    PORT_GRAPH <==> COMPONENT_NODES
    PORT_GRAPH <==> STM_LOGS
    PORT_GRAPH <==> ESP_LOGS
    PORT_GRAPH <==> MISTAKE_GRAPH

    MISTAKE_LOG <--> MISTAKE_GRAPH
    MISTAKE_GRAPH -. Rectified by .-> COMPONENT_NODES
    COMPONENT_NODES <--> STM_LOGS
    COMPONENT_NODES <--> ESP_LOGS
    COMPONENT_NODES <--> HARDWARE_LAYER
```

---

## 2. Porting Component Knowledge Nodes (`knowledgebase/components/`)

| Node ID | Component Document | Focus & Implementation Scope | Backed References |
| :---: | :--- | :--- | :--- |
| **`PORT-COMP-01`** | **[01_SYSTEM_AND_CLOCKS.md](file:///home/ivan/Desktop/betaflight/knowledgebase/components/01_SYSTEM_AND_CLOCKS.md)** | 100 MHz timebase, 64-bit `mtime` up-counter (`0x0200BFF8`), eliminating SysTick rollover race, atomic double-read loop, `micros()` & `millis()` APIs | [01_STM32_COMMON_SYSTEM.md](file:///home/ivan/Desktop/betaflight/logs/hardware_inspection/01_STM32_COMMON_SYSTEM.md), [17_ESP32_SYSTEM_AND_TIMER.md](file:///home/ivan/Desktop/betaflight/logs/hardware_inspection/17_ESP32_SYSTEM_AND_TIMER.md) |
| **`PORT-COMP-02`** | **[02_STARTUP_BOOT_AND_EXCEPTIONS.md](file:///home/ivan/Desktop/betaflight/knowledgebase/components/02_STARTUP_BOOT_AND_EXCEPTIONS.md)** | Assembly `crt.S`, `Reset_Handler`, stack watermarking (`0xA5A5A5A5`), `mtvec` trap dispatcher, `mcause` decoding, reboot token safety | [02_STM32_SYSTEM_INIT.md](file:///home/ivan/Desktop/betaflight/logs/hardware_inspection/02_STM32_SYSTEM_INIT.md), [03_STM32_STARTUP_ASSEMBLY.md](file:///home/ivan/Desktop/betaflight/logs/hardware_inspection/03_STM32_STARTUP_ASSEMBLY.md), [04_STM32_FAULT_HANDLERS.md](file:///home/ivan/Desktop/betaflight/logs/hardware_inspection/04_STM32_FAULT_HANDLERS.md) |
| **`PORT-COMP-03`** | **[03_SERIAL_UART_AND_MSP.md](file:///home/ivan/Desktop/betaflight/knowledgebase/components/03_SERIAL_UART_AND_MSP.md)** | 16550 UART0 (`0x10000100`), divisor math @ 100 MHz (Divisor 54, +0.47% error), CP2102N bridge (Pins 107/108), ORE unfreeze, MSP parser state machine | [05_STM32_SERIAL_UART_HW.md](file:///home/ivan/Desktop/betaflight/logs/hardware_inspection/05_STM32_SERIAL_UART_HW.md), [06_STM32_SERIAL_UART_DRIVER.md](file:///home/ivan/Desktop/betaflight/logs/hardware_inspection/06_STM32_SERIAL_UART_DRIVER.md), [15_MSP_SERIAL_PARSER.md](file:///home/ivan/Desktop/betaflight/logs/hardware_inspection/15_MSP_SERIAL_PARSER.md), [18_ESP32_SERIAL_UART.md](file:///home/ivan/Desktop/betaflight/logs/hardware_inspection/18_ESP32_SERIAL_UART.md) |
| **`PORT-COMP-04`** | **[04_GPIO_AND_PIN_IO.md](file:///home/ivan/Desktop/betaflight/knowledgebase/components/04_GPIO_AND_PIN_IO.md)** | Flat 32-bit GPIO controller (`0x10000000`), $O(1)$ direct array index `&ioRecs[pin]` (256 bytes SRAM), eliminating multi-bank popcount tables | [08_STM32_IO_IMPL.md](file:///home/ivan/Desktop/betaflight/logs/hardware_inspection/08_STM32_IO_IMPL.md), [19_ESP32_IO_IMPL.md](file:///home/ivan/Desktop/betaflight/logs/hardware_inspection/19_ESP32_IO_IMPL.md) |
| **`PORT-COMP-05`** | **[05_TIMER_AND_MOTOR_PWM.md](file:///home/ivan/Desktop/betaflight/knowledgebase/components/05_TIMER_AND_MOTOR_PWM.md)** | Dedicated 8-channel PWM controller (`0x10000600`), uniform 100 MHz clock, eliminating APB multipliers and DMA collisions, non-null VTable safety | [09_STM32_TIMER_PWM_HW.md](file:///home/ivan/Desktop/betaflight/logs/hardware_inspection/09_STM32_TIMER_PWM_HW.md), [22_ESP32_PWM_DSHOT.md](file:///home/ivan/Desktop/betaflight/logs/hardware_inspection/22_ESP32_PWM_DSHOT.md) |
| **`PORT-COMP-06`** | **[06_INTERRUPTS_AND_EXTI.md](file:///home/ivan/Desktop/betaflight/knowledgebase/components/06_INTERRUPTS_AND_EXTI.md)** | Platform-Level Interrupt Controller (`PLIC` @ `0x0C000000`), single centralized GPIO ISR with `__builtin_ctz()` dispatch, Gyro data-ready synchronization | [08_STM32_IO_IMPL.md](file:///home/ivan/Desktop/betaflight/logs/hardware_inspection/08_STM32_IO_IMPL.md), [16_SCHEDULER_CORE.md](file:///home/ivan/Desktop/betaflight/logs/hardware_inspection/16_SCHEDULER_CORE.md), [20_ESP32_INTERRUPT_MODEL.md](file:///home/ivan/Desktop/betaflight/logs/hardware_inspection/20_ESP32_INTERRUPT_MODEL.md), [24_ESP32_DMA_AND_EXTI.md](file:///home/ivan/Desktop/betaflight/logs/hardware_inspection/24_ESP32_DMA_AND_EXTI.md) |
| **`PORT-COMP-07`** | **[07_BUS_SPI_AND_I2C.md](file:///home/ivan/Desktop/betaflight/knowledgebase/components/07_BUS_SPI_AND_I2C.md)** | Polled SPI0 (Flash `U5`) & SPI3 (IMU `J4`), polled I2C2 (`U9`), avoiding Cortex-M7 L1 D-Cache invalidations and GDMA bounce buffers, 9-pulse unstick routine | [10_STM32_BUS_SPI_HW.md](file:///home/ivan/Desktop/betaflight/logs/hardware_inspection/10_STM32_BUS_SPI_HW.md), [21_ESP32_BUS_SPI_I2C.md](file:///home/ivan/Desktop/betaflight/logs/hardware_inspection/21_ESP32_BUS_SPI_I2C.md) |
| **`PORT-COMP-08`** | **[08_ANALOG_ADC_SUBSYSTEM.md](file:///home/ivan/Desktop/betaflight/knowledgebase/components/08_ANALOG_ADC_SUBSYSTEM.md)** | Hardware reality: off-chip 4-channel I2C ADC (`U9`), omission of DMA, static RAM cache `aries_adc_raw[4]`, boot telemetry mock strategy | [11_STM32_ADC_IMPL.md](file:///home/ivan/Desktop/betaflight/logs/hardware_inspection/11_STM32_ADC_IMPL.md), [23_ESP32_ADC.md](file:///home/ivan/Desktop/betaflight/logs/hardware_inspection/23_ESP32_ADC.md) |
| **`PORT-COMP-09`** | **[09_BUILD_LINKER_AND_MEMORY.md](file:///home/ivan/Desktop/betaflight/knowledgebase/components/09_BUILD_LINKER_AND_MEMORY.md)** | RISC-V RV32IM toolchain flags (`-mabi=ilp32`), single-allocator 250 KB SRAM script (`linker_ram.lds`), Parameter Group anchors (`.pg_registry`), `dma_stub` mechanics | [12_STM32_LINKER_AND_SECTIONS.md](file:///home/ivan/Desktop/betaflight/logs/hardware_inspection/12_STM32_LINKER_AND_SECTIONS.md), [13_STM32_BUILD_MAKEFILE.md](file:///home/ivan/Desktop/betaflight/logs/hardware_inspection/13_STM32_BUILD_MAKEFILE.md), [25_ESP32_BUILD_AND_LINKER.md](file:///home/ivan/Desktop/betaflight/logs/hardware_inspection/25_ESP32_BUILD_AND_LINKER.md) |
| **`PORT-COMP-10`** | **[10_SCHEDULER_AND_TASK_ARCHITECTURE.md](file:///home/ivan/Desktop/betaflight/knowledgebase/components/10_SCHEDULER_AND_TASK_ARCHITECTURE.md)** | 3-stage boot lifecycle (`initPhase1..3`), cooperative non-preemptive scheduler loop, deterministic task timing, `TASK_SERIAL` non-blocking guarantee | [14_MAIN_STARTUP_FLOW.md](file:///home/ivan/Desktop/betaflight/logs/hardware_inspection/14_MAIN_STARTUP_FLOW.md), [15_MSP_SERIAL_PARSER.md](file:///home/ivan/Desktop/betaflight/logs/hardware_inspection/15_MSP_SERIAL_PARSER.md), [16_SCHEDULER_CORE.md](file:///home/ivan/Desktop/betaflight/logs/hardware_inspection/16_SCHEDULER_CORE.md) |
| **`PORT-COMP-11`** | **[11_TARGET_OPTIMIZATION_BUDGET.md](file:///home/ivan/Desktop/betaflight/knowledgebase/components/11_TARGET_OPTIMIZATION_BUDGET.md)** | Exact modular stripping flags, footprint breakdown (~72 KB total), compiler/linker directives, board configuration `target.h` and `target.mk` | [TARGET_OPTIMIZATION_128KB.md](file:///home/ivan/Desktop/betaflight/knowledgebase/TARGET_OPTIMIZATION_128KB.md), [13_STM32_BUILD_MAKEFILE.md](file:///home/ivan/Desktop/betaflight/logs/hardware_inspection/13_STM32_BUILD_MAKEFILE.md), [25_ESP32_BUILD_AND_LINKER.md](file:///home/ivan/Desktop/betaflight/logs/hardware_inspection/25_ESP32_BUILD_AND_LINKER.md) |

---

## 3. Historical Planning & Implementation Audit (`oldfiles/logs/`)

### A. Foundational Planning Analyses & Mistake Cascades
| Node Document | Focus & Analysis | Key Entities & Concepts Covered |
| :--- | :--- | :--- |
| **[PLANNING_MISTAKES_ANALYSIS.md](file:///home/ivan/Desktop/betaflight/knowledgebase/PLANNING_MISTAKES_ANALYSIS.md)** | **Historical Planning Audit** | Deep analysis of the 7 foundational planning mistakes: XIP flash assumption, imaginary `0x10000000` SRAM base, `MCU_FLASH_SIZE := 2048` bloat, Renode simulation false security, `.hex` vs `.bin` flasher confusion, 43s `mcycle` timebase rollover. |
| **[HISTORICAL_PLANNING_MISTAKES_GRAPH.md](file:///home/ivan/Desktop/betaflight/knowledgebase/HISTORICAL_PLANNING_MISTAKES_GRAPH.md)** | **Mistake & Rectification Graph** | Visual Mermaid cascade showing how planning flaws caused hardware failure, accompanied by an explicit old-plan vs new-architecture rectification matrix. |
| **[HISTORICAL_RETROSPECTIVE_GRAPH.md](file:///home/ivan/Desktop/betaflight/knowledgebase/HISTORICAL_RETROSPECTIVE_GRAPH.md)** | **Implementation Retrospective Graph** | Comprehensive retrospective knowledge graph mapping the 0–10 scoring spectrum, the Five Great Failure Cascades (DMA truncation, simulation trap, THRE storm, unarmed IRQs, fallback falsification), and the 128 KB rebuild rectification matrix. |

### B. Retrospective Audit Logs Catalog (`knowledgebase/retrospective_logs/`)
| Log Document | Source Audited | Decision Scoring | Core Findings & Silicon Lessons |
| :--- | :--- | :---: | :--- |
| **[01_RETROSPECTIVE_DEBUG_SESSION.md](file:///home/ivan/Desktop/betaflight/knowledgebase/retrospective_logs/01_RETROSPECTIVE_DEBUG_SESSION.md)** | `DEBUG_SESSION.md` | 7 Scored | Unmasking `mstatus.MIE` and `mie.MEIE`, 16550 THRE interrupt storm, ASCII telemetry pollution on MSP wire |
| **[02_RETROSPECTIVE_HAL_PROGRESS.md](file:///home/ivan/Desktop/betaflight/knowledgebase/retrospective_logs/02_RETROSPECTIVE_HAL_PROGRESS.md)** | `HAL_PROGRESS.md` | 25 Scored | Phases 1–8 HAL, Hardware Hurdles H1–H22, No-XIP in-SRAM boot protocol discovery (`0x00202000`, 248 KB limit) |
| **[03_RETROSPECTIVE_GEMINI.md](file:///home/ivan/Desktop/betaflight/knowledgebase/retrospective_logs/03_RETROSPECTIVE_GEMINI.md)** | `GEMINI.md` | 9 Scored | Agent handoff notes, NIDAR mission context, document split personality (obsolete 770 KB / `0x1000FEF0`) |
| **[04_RETROSPECTIVE_TEST_LOGS.md](file:///home/ivan/Desktop/betaflight/knowledgebase/retrospective_logs/04_RETROSPECTIVE_TEST_LOGS.md)** | `TEST_LOGS.md` | 10 Scored | The "100% Passing Tests on Non-Working Silicon" paradox, `micros()` reciprocal optimization, RVC opcode audit |
| **[05_RETROSPECTIVE_TEST_PLAN_PHASES.md](file:///home/ivan/Desktop/betaflight/knowledgebase/retrospective_logs/05_RETROSPECTIVE_TEST_PLAN_PHASES.md)** | `SOFTWARE_TEST_PLAN_PHASES.md` | 12 Scored | Modular M0–M3 mocks, test shims, hardcoded `_start == 0x20000000` assertion fallacy in `build_verify.sh` |
| **[06_RETROSPECTIVE_SOFTWARE_TEST_PLAN.md](file:///home/ivan/Desktop/betaflight/knowledgebase/retrospective_logs/06_RETROSPECTIVE_SOFTWARE_TEST_PLAN.md)** | `SOFTWARE_TEST_PLAN.md` | 16 Scored | 10,000 MSP packet test, arming lockouts, failsafe timings, internal contradiction between Module 1 and script |
| **[07_RETROSPECTIVE_TEST_PLAN.md](file:///home/ivan/Desktop/betaflight/knowledgebase/retrospective_logs/07_RETROSPECTIVE_TEST_PLAN.md)** | `TEST_PLAN.md` | 8 Scored | Propellers Off rule, 5V BEC and TVS/low-ESR capacitor rules, 3.3V ceiling, soft-float feature restrictions |
| **[08_RETROSPECTIVE_PORT_GUIDE.md](file:///home/ivan/Desktop/betaflight/knowledgebase/retrospective_logs/08_RETROSPECTIVE_PORT_GUIDE.md)** | `PORT_GUIDE.md` | 8 Scored | Day-1 feasibility, CP2102N USB-to-UART bridge advantage, origin of the `0x20000000`/`0x10000000` fallacy |
| **[09_RETROSPECTIVE_COMPILATION_GUIDE.md](file:///home/ivan/Desktop/betaflight/knowledgebase/retrospective_logs/09_RETROSPECTIVE_COMPILATION_GUIDE.md)** | `COMPILATION_GUIDE.md` | 7 Scored | GCC 12/13 `-march=rv32im_zicsr` flag necessity, atomic stub emulation, flashing command pitfalls |
| **[10_RETROSPECTIVE_RENODE_CHANGES.md](file:///home/ivan/Desktop/betaflight/knowledgebase/retrospective_logs/10_RETROSPECTIVE_RENODE_CHANGES.md)** | `RENODE_CHANGES.md` | 6 Scored | **The Simulation Trap:** Modifying Renode CPU to `rv32im_zicsr_c` to silence opcode `0x433d` instead of fixing multilib |
| **[11_RETROSPECTIVE_BUILD_AND_BENCHMARK.md](file:///home/ivan/Desktop/betaflight/knowledgebase/retrospective_logs/11_RETROSPECTIVE_BUILD_AND_BENCHMARK.md)** | `buildAndBenchmark.sh` | 5 Scored | Symbol-range objdump disassembly audit for soft-float, calculating Flash % against fictional 2 MB |
| **[12_RETROSPECTIVE_RUN_SIMULATION.md](file:///home/ivan/Desktop/betaflight/knowledgebase/retrospective_logs/12_RETROSPECTIVE_RUN_SIMULATION.md)** | `runSimulation.sh` | 5 Scored | **The Fallback Falsification:** Script hardcoding fake `PC=0x200000b0` / `SP=0x10010b30` if grep returns empty |
| **[13_RETROSPECTIVE_STARTUP_AGENT_SPEC.md](file:///home/ivan/Desktop/betaflight/knowledgebase/retrospective_logs/13_RETROSPECTIVE_STARTUP_AGENT_SPEC.md)** | `ariesv3_betaflight_startup_agent_spec.md` | 6 Scored | A masterpiece of prompt engineering & epistemic humility (Verified vs. Inferred vs. Unverified) that was later ignored |
| **[14_RETROSPECTIVE_PRESENTATION_PLAN.md](file:///home/ivan/Desktop/betaflight/knowledgebase/retrospective_logs/14_RETROSPECTIVE_PRESENTATION_PLAN.md)** | `PRESENTATION_PLAN.md` | 7 Scored | Strategic DIR-V framing, real-time control loop microsecond budget (290 µs WCET), custom 30.5mm PCB roadmap |

---

## 4. Master Porting Architecture & Reference Catalog

| Node Document | Primary Focus | Key Entities & Concepts Covered |
| :--- | :--- | :--- |
| **[PORTING_KNOWLEDGE_GRAPH.md](file:///home/ivan/Desktop/betaflight/knowledgebase/PORTING_KNOWLEDGE_GRAPH.md)** | **Master Porting Blueprint** | End-to-end component-by-component implementation architecture for ARIES v3.0 (RV32IM, 16550 UART0, 64-bit mtime, Flat GPIO, 8-Ch PWM, Polled SPI/I2C, SRAM Boot, <128KB Target) |
| **[ARIES_V3_DATASHEET_ANALYSIS.md](file:///home/ivan/Desktop/betaflight/knowledgebase/ARIES_V3_DATASHEET_ANALYSIS.md)** | Hardware Specifications | THEJAS32 SoC, VEGA ET1031, Pinout, 2023 vs 2024 revisions, CP2102N, Power tree |
| **[ARIES_V3_KNOWLEDGE_GRAPH.md](file:///home/ivan/Desktop/betaflight/knowledgebase/ARIES_V3_KNOWLEDGE_GRAPH.md)** | Visual Topology & ER Graph | Board power distribution, SoC peripherals, Betaflight pin allocation matrix, entity relationships |
| **[THEJAS32_PROGRAMMER_MANUAL_ANALYSIS.md](file:///home/ivan/Desktop/betaflight/knowledgebase/THEJAS32_PROGRAMMER_MANUAL_ANALYSIS.md)** | Core & Programming Model | RISC-V RV32IM ISA, 250KB SRAM at `0x200000`, 16550 UART registers, `crt.S`, XMODEM loader |
| **[MSP_CONFIGURATOR_HANDSHAKE_GRAPH.md](file:///home/ivan/Desktop/betaflight/knowledgebase/MSP_CONFIGURATOR_HANDSHAKE_GRAPH.md)** | Serial Protocol & Handshake | MultiWii Serial Protocol v1/v2, 7-step handshake sequence, packet framing, CP2102N UART0 streaming |
| **[TARGET_OPTIMIZATION_128KB.md](file:///home/ivan/Desktop/betaflight/knowledgebase/TARGET_OPTIMIZATION_128KB.md)** | Sizing Strategy (<128KB) | Modular feature stripping (`USE_...` defines), `-Wl,--gc-sections`, `-Os`, RAM execution footprint |
| **[BETAFLIGHT_ARCHITECTURE_GRAPH.md](file:///home/ivan/Desktop/betaflight/knowledgebase/BETAFLIGHT_ARCHITECTURE_GRAPH.md)** | Subsystems & Execution Loop | Cooperative task scheduler (`taskTable`), `extDevice_t` and `busDevice_t` abstractions, Driver hierarchy |

---

## 5. Hardware Inspection Logs Directory (`logs/hardware_inspection/`)

### A. STM32 Reference Platform Logs (Logs 01 – 16)
1. **[01_STM32_COMMON_SYSTEM.md](file:///home/ivan/Desktop/betaflight/logs/hardware_inspection/01_STM32_COMMON_SYSTEM.md)**: System Clocks, SysTick Rollover Race Condition, DWT Cycle Counter Unlock.
2. **[02_STM32_SYSTEM_INIT.md](file:///home/ivan/Desktop/betaflight/logs/hardware_inspection/02_STM32_SYSTEM_INIT.md)**: Hardware `systemInit()`, Persistent Bootloader Tokens, Peripheral Clock Gating, VTOR Relocation.
3. **[03_STM32_STARTUP_ASSEMBLY.md](file:///home/ivan/Desktop/betaflight/logs/hardware_inspection/03_STM32_STARTUP_ASSEMBLY.md)**: `Reset_Handler`, DFU Hook, Stack Watermarking (`0xA5A5A5A5`), FPU Enablement.
4. **[04_STM32_FAULT_HANDLERS.md](file:///home/ivan/Desktop/betaflight/logs/hardware_inspection/04_STM32_FAULT_HANDLERS.md)**: Naked Exception Handlers, MSP/PSP Stack Pointer Extraction, SCB Fault Register Logging.
5. **[05_STM32_SERIAL_UART_HW.md](file:///home/ivan/Desktop/betaflight/logs/hardware_inspection/05_STM32_SERIAL_UART_HW.md)**: UART Hardware Abstraction Layer, Inversion Constraints, Half-Duplex Single-Wire Open-Drain.
6. **[06_STM32_SERIAL_UART_DRIVER.md](file:///home/ivan/Desktop/betaflight/logs/hardware_inspection/06_STM32_SERIAL_UART_DRIVER.md)**: `uartIrqHandler`, ORE Overrun Unfreeze, IDLE Clear Sequence, TXE CPU Spinning Fix.
7. **[07_STM32_SERIAL_UART_PINCONFIG.md](file:///home/ivan/Desktop/betaflight/logs/hardware_inspection/07_STM32_SERIAL_UART_PINCONFIG.md)**: Dynamic Alternate Function Matching, Silicon Pin Swap Detection.
8. **[08_STM32_IO_IMPL.md](file:///home/ivan/Desktop/betaflight/logs/hardware_inspection/08_STM32_IO_IMPL.md)**: Multi-Bank GPIOA–I Pin Abstraction, Sparse Bitmask Compression via `popcount`.
9. **[09_STM32_TIMER_PWM_HW.md](file:///home/ivan/Desktop/betaflight/logs/hardware_inspection/09_STM32_TIMER_PWM_HW.md)**: Asymmetric APB1/APB2 Timer Multipliers, Fixed DMA Channel Collision Matrix.
10. **[10_STM32_BUS_SPI_HW.md](file:///home/ivan/Desktop/betaflight/logs/hardware_inspection/10_STM32_BUS_SPI_HW.md)**: Cortex-M7 L1 D-Cache Coherency Invalidation, STM32F4 DMA2 Errata 2.1.10.
11. **[11_STM32_ADC_IMPL.md](file:///home/ivan/Desktop/betaflight/logs/hardware_inspection/11_STM32_ADC_IMPL.md)**: Multi-Channel DMA Scan Sequence Decoupling, Factory Bandgap Calibration (`VREFINTCAL`).
12. **[12_STM32_LINKER_AND_SECTIONS.md](file:///home/ivan/Desktop/betaflight/logs/hardware_inspection/12_STM32_LINKER_AND_SECTIONS.md)**: Uneven Flash Sector Splitting (16 KB vs 128 KB), Hot Reboot Flag Reservation.
13. **[13_STM32_BUILD_MAKEFILE.md](file:///home/ivan/Desktop/betaflight/logs/hardware_inspection/13_STM32_BUILD_MAKEFILE.md)**: Multi-Tier Optimization (`-O3` vs `-Os`), Native USB Middleware Savings (~30 KB).
14. **[14_MAIN_STARTUP_FLOW.md](file:///home/ivan/Desktop/betaflight/logs/hardware_inspection/14_MAIN_STARTUP_FLOW.md)**: Phased Boot Sequence (`initPhase1..3`), Cooperative Main Loop `scheduler()`.
15. **[15_MSP_SERIAL_PARSER.md](file:///home/ivan/Desktop/betaflight/logs/hardware_inspection/15_MSP_SERIAL_PARSER.md)**: Zero-Allocation Heapless MSP State Machine, Line Noise Resynchronization.
16. **[16_SCHEDULER_CORE.md](file:///home/ivan/Desktop/betaflight/logs/hardware_inspection/16_SCHEDULER_CORE.md)**: Cooperative Multitasking, Gyro EXTI Software PLL, `TASK_SERIAL` Non-Blocking Guarantee.

### B. ESP32 Cross-Architecture Comparative Logs (Logs 17 – 25)
17. **[17_ESP32_SYSTEM_AND_TIMER.md](file:///home/ivan/Desktop/betaflight/logs/hardware_inspection/17_ESP32_SYSTEM_AND_TIMER.md)**: Empty Timer Hardware Array (`fullTimerHardware[1]`), 64-bit `SYSTIMER` at 16 MHz. *(vs Logs 01 & 09)*
18. **[18_ESP32_SERIAL_UART.md](file:///home/ivan/Desktop/betaflight/logs/hardware_inspection/18_ESP32_SERIAL_UART.md)**: Universal GPIO Matrix Crossbar, 128-byte Hardware FIFOs, Dynamic Interrupt Attachment. *(vs Logs 05, 06, 07)*
19. **[19_ESP32_IO_IMPL.md](file:///home/ivan/Desktop/betaflight/logs/hardware_inspection/19_ESP32_IO_IMPL.md)**: Single Flat GPIO Port (`DEFIO_PORT_USED_COUNT == 1`), $O(1)$ Direct Array Index `&ioRecs[pinIdx]`. *(vs Log 08)*
20. **[20_ESP32_INTERRUPT_MODEL.md](file:///home/ivan/Desktop/betaflight/logs/hardware_inspection/20_ESP32_INTERRUPT_MODEL.md)**: Dynamic Peripheral Interrupt Routing Matrix vs Static NVIC Vector Table. *(vs Logs 03 & 04)*
21. **[21_ESP32_BUS_SPI_I2C.md](file:///home/ivan/Desktop/betaflight/logs/hardware_inspection/21_ESP32_BUS_SPI_I2C.md)**: GDMA Descriptors, Word Alignment Bounce Buffers, 64-byte Polled Bypass, I2C Command-list Coprocessor. *(vs Log 10)*
22. **[22_ESP32_PWM_DSHOT.md](file:///home/ivan/Desktop/betaflight/logs/hardware_inspection/22_ESP32_PWM_DSHOT.md)**: LEDC Fractional Clock Division, RMT Pulse-train Dedicated SRAM, Non-null VTable Safety. *(vs Log 09)*
23. **[23_ESP32_ADC.md](file:///home/ivan/Desktop/betaflight/logs/hardware_inspection/23_ESP32_ADC.md)**: On-demand Polled Oneshot ADC, Non-DMA Execution, Linear GPIO Mapping. *(vs Log 11)*
24. **[24_ESP32_DMA_AND_EXTI.md](file:///home/ivan/Desktop/betaflight/logs/hardware_inspection/24_ESP32_DMA_AND_EXTI.md)**: Centralized GPIO Interrupt with `__builtin_ctz` Dispatch, Dynamic CPU Interrupt Pool. *(vs Logs 08 & 10)*
25. **[25_ESP32_BUILD_AND_LINKER.md](file:///home/ivan/Desktop/betaflight/logs/hardware_inspection/25_ESP32_BUILD_AND_LINKER.md)**: RISC-V 32-bit RV32IM Toolchain Flags, Parameter Group Linker Anchors, Single-Allocator RAM. *(vs Logs 12 & 13)*

---

## 6. Upstream Submodule Documentation
* [betaflight.com/docs/development/App-Architecture.mdx](file:///home/ivan/Desktop/betaflight/knowledgebase/betaflight.com/docs/development/App-Architecture.mdx): Complete Vue 3 / Pinia / MSP client architecture of the Betaflight Configurator.
* [betaflight.com/docs/development/MSP-Protocol-Reference-Dev.md](file:///home/ivan/Desktop/betaflight/knowledgebase/betaflight.com/docs/development/MSP-Protocol-Reference-Dev.md): Comprehensive reference of all MSP v1 & v2 commands, offsets, and field structures.
* [betaflight.com/docs/development/Defines.md](file:///home/ivan/Desktop/betaflight/knowledgebase/betaflight.com/docs/development/Defines.md): Complete index of all `USE_` and `ENABLE_` feature flags used in Betaflight builds.
* [betaflight.com/docs/development/API/BusDrivers.md](file:///home/ivan/Desktop/betaflight/knowledgebase/betaflight.com/docs/development/API/BusDrivers.md): Specifications for bus-agnostic device access (`busReadRegister`, `busWriteRegister`, DMA segments).
