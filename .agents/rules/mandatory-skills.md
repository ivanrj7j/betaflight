# Mandatory Skills & Code Accuracy Directive

Every agent operating in this repository must automatically adhere to the following operational laws:

## 1. Always-Loaded Skills
1. **`project-onboarding`**: Required at the start of any session. Ensures the agent is on the `rebuild` branch and aware of the 248 KB SRAM boot and RV32IM constraints.
2. **`knowledge-search`**: **MANDATORY BEFORE ANY CODE MODIFICATION.** Every driver, register address, clock divisor, and pin mapping must be cross-checked against the 50+ node knowledge base (`knowledgebase/INDEX.md`, `knowledgebase/PORTING_KNOWLEDGE_GRAPH.md`, and `knowledgebase/codebase_evaluations/`). No guesswork is permitted.
3. **`knowledge-update`**: Required whenever evaluating code, auditing drivers, or running bench tests. Demands a 0–10 score for every decision and synchronization of `INDEX.md` and `PORTING_KNOWLEDGE_GRAPH.md`.

## 2. Silicon Constraints (THEJAS32 / VEGA ET1031)
* RV32IM only: 0 compressed 16-bit instructions (`c.`).
* SRAM execution starting at `0x00202000` (max 248 KB, no external flash XIP).
* `#define CONFIG_IN_RAM` with `EEPROM_SIZE 8192` (never 4096).
* 1.0 kHz flight loop clamping (`DEFAULT_PID_PROCESS_DENOM 8`).
* Timer 1 (IRQ 8) soft PWM ascending deadline event queue.
* CSR `mstatus.MIE` atomic sections (no empty `BASEPRI` stubs).
* SPI0 clock clamped to 12.5 MHz for ICM-42688P.
* Branch `rebuild` is the only active working branch. `master` contains discarded code.
