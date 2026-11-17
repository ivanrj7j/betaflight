# Knowledge Evaluation Node: `src/platform/ARIES/link/aries_v3.ld`
**Evaluated Log**: [`logs/code_critique/02_LINKER_ARIES_V3_LD.md`](file:///home/ivan/Desktop/betaflight/logs/code_critique/02_LINKER_ARIES_V3_LD.md)  
**Target Code**: [`src/platform/ARIES/link/aries_v3.ld`](file:///home/ivan/Desktop/betaflight/src/platform/ARIES/link/aries_v3.ld)  
**Reference Code**:
- [`lib/vega-bsp/thejas32/link1.lds`](file:///home/ivan/Desktop/betaflight/lib/vega-bsp/thejas32/link1.lds) (Official C-DAC RAM Linker Script)
- [`lib/vega-sdk/vega-sdk/bsp/common/mbl.lds`](file:///home/ivan/Desktop/betaflight/lib/vega-sdk/vega-sdk/bsp/common/mbl.lds)  
**Component**: Linker Script, Memory Mapping, Section Layout, Stack Allocation, Size Assertions.

---

## 1. Context & Architectural Role
`aries_v3.ld` defines the memory layout for compiling Betaflight on C-DAC THEJAS32. Because the SoC lacks Execute-In-Place (XIP) over external SPI flash, the entire firmware image must reside inside the internal SRAM partition starting at `0x00202000` (right above the 8 KB ROM bootloader partition) with a strict ceiling of 248 KB ($253,952$ bytes).

---

## 2. Decision & Action Scoring (0 to 10)

| Decision / Action Taken | Score (0-10) | Category | Technical Analysis & Rationale | References |
| :--- | :---: | :---: | :--- | :--- |
| **Enforcing 248 KB Limit (`LENGTH = 248K`) & Linker Assertion** | **9.8 / 10** | **Masterpiece** | C-DAC reference `link1.lds` set `LENGTH = 256K`, ignoring that the first 8 KB is used by the ROM bootloader. Defining `LENGTH = 248K` and adding `ASSERT(_end <= ...)` guarantees the binary cannot silently overflow physical SRAM. | [`aries_v3.ld#L6-L78`](file:///home/ivan/Desktop/betaflight/src/platform/ARIES/link/aries_v3.ld#L6-L78) vs [`link1.lds#L7`](file:///home/ivan/Desktop/betaflight/lib/vega-bsp/thejas32/link1.lds#L7) |
| **Origin Placement at `0x00202000`** | **9.5 / 10** | **Brilliant** | Matches the exact bootloader jump target of the on-chip ROM, ensuring instant firmware execution without a secondary stage bootloader. | [`aries_v3.ld#L6`](file:///home/ivan/Desktop/betaflight/src/platform/ARIES/link/aries_v3.ld#L6) |
| **Explicit Parameter Group Linker Sections (`.pg_registry`, `.pg_resetdata`)** | **9.0 / 10** | **Brilliant** | Preserving `__pg_registry_start/end` and `__pg_resetdata_start/end` with `KEEP()` and `SORT()` is strictly required for Betaflight's configuration system to discover settings structures. | [`aries_v3.ld#L29-L43`](file:///home/ivan/Desktop/betaflight/src/platform/ARIES/link/aries_v3.ld#L29-L43) |
| **8 KB Dedicated Stack (`.stack` section)** | **8.5 / 10** | **Good** | Reserving `_sstack` to `_estack` (0x2000 bytes, 16-byte aligned) provides verified headroom for deep nested interrupts (Timer 0, Timer 1, UART RX). | [`aries_v3.ld#L66-L71`](file:///home/ivan/Desktop/betaflight/src/platform/ARIES/link/aries_v3.ld#L66-L71) |
| **Placing Standalone `.init` Before `.text.startup`** | **3.0 / 10** | **Flawed** | Having `.init` defined before `.text` means if any library defines a `.init` section, it will be placed at `0x00202000` ahead of `_start` (`.text.startup`), causing the core to execute wrong code on boot. | [`aries_v3.ld#L13-L20`](file:///home/ivan/Desktop/betaflight/src/platform/ARIES/link/aries_v3.ld#L13-L20) |
| **Folding `.rodata` Directly into `.text`** | **6.0 / 10** | **Mediocre** | While functional in RAM, nesting `.rodata` and `.srodata` directly inside `.text` obscures binary analysis by merging constant data into the executable code size metrics. | [`aries_v3.ld#L24-L25`](file:///home/ivan/Desktop/betaflight/src/platform/ARIES/link/aries_v3.ld#L24-L25) |

---

## 3. Deep-Dive Analysis

### 🌟 What Were the Good / Brilliant Parts?
1. **The 248 KB Linker Assertion**:
   In embedded projects with tight memory boundaries, linkers without assertions silently overflow beyond physical SRAM limits, leading to silent memory wrapping and corrupted register states. The assertion:
   ```ld
   ASSERT(_end <= (ORIGIN(RAM) + LENGTH(RAM)), "Error: Firmware image exceeds internal SRAM capacity (248 KB)!")
   ```
   provides compile-time verification of physical hardware safety.
2. **Explicit 4-Byte Boundaries on Termination Symbols**:
   Enforcing `. = ALIGN(4);` before `_edata` and `_ebss` ensures word-aligned endpoints, preventing loop termination misses in `startup.S`.

### ⚠️ What Went Wrong?
1. **The `.init` Section Precedence Trap**:
   In `aries_v3.ld`, lines 13–16 declare:
   ```ld
   .init : {
       . = ALIGN(4);
       KEEP (*(SORT_NONE(.init)))
   } > RAM
   ```
   followed by `.text` on line 18 with `*(.text.startup*)`. Because the ROM bootloader jumps directly to `ORIGIN(RAM)` (`0x00202000`), whatever input section is listed first in `SECTIONS` receives that physical address. If `.init` has any content, `_start` will not be at `0x00202000`.

### 💡 What Could Have Been Done Better?
- **Unified Text Section**: Put `KEEP(*(.text.startup*))` at the very top of `.text` as the first item in RAM, and enclose `KEEP(*(SORT_NONE(.init)))` immediately after it.
- **Dedicated `.rodata` Section**: Separate read-only constants into a distinct `.rodata` block for cleaner memory footprint profiling.

---

## 4. Key Takeaways & Overall Score: 8.5 / 10
`aries_v3.ld` is a well-designed linker script that correctly models the in-SRAM execution reality of THEJAS32. Fixing the `.init` ordering ensures guaranteed reset vector placement.
