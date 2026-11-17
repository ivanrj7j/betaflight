---
name: knowledge-update
description: >-
  Update, document, and synchronize findings, code critiques, hardware test outcomes, and technical
  decisions into the Betaflight ARIES knowledge base. Use whenever new drivers are audited, bench
  tests are conducted, code critiques are written, or knowledge graph/index updates are required.
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

## 3. Knowledge Base Synchronization Workflow

After creating an evaluation or retrospective document:

### Step 1: Update the Central Index (`knowledgebase/INDEX.md`)
1. Open [`knowledgebase/INDEX.md`](../../../knowledgebase/INDEX.md).
2. Locate the appropriate subsection under Section 3 (e.g. *Codebase Evaluations Catalog*).
3. Insert a new row containing:
   - Link to the evaluation document.
   - Target source file.
   - Source critique log link.
   - Decision scoring summary.
   - Core takeaways.

### Step 2: Update the Master Knowledge Graph (`knowledgebase/PORTING_KNOWLEDGE_GRAPH.md`)
1. Open [`knowledgebase/PORTING_KNOWLEDGE_GRAPH.md`](../../../knowledgebase/PORTING_KNOWLEDGE_GRAPH.md).
2. Update the Mermaid cross-reference diagram:
   - Add the node ID (e.g. `EXX["XX_Driver_Name"]`).
   - Add connection edges to relevant target modules (e.g. `EXX --> M4`).
3. Add a row to the *Evaluation Matrix & Critical Scoring* table in Section 4.

---

## 4. Pre-Sync Verification Checklist

Before completing an update:
- [ ] Every decision has an explicit score (0–10).
- [ ] Analysis covers: What went wrong, What could have been done better, and Good parts.
- [ ] All file links are formatted as strictly relative paths (0 absolute `/home/...` or `file:///...` links).
- [ ] [`INDEX.md`](../../../knowledgebase/INDEX.md) and [`PORTING_KNOWLEDGE_GRAPH.md`](../../../knowledgebase/PORTING_KNOWLEDGE_GRAPH.md) are both synchronized.
