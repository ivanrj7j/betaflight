---
name: knowledge-search
description: >-
  Search, locate, and retrieve verified hardware specifications, register maps, comparative logs
  (STM32 vs ESP32 vs THEJAS32), post-mortem retrospective analyses, and codebase evaluations across
  the 50+ node Betaflight ARIES knowledge base. Use before proposing any architectural change,
  writing driver code, or answering questions about hardware constraints.
---

# Knowledge Search Skill: ARIES v3.0 Knowledge Base

This skill provides a fast, structured routing guide to search and navigate the 50+ node Betaflight ARIES knowledge base before making any code modifications or architectural decisions.

---

## 1. The Knowledge Base First Rule

> [!IMPORTANT]
> **RULE:** Never guess a register address, timing constant, pin assignment, or memory boundary. Every subsystem on the C-DAC THEJAS32 SoC has been analyzed, benchmarked, and evaluated in the knowledge base. Always consult the corresponding documents first.

### 1.1 Anti-Assumption Directive for Knowledge Gaps
> [!CAUTION]
> **STRICT PROHIBITION ON ASSUMPTIONS:**  
> If an item or subsystem is listed in [`knowledgebase/CRITICAL_KNOWLEDGE_GAPS_AND_ASSUMPTIONS.md`](../../../knowledgebase/CRITICAL_KNOWLEDGE_GAPS_AND_ASSUMPTIONS.md) as **[PENDING TEAM CONFIRMATION]** (e.g. physical IMU orientation, ESC protocol selection, ADC register verification), you **MUST NOT** guess or assume standard STM32/ESP32 behavior.
> - You **MUST** state the known silicon boundaries.
> - You **MUST** highlight the specific gap.
> - You **MUST** ask the user or wait for verified team laboratory confirmation before committing code.

### 1.2 Relative File Path Requirement
> [!IMPORTANT]
> **STRICT USE OF RELATIVE PATHS:**  
> When querying, citing, cross-referencing, or creating links to knowledge base documents and source files, always use POSIX relative file paths (e.g. `knowledgebase/INDEX.md` or `../../src/...`). Absolute paths (e.g. `/home/ivan/...` or `file:///home/...`) are strictly forbidden across all committed documentation and logs to guarantee portability across environments.

---

## 2. Master Navigation Index

* **Critical Knowledge Gaps & Assumptions:** [`knowledgebase/CRITICAL_KNOWLEDGE_GAPS_AND_ASSUMPTIONS.md`](../../../knowledgebase/CRITICAL_KNOWLEDGE_GAPS_AND_ASSUMPTIONS.md)  
  *Unverified hardware boundaries, pending team inputs, and strict anti-hallucination guardrails.*
* **Central Knowledge Index:** [`knowledgebase/INDEX.md`](../../../knowledgebase/INDEX.md)  
  *High-level directory cross-linking all hardware datasheets, inspection logs, retrospective analyses, and component nodes.*
* **Master Architectural Blueprint:** [`knowledgebase/PORTING_KNOWLEDGE_GRAPH.md`](../../../knowledgebase/PORTING_KNOWLEDGE_GRAPH.md)  
  *Component-by-component implementation architecture, Mermaid system graphs, and hardware verification checklists.*
* **Operational Directives:** [`AGENT.md`](../../../AGENT.md)  
  *Project ground truths, git branch rules (`rebuild` vs `master`), and safety checklists.*

---

## 3. Subsystem Query Routing Table

Use this table to quickly find the authoritative documents for specific subsystems:

| Subsystem / Topic | Primary Component Node | Hardware Inspection Logs | Retrospective & Evaluation Nodes | Target Source Files |
| :--- | :--- | :--- | :--- | :--- |
| **System Clocks & Timebase** | [`01_SYSTEM_AND_CLOCKS.md`](../../../knowledgebase/components/01_SYSTEM_AND_CLOCKS.md) | [01_STM32_COMMON_SYSTEM.md](../../../logs/hardware_inspection/01_STM32_COMMON_SYSTEM.md), [17_ESP32_SYSTEM_AND_TIMER.md](../../../logs/hardware_inspection/17_ESP32_SYSTEM_AND_TIMER.md) | [`04_RETROSPECTIVE_TEST_LOGS.md`](../../../knowledgebase/retrospective_logs/04_RETROSPECTIVE_TEST_LOGS.md), [`05_EVAL_SYSTEM_C.md`](../../../knowledgebase/codebase_evaluations/05_EVAL_SYSTEM_C.md) | [`system.c`](../../../src/platform/ARIES/system.c) |
| **Startup, Boot & Traps** | [`02_STARTUP_BOOT_AND_EXCEPTIONS.md`](../../../knowledgebase/components/02_STARTUP_BOOT_AND_EXCEPTIONS.md) | [02_STM32_SYSTEM_INIT.md](../../../logs/hardware_inspection/02_STM32_SYSTEM_INIT.md), [03_STM32_STARTUP_ASSEMBLY.md](../../../logs/hardware_inspection/03_STM32_STARTUP_ASSEMBLY.md) | [`01_RETROSPECTIVE_DEBUG_SESSION.md`](../../../knowledgebase/retrospective_logs/01_RETROSPECTIVE_DEBUG_SESSION.md), [`01_EVAL_STARTUP_S.md`](../../../knowledgebase/codebase_evaluations/01_EVAL_STARTUP_S.md) | [`startup.S`](../../../src/platform/ARIES/startup.S) |
| **Serial UART0 & MSP** | [`03_SERIAL_UART_AND_MSP.md`](../../../knowledgebase/components/03_SERIAL_UART_AND_MSP.md) | [05_STM32_SERIAL_UART_HW.md](../../../logs/hardware_inspection/05_STM32_SERIAL_UART_HW.md), [06_STM32_SERIAL_UART_DRIVER.md](../../../logs/hardware_inspection/06_STM32_SERIAL_UART_DRIVER.md), [15_MSP_SERIAL_PARSER.md](../../../logs/hardware_inspection/15_MSP_SERIAL_PARSER.md) | [`01_RETROSPECTIVE_DEBUG_SESSION.md`](../../../knowledgebase/retrospective_logs/01_RETROSPECTIVE_DEBUG_SESSION.md), [`06_EVAL_SERIAL_UART_ARIES_C.md`](../../../knowledgebase/codebase_evaluations/06_EVAL_SERIAL_UART_ARIES_C.md), [`15_EVAL_CORE_DIFFS_MAIN_CONFIG_PG_MSP.md`](../../../knowledgebase/codebase_evaluations/15_EVAL_CORE_DIFFS_MAIN_CONFIG_PG_MSP.md) | [`serial_uart_aries.c`](../../../src/platform/ARIES/serial_uart_aries.c), [`msp_serial.c`](../../../src/main/msp/msp_serial.c) |
| **GPIO Pin IO** | [`04_GPIO_AND_PIN_IO.md`](../../../knowledgebase/components/04_GPIO_AND_PIN_IO.md) | [08_STM32_IO_IMPL.md](../../../logs/hardware_inspection/08_STM32_IO_IMPL.md), [19_ESP32_IO_IMPL.md](../../../logs/hardware_inspection/19_ESP32_IO_IMPL.md) | [`07_EVAL_IO_ARIES_C_AND_DEF.md`](../../../knowledgebase/codebase_evaluations/07_EVAL_IO_ARIES_C_AND_DEF.md) | [`io_aries.c`](../../../src/platform/ARIES/io_aries.c) |
| **Motor PWM & Timer 1** | [`05_TIMER_AND_MOTOR_PWM.md`](../../../knowledgebase/components/05_TIMER_AND_MOTOR_PWM.md) | [09_STM32_TIMER_PWM_HW.md](../../../logs/hardware_inspection/09_STM32_TIMER_PWM_HW.md), [22_ESP32_PWM_DSHOT.md](../../../logs/hardware_inspection/22_ESP32_PWM_DSHOT.md) | [`02_RETROSPECTIVE_HAL_PROGRESS.md`](../../../knowledgebase/retrospective_logs/02_RETROSPECTIVE_HAL_PROGRESS.md), [`11_EVAL_PWM_MOTOR_ARIES_C.md`](../../../knowledgebase/codebase_evaluations/11_EVAL_PWM_MOTOR_ARIES_C.md) | [`pwm_motor_aries.c`](../../../src/platform/ARIES/pwm_motor_aries.c) |
| **Interrupts & EXTI** | [`06_INTERRUPTS_AND_EXTI.md`](../../../knowledgebase/components/06_INTERRUPTS_AND_EXTI.md) | [20_ESP32_INTERRUPT_MODEL.md](../../../logs/hardware_inspection/20_ESP32_INTERRUPT_MODEL.md), [24_ESP32_DMA_AND_EXTI.md](../../../logs/hardware_inspection/24_ESP32_DMA_AND_EXTI.md) | [`04_EVAL_PLATFORM_H.md`](../../../knowledgebase/codebase_evaluations/04_EVAL_PLATFORM_H.md), [`08_EVAL_EXTI_ARIES_C.md`](../../../knowledgebase/codebase_evaluations/08_EVAL_EXTI_ARIES_C.md) | [`exti_aries.c`](../../../src/platform/ARIES/exti_aries.c), [`platform.h`](../../../src/platform/ARIES/platform.h) |
| **SPI Bus Master (IMU)** | [`07_BUS_SPI_AND_I2C.md`](../../../knowledgebase/components/07_BUS_SPI_AND_I2C.md) | [10_STM32_BUS_SPI_HW.md](../../../logs/hardware_inspection/10_STM32_BUS_SPI_HW.md), [21_ESP32_BUS_SPI_I2C.md](../../../logs/hardware_inspection/21_ESP32_BUS_SPI_I2C.md) | [`09_EVAL_BUS_SPI_ARIES_C.md`](../../../knowledgebase/codebase_evaluations/09_EVAL_BUS_SPI_ARIES_C.md) | [`bus_spi_aries.c`](../../../src/platform/ARIES/bus_spi_aries.c) |
| **Parameter Persistence** | [`09_BUILD_LINKER_AND_MEMORY.md`](../../../knowledgebase/components/09_BUILD_LINKER_AND_MEMORY.md) | [12_STM32_LINKER_AND_SECTIONS.md](../../../logs/hardware_inspection/12_STM32_LINKER_AND_SECTIONS.md) | [`10_EVAL_CONFIG_FLASH_ARIES_C.md`](../../../knowledgebase/codebase_evaluations/10_EVAL_CONFIG_FLASH_ARIES_C.md), [`14_EVAL_TARGET_H_AND_CONFIG.md`](../../../knowledgebase/codebase_evaluations/14_EVAL_TARGET_H_AND_CONFIG.md) | [`config_flash_aries.c`](../../../src/platform/ARIES/config_flash_aries.c), [`target.h`](../../../src/platform/ARIES/target/ARIESV3/target.h) |
| **Linker, SRAM & Build** | [`09_BUILD_LINKER_AND_MEMORY.md`](../../../knowledgebase/components/09_BUILD_LINKER_AND_MEMORY.md) | [13_STM32_BUILD_MAKEFILE.md](../../../logs/hardware_inspection/13_STM32_BUILD_MAKEFILE.md), [25_ESP32_BUILD_AND_LINKER.md](../../../logs/hardware_inspection/25_ESP32_BUILD_AND_LINKER.md) | [`10_RETROSPECTIVE_RENODE_CHANGES.md`](../../../knowledgebase/retrospective_logs/10_RETROSPECTIVE_RENODE_CHANGES.md), [`02_EVAL_LINKER_ARIES_V3_LD.md`](../../../knowledgebase/codebase_evaluations/02_EVAL_LINKER_ARIES_V3_LD.md), [`13_EVAL_MAKEFILES_ARIES_MK_TARGET_MK.md`](../../../knowledgebase/codebase_evaluations/13_EVAL_MAKEFILES_ARIES_MK_TARGET_MK.md) | [`aries_v3.ld`](../../../src/platform/ARIES/linker/aries_v3.ld), [`ARIES.mk`](../../../src/platform/ARIES/mk/ARIES.mk) |
| **128 KB Footprint Pruning** | [`11_TARGET_OPTIMIZATION_BUDGET.md`](../../../knowledgebase/components/11_TARGET_OPTIMIZATION_BUDGET.md) | [TARGET_OPTIMIZATION_128KB.md](../../../knowledgebase/TARGET_OPTIMIZATION_128KB.md) | [`02_RETROSPECTIVE_HAL_PROGRESS.md`](../../../knowledgebase/retrospective_logs/02_RETROSPECTIVE_HAL_PROGRESS.md), [`14_EVAL_TARGET_H_AND_CONFIG.md`](../../../knowledgebase/codebase_evaluations/14_EVAL_TARGET_H_AND_CONFIG.md) | [`target.mk`](../../../src/platform/ARIES/target/ARIESV3/target.mk) |

---

## 4. Search Execution Workflow

When investigating an issue or planning a change:

1. **Identify Subsystem:** Match your task to the routing table above.
2. **Read Component Node:** Review the component blueprint to understand the architectural design.
3. **Read Codebase Evaluation Node:** Review what went wrong and what worked well in the corresponding `01..15_EVAL_*.md` file.
4. **Compare with C-DAC Reference Drivers:** Cross-check register structures against C-DAC reference files in [`lib/vega-sdk/vega-sdk/bsp/`](../../../lib/vega-sdk/vega-sdk/bsp).
5. **Proceed to Implementation:** Only after reviewing these verified records should you propose edits.
