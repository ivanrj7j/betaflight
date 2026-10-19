# RENODE_CHANGES.md

## Summary of changes made during the startup‑fix investigation

### 1. Added minimal startup assembly
- **File:** `src/platform/ARIES/startup.S`
- Purpose: Initialize stack pointer (`_estack`), optionally set global pointer, copy `.data` from flash to RAM, zero `.bss`, call `main()`, and halt if `main` returns.
- Content includes comments and uses symbols defined by the linker script (`_estack`, `_sidata`, `_sdata`, `_edata`, `_sbss`, `_ebss`).

### 2. Updated ARIES makefile
- **File:** `src/platform/ARIES/mk/ARIES.mk`
- **Changes:**
  1. Added `ARIES/startup.S` to `MCU_COMMON_SRC` so the startup object is compiled and linked.
  2. Added `-nostartfiles` flag to `LD_FLAGS` to prevent the default `crt0.o` (which defines its own `_start`) from being linked, ensuring our custom `_start` is used.

### 3. Modified the ARIES linker script
- **File:** `src/platform/ARIES/link/aries_v3.ld`
- **Changes:**
  1. Changed `ENTRY(main)` → `ENTRY(_start)` to make the linker entry point our new startup symbol.
  2. Restored the `.stack` section (16 KB, aligned to 16) and defined `_sstack` / `_estack`.
  3. Added `PROVIDE` symbols for the global pointer and BSS boundaries:
     ```ld
     PROVIDE(__global_pointer$ = _sdata + 0x800);
     PROVIDE(__bss_start = _sbss);
     PROVIDE(__bss_end = _ebss);
     ```
  4. Ensured `_end` is defined after the stack.

### 4. Renode validation attempt
- Ran a Renode script to load the rebuilt ELF, set the PC to `0x20000000`, and read a few stack locations.
- **Result:** The simulation aborted because the CPU model had the RVC (compressed‑instruction) extension disabled, leading to an illegal instruction (`0x433d`).
- **Implication:** For a full validation we need to enable the `rv32im_zicsr_c` ISA (or at least `c`) in the Renode platform description.

### 5. Build verification
- After applying the above changes the firmware builds cleanly:
  ```
  text   data    bss    dec    hex filename
  718688  6336  58928  783952  bf650 ./obj/main/betaflight_ARIESV3.elf
  ```
- The ELF entry point is now `_start` (address `0x20000000`).

## How to rollback
If you need to revert to the original state, perform the following steps:
1. **Remove `startup.S`**:
   ```bash
   rm src/platform/ARIES/startup.S
   ```
2. **Restore `ARIES.mk`** to its original content (remove the line adding `ARIES/startup.S` and the `-nostartfiles` flag).
3. **Restore `aries_v3.ld`**:
   - Change `ENTRY(_start)` back to `ENTRY(main)`.
   - Delete the `.stack` section and the three `PROVIDE` lines that were added.
4. **Clean and rebuild**:
   ```bash
   make TARGET=ARIESV3 clean
   make TARGET=ARIESV3
   ```

These steps will bring the repository back to the state it was in before this investigation.
