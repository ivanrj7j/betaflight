# Code Critique: `src/platform/ARIES/link/aries_v3.ld` vs `lib/vega-bsp/thejas32/link1.lds` & `lib/vega-sdk/bsp/common/mbl.lds`

## 1. File Metadata & Overview
- **Source File**: [`src/platform/ARIES/link/aries_v3.ld`](../../src/platform/ARIES/link/aries_v3.ld)
- **Reference Files**:
  - [`lib/vega-bsp/thejas32/link1.lds`](../../lib/vega-bsp/thejas32/link1.lds) (Official C-DAC BSP RAM Linker Script)
  - [`lib/vega-sdk/vega-sdk/bsp/common/mbl.lds`](../../lib/vega-sdk/vega-sdk/bsp/common/mbl.lds)
- **Component**: Linker memory map, section layout, stack definition, symbol exports.

---

## 2. Structural & Memory Map Comparison

| Metric / Parameter | Reference (`link1.lds`) | Target (`aries_v3.ld`) | Evaluation |
| :--- | :--- | :--- | :--- |
| **Origin (`ORIGIN`)** | `0x202000` | `0x00202000` | **Exact match**. Sits precisely after the 8 KB bootloader partition (`0x00200000`–`0x00201FFF`). |
| **Capacity (`LENGTH`)** | `256K` (ignores 8 KB reserve) | `248K` ($253,952$ bytes) | **Superior safety in `aries_v3.ld`**. Using `248K` prevents the stack/bss from overflowing past physical SRAM limit `0x00240000`. |
| **Stack Allocation** | `__stack_size = 8K;` (.stack placed at end) | `8 KB` (.stack placed at end, 16-byte aligned) | **Matches**. 8 KB is proven stable for Betaflight nested interrupts. |
| **Global Pointer (`gp`)** | `PROVIDE( _gp = . + 0x800 );` | `PROVIDE(__global_pointer$ = _sdata + 0x800);` | **Standardized**. Sets `__global_pointer$` to midpoint of small data segment for 12-bit signed GP relaxation. |
| **Betaflight Sections** | Not present | `.pg_registry` & `.pg_resetdata` explicitly kept | **Mandatory**. Required for Parameter Group initialization and flash/RAM config persistence. |
| **Capacity Assertion** | None (silent overflow) | `ASSERT(_end <= (ORIGIN(RAM) + LENGTH(RAM)))` | **Crucial guard**. Fails link immediately if binary exceeds 248 KB limit. |

---

## 3. Critical Critique & Identified Vulnerabilities

### Issue 1: `.init` Placement vs Reset Vector Entry Point
- **Observation**:
  In `aries_v3.ld`:
  ```ld
  SECTIONS
  {
      .init : {
          . = ALIGN(4);
          KEEP (*(SORT_NONE(.init)))
      } > RAM

      .text : {
          . = ALIGN(4);
          *(.text.startup*)
  ```
  The C-DAC ROM bootloader transfers control unconditionally by jumping to `0x00202000`.
  If `.init` is defined before `.text` and any object file in the link pulls in a `.init` section, that code is placed at `0x00202000` before `_start` (`.text.startup`), resulting in execution of unintended code or instructions on boot!
- **Fix**:
  Ensure `*(.text.startup*)` is guaranteed to be the very first input section placed at `ORIGIN(RAM)`:
  ```ld
  .text : {
      . = ALIGN(4);
      KEEP(*(.text.startup*))
      KEEP(*(SORT_NONE(.init)))
      *(.text*)
  ```

### Issue 2: Consolidation of Read-Only Data (`.rodata`, `.srodata`)
- **Observation**:
  In `aries_v3.ld`, `.rodata` and `.srodata` are nested directly inside `.text`:
  ```ld
      *(.rodata*)
      *(.srodata*)
  ```
  While all code is in RAM on THEJAS32, keeping `.rodata` and `.srodata` explicitly aligned and placed cleanly before `.pg_registry` helps accurate size reporting (e.g. `size` command breaking down text vs data).

### Issue 3: Alignment Guarantees for Section Termination Symbols
- **Observation**:
  `_edata` and `_ebss` are used by assembly loops in `startup.S` (`beq` checks with 4-byte increments).
  `aries_v3.ld` has `. = ALIGN(4);` before `_edata` and `_ebss`, which prevents word-unaligned infinite loops.

---

## 4. Remediation Plan
1. Re-order sections in `newFiles/updatedFiles/src/platform/ARIES/link/aries_v3.ld` to ensure `KEEP(*(.text.startup*))` is strictly at `0x00202000`.
2. Cleanly isolate `.rodata` section with proper 4-byte boundaries.
