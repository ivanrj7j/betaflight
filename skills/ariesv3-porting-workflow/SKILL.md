---
name: ariesv3-porting-workflow
description: >-
  Standard operating procedure, build commands, and architectural guidelines for continuing
  the Betaflight port to C-DAC ARIES v3.0 (THEJAS32 / VEGA ET1031 RISC-V SoC). Use whenever
  implementing or debugging hardware drivers, startup routines, or updating HAL progress.
---

# ARIES v3.0 (THEJAS32 RISC-V) Betaflight Porting Workflow Skill

This skill contains the authoritative development runbook, directory architecture, compilation environment, and progress tracking guidelines for the **Betaflight ARIES v3.0** port.

---

## 1. Quick Environment & Toolchain Reference

- **Host OS:** Linux (Ubuntu/Debian)
- **Target MCU:** C-DAC THEJAS32 / VEGA ET1031 (32-bit RISC-V, RV32IM core without hardware 'A' atomic or 'F' float extensions)
- **Clock Speed:** 100 MHz
- **Memory:** 256 KB internal SRAM (`0x10000000`–`0x1003FFFF`), 2 MB external SPI NOR flash (`0x20000000`–`0x201FFFFF`)
- **Toolchain:** `riscv32-esp-elf-gcc` (GCC 13.2.0)
  - Path: `/home/ivan/.espressif/tools/riscv32-esp-elf/esp-13.2.0_20250707/riscv32-esp-elf/bin/riscv32-esp-elf-`
- **Compiler Flags:**
  `-march=rv32im_zicsr -mabi=ilp32 -mcmodel=medany` (Note: `_zicsr` is mandatory in GCC 13 for CSR instructions like `csrr mcycle`).

---

## 2. Standard Build & Verification Workflow

Always build from the repository root: `/home/ivan/Desktop/betaflight`.

### Clean Rebuild
```bash
make TARGET=ARIESV3 clean
make TARGET=ARIESV3 -j$(nproc)
```

### Output Binary Artifacts
- **ELF:** `obj/main/betaflight_ARIESV3.elf`
- **HEX:** `obj/betaflight_2026.12.0-alpha_ARIESV3.hex` (generated via `make TARGET=ARIESV3 hex`)
- **Map:** `obj/main/betaflight_ARIESV3.map`

### Inspection Commands
```bash
# Check memory footprint
/home/ivan/.espressif/tools/riscv32-esp-elf/esp-13.2.0_20250707/riscv32-esp-elf/bin/riscv32-esp-elf-size obj/main/betaflight_ARIESV3.elf

# Inspect symbol placement
/home/ivan/.espressif/tools/riscv32-esp-elf/esp-13.2.0_20250707/riscv32-esp-elf/bin/riscv32-esp-elf-nm -n obj/main/betaflight_ARIESV3.elf | grep <symbol>

# Disassemble entry/startup
/home/ivan/.espressif/tools/riscv32-esp-elf/esp-13.2.0_20250707/riscv32-esp-elf/bin/riscv32-esp-elf-objdump -d obj/main/betaflight_ARIESV3.elf --start-address=0x20000000 --stop-address=0x20000200
```

---

## 3. Repository Architecture & File Isolation

To preserve upstream maintainability, **never modify Betaflight core algorithms** (`src/main/fc/`, `scheduler/`, `pid/`). All platform code must reside in:

```
src/platform/ARIES/
├── include/platform/
│   ├── bsp_thejas32.h            # Memory-mapped registers & IRQ numbers (VEGA SDK)
│   ├── platform.h                # Interrupt control, MCU traits, memory decorators
│   └── timer_def.h               # Hardware timer definitions
├── link/
│   └── aries_v3.ld               # Memory sections: Flash, SRAM, stack, config PG
├── mk/
│   └── ARIES.mk                  # Sources, build flags, linker arguments (-nostartfiles)
├── target/ARIESV3/
│   ├── target.h                  # Hardware features enabled/disabled, pin stubs
│   └── target.mk                 # MCU identification (THEJAS32 / ARIES)
├── startup.S                     # Reset entry, register clearing, sp/gp/mtvec, context save/restore
├── system.c                      # Trap handler, IRQ table, cycle counter, timebase
├── serial_uart_aries.c           # Hardware UART0 (CLI/MSP) and UART1 (Serial RX)
└── stub_drivers.c                # Dummy fallbacks for unimplemented HAL drivers
```

---

## 4. Hardware Reference Source

When authoring real hardware drivers, refer directly to the official C-DAC BSP source files:
- **Base directory:** `lib/vega-sdk/vega-sdk/bsp/`
- **UART:** `drivers/uart/uart.c` & `include/uart.h`
- **SPI:** `drivers/spi/spi.c` & `include/spi.h`
- **Timers:** `drivers/timer/timer.c` & `include/timer.h`
- **GPIO:** `drivers/gpio/gpio.c` & `include/gpio.h`
- **Interrupts:** `drivers/interrupt/interrupt.c` & `include/interrupt.h`
- **SPI NOR Flash:** `include/m25p80_eeprom.h`

---

## 5. Progress Tracking Protocol (`HAL_PROGRESS.md`)

When completing any implementation phase or resolving blockers:
1. Open `HAL_PROGRESS.md`.
2. Update the **High-Level Milestone Tracker** table (mark status as `Completed` with current date).
3. Append a new **Detailed Execution Log** section for the phase:
   - Objective & Scope
   - Files Created & Modified
   - Problems / Obstacles Encountered & Exact Technical Solutions
   - Build Verification & Size metrics
   - Handoff instructions for the next phase
