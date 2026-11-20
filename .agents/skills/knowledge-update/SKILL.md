---
name: knowledge-update
description: >-
  Update, document, and synchronize findings, code critiques, hardware test outcomes, and technical
  decisions into the Betaflight ARIES knowledge base. Automatically activate whenever completing a development phase,
  auditing drivers, running bench/hardware tests, discovering new hardware bugs, or updating knowledge graphs/indexes.
---

# Knowledge Update Skill: Knowledge Base Synchronization

This skill governs the protocol for documenting engineering analyses, code critiques, hardware test outcomes, and architectural changes in the Betaflight ARIES knowledge base.

---

## 1. Core Principles & Strict Operational Rules

1. **Read-Then-Write Discipline:**
   - Always read the target file or source log first before creating or updating an evaluation node.
   - Do NOT batch read and batch write. Process one node at a time to ensure high technical depth.
2. **0 to 10 Decision Scoring Rubric:**
   - Every architectural choice, hardware assumption, and implementation decision must be scored from 0 to 10 with clear technical justification.
3. **Hyperlink Integrity & Mandatory Relative File Paths:**
   - All referenced files inside repository markdown files and documentation MUST use POSIX relative paths with line numbers where appropriate: `[basename](relative/path/to/file#L10-L20)`.
   - Absolute filesystem paths (such as `/home/ivan/...` or `file:///home/...`) are strictly forbidden across all committed documentation, logs, evaluations, and code comments to guarantee portability across developer workstations, containers, and CI runners.
   - When creating or updating documents in subdirectories (e.g. `knowledgebase/codebase_evaluations/`), calculate the path relative to that document's directory (e.g. `../../src/platform/ARIES/system.c` or `../../logs/code_critique/05_SYSTEM_C.md`).

---

## 2. Mandatory Evaluation Node Document Template

When evaluating a driver, subsystem, or test outcome, create a new file in `knowledgebase/codebase_evaluations/` or `knowledgebase/retrospective_logs/` adhering to this exact 4-part structure:

```markdown
# Evaluation Node XX: Subsystem Name (`filename.c`)

## 1. Executive Summary & File Traceability
- **Evaluated File**: [`src/platform/ARIES/...`](../../src/platform/ARIES/...)
- **Source Log / Test Record**: [`logs/...`](../../logs/...)
- **Upstream / Reference Drivers**: [`lib/vega-sdk/...`](../../lib/vega-sdk/...)
- **Domain**: Brief functional summary.

---

## 2. Decision & Action Scoring Rubric (0 to 10)

| Decision / Action | Score (0-10) | Evaluation Rationale |
| :--- | :---: | :--- |
| **Decision 1** | X/10 | Technical rationale... |
| **Decision 2** | Y/10 | Technical rationale... |

---

## 3. Deep-Dive Critique

### What Went Wrong
1. **Specific Vulnerability / Bug**:
   - Detailed code snippet, CPU trap mode (`mcause`), or timing race condition.

### What Could Have Been Done Better
1. **Remediation & Alternative**:
   - Better register patterns, state-preserving atomics, or compiler built-ins.

### What Were the Good / Brilliant Parts
1. **Engineering Breakthrough**:
   - Memory optimization, clever hardware reuse, or latency savings.
```

---

## 3. Mandatory Phase Development Log Protocol (`logs/dev/phase_XX_<name>.md`)

> [!IMPORTANT]
> **DEDICATED LOG FILE FOR EVERY PHASE:**  
> Upon concluding any development phase (Phases 01 through 20 from [`PLAN/20_DEVELOPMENT_PHASES.md`](../../../PLAN/20_DEVELOPMENT_PHASES.md)), the agent **MUST** author a dedicated phase verification document in `logs/dev/phase_XX_<name>.md` (and companion `.log`).  
> Batching multiple phases into a single log file or omitting the detailed rationale sections is strictly forbidden.

### Required 7-Section Structure for Every Phase Dev Log:

1. **Header & Traceability**:
   - Phase ID, Date/Timestamp, Git commit hash, Active branch (`rebuild`), Target silicon (THEJAS32 RV32IM @ 100 MHz).
   - Relative links to corresponding `PLAN/` micro-plan and knowledgebase evaluation nodes.
2. **High-Level Executive Overview**:
   - Clear summary of the phase objective, silicon necessity, and architectural context.
3. **Unit Test Design Rationales (Living Documentation)**:
   - Exhaustive documentation of **every rational decision** made when authoring the unit tests.
   - For every test case: state the condition under test, the Arrange-Act-Assert (AAA) layout, the expected outcome, and the **real-world flight & silicon failure consequence** if the test were to fail on physical hardware.
4. **Code Implementation Rationales**:
   - Exhaustive documentation of **every rational decision** made when writing C code, assembly, or Makefiles.
   - Detail the register selections, base addresses, bitmasks, compiler/linker flags, memory layout, and how the 8 Silicon Ground Truths are enforced.
5. **Expected vs. Actual Outputs**:
   - Explicit commands executed, expected stdout/metrics, actual captured outputs, and variance analysis (test execution time, memory footprint, objdump disassembly).
6. **10-Point Silicon Invariant Audit Results**:
   - Full 10-point audit table (branch, sizing $\le 248\text{ KB}$, entry point `0x00202000`, RVC opcode count `grep c. == 0`, soft-float double precision check, trap alignment, stack bounds, parameter group bounds, etc.).
7. **Retrospective & 0 to 10 Decision Scoring Rubric**:
   - Score every architectural and implementation decision from 0 to 10 with technical justifications.
   - 3-part deep-dive critique: What went wrong previously, What could have been done better, and What were the brilliant engineering insights.

---

## 4. Knowledge Base Synchronization Workflow

After creating an evaluation document or phase dev log:

### Step 1: Update the Central Index (`knowledgebase/INDEX.md`)
1. Open [`knowledgebase/INDEX.md`](../../../knowledgebase/INDEX.md).
2. If updating an evaluation node, insert into Section 3 (*Codebase Evaluations Catalog*).
3. If concluding a development phase, insert into Section 8 (*Active Development Phase Verification Logs*):
   - Phase number and link to `logs/dev/phase_XX_<name>.log` or `.md`.
   - Core verification focus.
   - Silicon status and outcome summary.

### Step 2: Update the Master Knowledge Graph (`knowledgebase/PORTING_KNOWLEDGE_GRAPH.md`)
1. Open [`knowledgebase/PORTING_KNOWLEDGE_GRAPH.md`](../../../knowledgebase/PORTING_KNOWLEDGE_GRAPH.md).
2. Update the Mermaid cross-reference diagram:
   - Add the node ID (e.g. `EXX["XX_Driver_Name"]`).
   - Add connection edges to relevant target modules (e.g. `EXX --> M4`).
3. Add a row to the *Evaluation Matrix & Critical Scoring* table in Section 4.

---

## 5. Pre-Sync Verification Checklist

Before completing an update:
- [ ] Every phase has its own separate file in `logs/dev/phase_XX_<name>.md`.
- [ ] Log includes High-Level Overview, Unit Test Rationales, Code Implementation Rationales, Expected vs Actual Outputs, and 10-point audit.
- [ ] Every decision has an explicit score (0–10).
- [ ] Analysis covers: What went wrong, What could have been done better, and Good parts.
- [ ] All file links are formatted as strictly relative paths (0 absolute `/home/...` or `file:///...` links).
- [ ] [`INDEX.md`](../../../knowledgebase/INDEX.md) and [`PORTING_KNOWLEDGE_GRAPH.md`](../../../knowledgebase/PORTING_KNOWLEDGE_GRAPH.md) are both synchronized.
