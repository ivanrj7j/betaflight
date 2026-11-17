# Knowledge Evaluation Node: `src/platform/ARIES/startup.S`
**Evaluated Log**: [`logs/code_critique/01_STARTUP_S.md`](file:///home/ivan/Desktop/betaflight/logs/code_critique/01_STARTUP_S.md)  
**Target Code**: [`src/platform/ARIES/startup.S`](file:///home/ivan/Desktop/betaflight/src/platform/ARIES/startup.S)  
**Reference Code**: [`lib/vega-sdk/vega-sdk/bsp/common/crt.S`](file:///home/ivan/Desktop/betaflight/lib/vega-sdk/vega-sdk/bsp/common/crt.S)  
**Component**: Reset Sequence, GPR Zeroing, C Runtime Setup, and Trap Entry.

---

## 1. Context & Architectural Role
`startup.S` is the very first code executed when the THEJAS32 core resets. It sets up all 32 General Purpose Registers, loads `sp` from `_estack`, anchors the global pointer `gp`, sets `mtvec` to `trap_entry`, clears `.bss`, conditionally copies `.data`, and jumps to `main()`. It also contains `trap_entry` which saves all core registers and calls `handle_trap`.

---

## 2. Decision & Action Scoring (0 to 10)

| Decision / Action Taken | Score (0-10) | Category | Technical Analysis & Rationale | References |
| :--- | :---: | :---: | :--- | :--- |
| **In-SRAM Data Copy Bypass (`beq t0, t1, copy_done`)** | **9.5 / 10** | **Brilliant** | When executing in SRAM (LMA == VMA), copying `.data` over itself is redundant and dangerous. Bypassing the loop preserves pre-initialized memory and speeds up boot time. | [`startup.S#L74`](file:///home/ivan/Desktop/betaflight/src/platform/ARIES/startup.S#L74) |
| **Trimming Trap Frame to 128 Bytes ($32 \times 4$)** | **9.0 / 10** | **Brilliant** | Official C-DAC `crt.S` allocated 272 bytes per trap entry (sized for RV64 and FPU registers). Reducing this to exactly 128 bytes (16-byte aligned for RV32) saves 144 bytes per trap frame on an 8 KB stack. | [`startup.S#L111`](file:///home/ivan/Desktop/betaflight/src/platform/ARIES/startup.S#L111) vs [`crt.S#L162`](file:///home/ivan/Desktop/betaflight/lib/vega-sdk/vega-sdk/bsp/common/crt.S#L162) |
| **Complete 32-GPR Zeroing on Boot** | **9.0 / 10** | **Good** | Iteratively clearing `x1`..`x31` prevents uninitialized register leakage, eliminating simulator warnings and undefined register states. | [`startup.S#L25-L55`](file:///home/ivan/Desktop/betaflight/src/platform/ARIES/startup.S#L25-L55) |
| **Direct Single-Core `_estack` Assignment** | **8.5 / 10** | **Good** | Replaced C-DAC's complex multi-core `mhartid` stack shifting logic with a clean direct load (`la sp, _estack`), saving code size on single-core THEJAS32. | [`startup.S#L58`](file:///home/ivan/Desktop/betaflight/src/platform/ARIES/startup.S#L58) vs [`crt.S#L154`](file:///home/ivan/Desktop/betaflight/lib/vega-sdk/vega-sdk/bsp/common/crt.S#L154) |
| **Omitting Pre-Trap `sp` (`x2`) from Trap Frame** | **3.0 / 10** | **Flawed** | Decrementing `sp` by 128 and saving all registers except `x2` (`sp`) left the caller's stack pointer uninitialized in the frame. Exception handlers reading `frame[2]` received stale garbage. | [`startup.S#L113-L114`](file:///home/ivan/Desktop/betaflight/src/platform/ARIES/startup.S#L113-L114) |
| **Assuming 4-Byte Word Alignment Without Assertions in Assembly** | **5.0 / 10** | **Mediocre** | Stepping loops by 4 bytes without testing alignment bits relies entirely on the linker script enforcing 4-byte boundaries on `_edata` and `_ebss`. | [`startup.S#L79-L80`](file:///home/ivan/Desktop/betaflight/src/platform/ARIES/startup.S#L79-L80) |

---

## 3. Deep-Dive Analysis

### 🌟 What Were the Good / Brilliant Parts?
1. **Memory Copy Optimization**: Checking if load memory address (LMA) equals virtual memory address (VMA) (`beq t0, t1, copy_done`) is an elegant optimization for in-SRAM firmware images. It allows the same startup routine to support both ROM-to-RAM loading and direct SRAM execution without conditional compilation.
2. **Deterministic ABI Setup**: Setting up `gp` with `.option push; .option norelax; la gp, __global_pointer$; .option pop` guarantees that linker relaxations can safely convert 32-bit absolute address calculations into single-instruction 12-bit signed offsets relative to `gp`.

### ⚠️ What Went Wrong?
1. **Unsaved Stack Pointer (`x2`) in Exception Context**:
   In `trap_entry`, the code decrements `sp` by 128 bytes, saves `x1`, skips `x2` with a comment `/* sp saved later if needed */`, and saves `x3`..`x31`. When `system.c`'s `handle_trap()` receives `sp` as argument `a2`, `frame[2]` contains whatever random data was left in memory at that offset. During hard faults, the crash telemetry prints an erroneous stack pointer.

### 💡 What Could Have Been Done Better?
- **Pre-trap SP Calculation**: The original pre-trap `sp` should have been computed into a temporary register (e.g. `addi t0, sp, 128`) and stored directly into `2*REGBYTES(sp)`.
- **Trap Cause Fast Path**: External interrupts (cause 11) could have been checked in assembly before saving all 32 registers, saving ~60 clock cycles for high-rate timer interrupts.

---

## 4. Key Takeaways & Overall Score: 7.5 / 10
`startup.S` provides an efficient, stripped-down runtime initialization well-adapted to single-core THEJAS32. Hardening the trap frame stack pointer slot completes its robustness.
