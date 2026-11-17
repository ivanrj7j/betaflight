# Code Critique: `src/platform/ARIES/startup.S` vs `lib/vega-sdk/vega-sdk/bsp/common/crt.S`

## 1. File Metadata & Overview
- **Source File**: [`src/platform/ARIES/startup.S`](../../src/platform/ARIES/startup.S)
- **Reference File**: [`lib/vega-sdk/vega-sdk/bsp/common/crt.S`](../../lib/vega-sdk/vega-sdk/bsp/common/crt.S)
- **Component**: Reset sequence, GPR initialization, C runtime setup, and Machine Trap Entry (`trap_entry`).

---

## 2. Structural & Architectural Comparison

| Feature | Reference (`crt.S`) | Target (`startup.S`) | Assessment |
| :--- | :--- | :--- | :--- |
| **GPR Clearing** | Iterative `li x1, 0` .. `li x31, 0` | Iterative `li x1, 0` .. `li x31, 0` | **Exact match**. Essential on RISC-V to avoid uninitialized register state triggering simulator/hardware warnings. |
| **XLEN Assertion** | Tests bit 31 sign extension to trap XLEN mismatch | Omitted | **Acceptable**. Build system strictly enforces `-march=rv32im_zicsr -mabi=ilp32`. |
| **FPU Initialization** | `#ifdef __riscv_flen` clears FPU regs and status | Fully stripped | **Correct**. THEJAS32 (VEGA ET1031) is strictly RV32IM without FPU. Executing FPU CSR/register instructions causes an illegal instruction exception (`mcause = 2`). |
| **Stack Pointer (`sp`)** | Multicore shift calculation from `mhartid` | `la sp, _estack` | **Better for THEJAS32**. THEJAS32 is single-core; hardcoded linker symbol `_estack` prevents memory waste. |
| **Global Pointer (`gp`)** | `.option norelax; la gp, __global_pointer$` | `.option norelax; la gp, __global_pointer$` | **Exact match**. Standard RISC-V ABI linker relaxation anchor. |
| **Trap Vector (`mtvec`)** | Direct mode to `trap_entry` | Direct mode to `trap_entry` | **Matches**. Direct mode (`mtvec[1:0] == 00`). |
| **Data & BSS Init** | Externalized to `_init` in C runtime | Inlined assembly loops with LMA/VMA bypass check | **High Value**. Ensures `.data` copy is bypassed when running directly in SRAM (LMA == VMA), preventing overwriting initialized static variables. |
| **Trap Frame Size** | `272` bytes (accommodates RV64/FPU) | `128` bytes ($32 \times 4$) | **Optimized**. 128 bytes is 16-byte aligned and saves 144 bytes of stack space on each interrupt/exception. |

---

## 3. Critical Critique & Identified Vulnerabilities

### Issue 1: Missing `x2` (`sp`) Preservation in Trap Frame
- **Observation**:
  In `trap_entry`:
  ```assembly
  addi sp, sp, -128
  SREG x1,  1*REGBYTES(sp)
  /* sp saved later if needed */
  SREG x3,  3*REGBYTES(sp)
  ```
  `x2` (`sp`) is never stored to `2*REGBYTES(sp)`!
- **Consequence**:
  When a synchronous exception occurs (e.g. Load/Store Fault, Illegal Instruction) and `handle_trap()` in `system.c` accesses the register frame pointer `frame[2]`, it reads whatever stale value was on the stack at that offset rather than the stack pointer at the time of the fault.
- **Fix Required**:
  Before or immediately after `addi sp, sp, -128`, calculate the pre-trap `sp` and store it:
  ```assembly
  addi sp, sp, -128
  SREG x1, 1*REGBYTES(sp)
  addi t0, sp, 128
  SREG t0, 2*REGBYTES(sp)
  ```

### Issue 2: Section Alignment Dependency in `.data` and `.bss` Copy Loops
- **Observation**:
  Both `copy_data_loop` and `zero_bss_loop` step by 4 bytes:
  ```assembly
  addi t0, t0, 4
  addi t1, t1, 4
  ```
  If `_edata` or `_ebss` is not aligned to a 4-byte boundary, `beq` equality checks (`beq t1, t2`, `beq t0, t1`) can miss the termination address, causing memory corruption across adjacent memory regions.
- **Verification**:
  The linker script `aries_v3.ld` must enforce `. = ALIGN(4);` before defining `_edata` and `_ebss`.

### Issue 3: Section Name Conventions
- In `crt.S`, the section is `.section ".text.init"`.
- In `startup.S`, the section is `.section .text.startup, "ax", @progbits`.
- **Assessment**:
  The linker script must explicitly match `.text.startup` as the first section at `0x00202000` (or `0x20000000`).

---

## 4. Remediation Plan
1. Fix `x2` (`sp`) preservation in `trap_entry` by computing `sp + 128` into temporary register `t0` and saving to `2*REGBYTES(sp)`.
2. Ensure `newFiles/updatedFiles/src/platform/ARIES/startup.S` contains this hardened assembly.
