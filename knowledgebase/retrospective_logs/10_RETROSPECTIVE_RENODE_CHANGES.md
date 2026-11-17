# Retrospective Log 10: Renode Changes & The Simulation Trap

> **Source File:** [`oldfiles/logs/RENODE_CHANGES.md`](file:///home/ivan/Desktop/betaflight/oldfiles/logs/RENODE_CHANGES.md)  
> **Target Platform:** C-DAC THEJAS32 SoC (VEGA ET1031 RV32IM @ 100 MHz, 256 KB SRAM, No XIP)  
> **Analysis Date:** 2026-11-17  
> **Status:** Completed

---

## 1. Executive Summary & Purpose

`RENODE_CHANGES.md` documents early boot investigation notes from 2026-10-19 when the team first introduced bare-metal startup assembly:
1. Implemented custom `startup.S` (stack initialization, `.data` copy, `.bss` zeroing, jump to `main`).
2. Added `-nostartfiles` to `ARIES.mk` to bypass toolchain `crt0.o`.
3. Updated `aries_v3.ld` with `ENTRY(_start)`, 16 KB stack, and `__global_pointer$`.
4. Documented an early Renode simulation abort on opcode `0x433d`.
5. Outlined step-by-step rollback instructions.

---

## 2. Decision & Action Scoring Matrix (0 to 10)

| ID | Action / Topic | Decision / Action Description | Category | Score (0–10) | Justification & Technical Impact |
| :--- | :--- | :--- | :--- | :---: | :--- |
| **D01** | Startup Assembly (§1) | Creating minimal `startup.S` with GPR clear, `.data` copy, and `.bss` clear | Bare-Metal Runtime | **9.0 / 10** | **Essential Clean Bring-up.** Standard bare-metal startup assembly replacing vendor bloat and establishing predictable stack/data boundaries. |
| **D02** | Compiler Flags (§2) | Adding `-nostartfiles` to `LD_FLAGS` in `ARIES.mk` | Build System | **9.0 / 10** | **Clean Entry Isolation.** Bypassed default `crt0.o` entry points, allowing Betaflight's custom `_start` to take complete control of hardware reset. |
| **D03** | Linker Entry (§3) | Setting `ENTRY(_start)` and defining `__global_pointer$` at `_sdata + 0x800` | Linker Engineering | **8.5 / 10** | **Standard RISC-V ABI.** Allowed GP-relative 12-bit signed addressing for small data (`.sdata`/`.sbss`), optimizing variable access latency. |
| **D04** | Simulation Tweak (§4) | Enabling `rv32im_zicsr_c` in Renode to bypass illegal instruction `0x433d` | Simulation Integrity | **0.5 / 10** | **The Legendary Simulation Blunder.** When Renode aborted on opcode `0x433d` (a 16-bit compressed instruction), the author modified the simulator configuration to support the 'C' extension instead of investigating why the compiler generated compressed opcodes for an RV32IM core! This masked the toolchain multilib RVC bug for 26 days. |
| **D05** | Memory Map Target (§4) | Linking and testing PC entry at `0x20000000` | Memory Model | **1.5 / 10** | **Simulating Fiction.** Configured Renode to jump to `0x20000000` (XIP Flash), which does not exist on physical THEJAS32 silicon. |
| **D06** | Rollback Docs (§5) | Providing clean, step-by-step rollback instructions | Documentation Hygiene | **8.5 / 10** | **Responsible Change Control.** Allowed teammates to cleanly revert changes if the branch broke. |

---

## 3. Deep-Dive Post-Mortem: The "Modifying the Simulator to Fit Buggy Code" Trap

### 3.1 What Happened in Section 4?
Lines 29–32 of `RENODE_CHANGES.md` state:
> *"Ran a Renode script to load the rebuilt ELF, set the PC to `0x20000000`, and read a few stack locations.  
> **Result:** The simulation aborted because the CPU model had the RVC (compressed-instruction) extension disabled, leading to an illegal instruction (`0x433d`).  
> **Implication:** For a full validation we need to enable the `rv32im_zicsr_c` ISA (or at least `c`) in the Renode platform description."*

### 3.2 The Flawed Reasoning (Score: 0.5/10)
- **The Core Reality:**
  - The C-DAC THEJAS32 SoC (VEGA ET1031) is **strictly RV32IM**.
  - It does **not** have the 'C' (compressed instructions) extension in silicon.
- **The Warning:**
  - When Renode faulted on opcode `0x433d`, Renode was doing **exactly what it was supposed to do**: accurately emulating the physical RV32IM core and alerting the engineer that invalid instructions were in the binary!
- **The Fatal Reaction:**
  - Instead of diagnosing: *"Wait, why did my compiler output a 16-bit compressed instruction (`0x433d`) when my target is RV32IM?"*
  - The engineer concluded: *"Renode's CPU model is missing the 'C' extension; let me edit `ariesv3.repl` to enable `rv32im_zicsr_c`."*
- **The Consequence:**
  - By enabling 'C' in Renode, the simulator happily executed compressed instructions from `libc.a` without complaining.
  - The team operated for nearly a month believing their code was 100% verified.
  - On 2026-11-14, when the firmware was flashed to physical ARIES v3 silicon, the real ET1031 core encountered compressed instructions in `memset()` and immediately entered a continuous illegal instruction trap reboot loop (Hurdle H13).

---

## 4. Key Rules for Our 128 KB SRAM Build

1. **Rule 1: Never Modify the Simulator / Test Double to Match Code Errors:**
   - If a simulator or test double throws an illegal instruction exception, **always audit the compiler and disassembly first**. Never change hardware configuration files to silence build errors.
2. **Rule 2: RV32IM Strictly Means Zero Compressed Instructions:**
   - Verify all binaries with:
     ```bash
     riscv32-esp-elf-objdump -d obj/main/betaflight_ARIESV3.elf | grep -E '\b(c\.[a-z]+)'
     ```
   - Count must be strictly **0**.
3. **Rule 3: Ensure Clean Entry Points:**
   - Retain `-nostartfiles` and custom startup assembly, linking directly to SRAM at `0x00202000`.

---
*Retrospective log compiled and verified against historical `oldfiles/logs/RENODE_CHANGES.md` records.*
