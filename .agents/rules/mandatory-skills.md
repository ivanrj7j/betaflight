# Mandatory Skills & Code Accuracy Directive

Every agent operating in this repository must automatically adhere to the following operational laws:

## 1. Automatic Skill Activation Matrix

Every agent operating in this repository must automatically activate each skill whenever its trigger condition occurs, without waiting for explicit user prompts:

| Skill | Mandatory Trigger Condition | Required Action |
| :--- | :--- | :--- |
| **`project-onboarding`** | • Session start<br>• Starting a new development phase or task<br>• Checking toolchain setup or git branch | Read [`.agents/skills/project-onboarding/SKILL.md`](../skills/project-onboarding/SKILL.md); verify active branch is `rebuild`; review 8 silicon ground truths. |
| **`knowledge-search`** | • Planning or reviewing architecture<br>• Authoring unit tests (TDD Step 1)<br>• Proposing or editing build flags/Makefiles<br>• **Before writing code in development (TDD Step 3)** | Read [`.agents/skills/knowledge-search/SKILL.md`](../skills/knowledge-search/SKILL.md); search `knowledgebase/`; cross-check VEGA SDK, STM32, and ESP32 reference implementations before writing code. |
| **`hardware-debug`** | • Unexplained physical silicon failure or timeout<br>• Persistent bug not caught by unit tests<br>• Analyzing failing `COM9`/proxy captures | Read [`.agents/skills/hardware-debug/SKILL.md`](../skills/hardware-debug/SKILL.md); build timeline, rank testable hypotheses, verify via minimal silicon probe build, then enter normal TDD once root cause is proven. |
| **`knowledge-update`** | • Completing a development phase<br>• Concluding hardware inspection or bench test<br>• Auditing drivers or discovering bugs/fixes<br>• Creating retrospective or evaluation notes | Read [`.agents/skills/knowledge-update/SKILL.md`](../skills/knowledge-update/SKILL.md); document using 4-part 0–10 scoring template; synchronize `INDEX.md` and `PORTING_KNOWLEDGE_GRAPH.md`. |

### Specific Skill Mandates
1. **`project-onboarding`**: Required at the start of any session. Ensures the agent is on the `rebuild` branch and aware of the 248 KB SRAM boot and RV32IM constraints.
2. **`knowledge-search`**: **MANDATORY BEFORE ANY CODE MODIFICATION.** Every driver, register address, clock divisor, and pin mapping must be cross-checked against the 50+ node knowledge base (`knowledgebase/INDEX.md`, `knowledgebase/PORTING_KNOWLEDGE_GRAPH.md`, and `knowledgebase/codebase_evaluations/`). No guesswork is permitted. During development, refer to VEGA SDK, STM32 implementation, and ESP32 implementation before writing actual code. **ANTI-ASSUMPTION RULE:** Consult [`knowledgebase/CRITICAL_KNOWLEDGE_GAPS_AND_ASSUMPTIONS.md`](../../knowledgebase/CRITICAL_KNOWLEDGE_GAPS_AND_ASSUMPTIONS.md); never guess hardware settings marked as [PENDING TEAM CONFIRMATION].
3. **`hardware-debug`**: Required whenever physical hardware logs show an unproven bug. Inspect code/logs, formulate ranked hypotheses, build a minimal wire-safe diagnostic probe (`0` RVC, $\le 248\text{ KB}$, copy to `tempfiles/`) for the user to flash and capture, confirm the root cause on silicon first, then return to normal TDD (`DEVELOPMENT_GUIDE.md`) to write tests and the permanent fix.
4. **`knowledge-update`**: Required whenever evaluating code, auditing drivers, or running bench tests. Demands a 0–10 score for every decision and synchronization of `INDEX.md` and `PORTING_KNOWLEDGE_GRAPH.md`.

## 2. Silicon Constraints (THEJAS32 / VEGA ET1031)
* RV32IM only: 0 compressed 16-bit instructions (`c.`).
* SRAM execution starting at `0x00202000` (max 248 KB, no external flash XIP).
* `#define CONFIG_IN_RAM` with `EEPROM_SIZE 8192` (never 4096).
* 1.0 kHz flight loop clamping (`DEFAULT_PID_PROCESS_DENOM 8`).
* Timer 1 (IRQ 8) soft PWM ascending deadline event queue.
* CSR `mstatus.MIE` atomic sections (no empty `BASEPRI` stubs).
* SPI0 clock clamped to 12.5 MHz for ICM-42688P.
* Branch `rebuild` is the only active working branch. `master` contains discarded code.

## 3. Strict Relative File Path Law
* All markdown documentation, test logs (`logs/dev/`), evaluations, planning files, and code references MUST strictly use POSIX relative file paths.
* Hardcoded absolute filesystem paths (such as `/home/ivan/...` or `file:///home/...`) are strictly forbidden across all branches and commits to guarantee repository portability across machines, CI/CD runners, and container environments.
