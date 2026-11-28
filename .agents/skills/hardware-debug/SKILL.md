---
name: hardware-debug
description: >-
  Scientific hypothesis-driven debugging workflow for unexplained physical silicon failures on ARIES v3.0
  (THEJAS32 / VEGA ET1031). Automatically activate when physical hardware captures, serial/proxy logs,
  CLI freezes, MSP handshake timeouts, or runtime anomalies lack an experimentally proven root cause,
  before writing permanent unit tests and fixes.
---

# Hardware Debug Skill: Hypothesis-Driven Silicon Root-Cause Protocol

When physical ARIES v3.0 hardware fails in ways that host unit tests did not catch (or failures persist despite passing unit tests), **guesswork fixes are strictly forbidden**. Agents MUST confirm the exact root cause on physical silicon first, then return to the normal TDD development protocol.

---

## 1. Mandatory Activation Triggers

Activate **`hardware-debug`** automatically whenever:
* Bench/proxy/serial captures (`tempfiles/*.txt`, `COM9`) exhibit freezes, dropped bytes, MSP/Configurator handshake timeouts, unexpected traps (`mcause`), or non-deterministic lockups.
* A bug reproduces on physical silicon without an experimentally confirmed root cause.
* Multiple competing hardware or software explanations exist (e.g., ISR latency vs. FIFO overrun vs. pin noise vs. clock tolerance vs. peripheral lockup).

---

## 2. The 5-Step Hypothesis Verification Workflow

### Step D1: Forensic Timeline & Knowledge Inspection (`knowledge-search`)
1. Read the complete physical log capture(s) (`tempfiles/`, `logs/dev/`) and compare passing vs. failing runs byte by byte.
2. Inspect active driver/protocol source (`src/platform/ARIES/`, `src/main/`), recent `rebuild` commits/diffs, open issues in [`knowledgebase/CRITICAL_KNOWLEDGE_GAPS_AND_ASSUMPTIONS.md`](../../../knowledgebase/CRITICAL_KNOWLEDGE_GAPS_AND_ASSUMPTIONS.md) and [`knowledgebase/code_audit/`](../../../knowledgebase/code_audit), plus VEGA SDK, STM32, and ESP32 references.
3. Produce an exact **event timeline** (timestamps, wire bytes, state transitions, errors observed vs. absent) before proposing any firmware changes.

### Step D2: Formulate Ranked Testable Hypotheses
Define concrete, falsifiable hypotheses (`H1`, `H2`, `H3`, ...) specifying for each:
* **Mechanism & Location:** Exact file, function, register (`LSR`, `IIR`, `FCR`, `INTR_STATUS`, etc.), or timing condition.
* **Discriminating Prediction:** The exact diagnostic counter, register bit, LED state, or UART/proxy observation that **proves** `H_k` true versus **rules it out** on hardware.

Present the hypotheses and proposed probe experiment to the user concisely.

### Step D3: Minimal, Non-Destructive Diagnostic Probe Build
1. Design the **smallest reversible probe** that cleanly separates the hypotheses:
   * **Protocol Wire Safety:** Never blindly inject ASCII debug strings into the middle of an active binary MSP (`$M<` / `$M>`) or CRSF stream on `UART0`/`UART1`. Use in-SRAM flight-recorder rings/counters dumped safely (on idle, timeout, CLI entry, or dedicated diagnostic path) unless running a standalone diagnostic harness.
   * Gate diagnostic telemetry cleanly (e.g., `ARIES_DEV_LOGGING > 0`) whenever practical.
2. **Silicon Ground Truths Still Apply 100%:** Even temporary diagnostic probe builds MUST:
   * Occur strictly on branch `rebuild` (never `master`).
   * Pass the RVC opcode check (**strictly `0`** `c.` compressed instructions).
   * Fit inside **$\le 248\text{ KB}$** SRAM starting at `0x00202000`.
   * Copy the resulting `.bin` to `tempfiles/`.
3. **Diagnostic Build Briefing & User Wait-Gate:**
   * Provide binary path, exact size, `0x00202000` origin, `0` RVC confirmation, the hypotheses under test, exact trigger steps (e.g., proxy + Configurator sequence), and what output confirms vs. refutes each hypothesis.
   * **Stop and wait** for the user to flash manually (VM constraint: never flash directly) and supply the captured logs.

### Step D4: Evaluate Hardware Output Until Confirmed
1. Parse the returned capture (`tempfiles/*.txt`).
2. Explicitly classify every hypothesis against observed bytes/counters/registers as **`CONFIRMED`**, **`REFUTED`**, or **`INCONCLUSIVE`**.
3. If inconclusive or refuted, refine the hypotheses and iterate Step D2–D4 until the exact root cause is **experimentally proven on silicon**.

### Step D5: Transition Back to Normal TDD Protocol (`DEVELOPMENT_GUIDE.md`)
Once the silicon failure mechanism is **confirmed**:
1. Report the proven root cause clearly to the user and discard/clean any temporary diagnostic probe hacks (retaining useful clean `ARIES_DEV_LOGGING` telemetry where appropriate).
2. Execute the full normal TDD lifecycle in [`DEVELOPMENT_GUIDE.md`](../../../DEVELOPMENT_GUIDE.md):
   * **TDD Step 1:** Author deep AAA unit tests reproducing the **confirmed** failure mode and edge cases.
   * **TDD Step 2:** Present tests to user and wait for explicit approval.
   * **TDD Step 3:** Complete Mandatory Triple-Reference Consultation (**VEGA SDK + STM32 + ESP32** via `knowledge-search`) and implement the clean production fix.
   * **TDD Step 4:** Run host unit tests, compile `TARGET=ARIESV3`, verify `0` RVC & $\le 248\text{ KB}$ audit, copy `.bin` to `tempfiles/`, and deliver the Build Briefing for final hardware confirmation.
   * **TDD Step 5 (`knowledge-update`):** Document the proven root cause, probe evidence, TDD rationale, and `0–10` rubric in `logs/dev/` / `knowledgebase/code_audit/`, and synchronize [`knowledgebase/INDEX.md`](../../../knowledgebase/INDEX.md) and [`knowledgebase/PORTING_KNOWLEDGE_GRAPH.md`](../../../knowledgebase/PORTING_KNOWLEDGE_GRAPH.md).
