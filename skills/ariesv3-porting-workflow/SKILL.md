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
  - Path: `~/.espressif/tools/riscv32-esp-elf/esp-13.2.0_20250707/riscv32-esp-elf/bin/riscv32-esp-elf-`
- **Compiler Flags:**
  `-march=rv32im_zicsr -mabi=ilp32 -mcmodel=medany` (Note: `_zicsr` is mandatory in GCC 13 for CSR instructions like `csrr mcycle`).

---

## 2. Standard Build & Verification Workflow

Always build from the repository root: `.`.

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
~/.espressif/tools/riscv32-esp-elf/esp-13.2.0_20250707/riscv32-esp-elf/bin/riscv32-esp-elf-size obj/main/betaflight_ARIESV3.elf

# Inspect symbol placement
~/.espressif/tools/riscv32-esp-elf/esp-13.2.0_20250707/riscv32-esp-elf/bin/riscv32-esp-elf-nm -n obj/main/betaflight_ARIESV3.elf | grep <symbol>

# Disassemble entry/startup
~/.espressif/tools/riscv32-esp-elf/esp-13.2.0_20250707/riscv32-esp-elf/bin/riscv32-esp-elf-objdump -d obj/main/betaflight_ARIESV3.elf --start-address=0x20000000 --stop-address=0x20000200
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

---

## 6. Critical Hardware Risks & Architectural Constraints

Any AI agent or developer continuing this port **must be aware of and strictly adhere to** the following failure modes and architectural limitations documented in [`RISK_ASSESMENT.md`](../../RISK_ASSESMENT.md):

### ⚠️ RSK-01: Soft-Float Arithmetic & Loop Overrun (RV32IM without 'F')
- **The Risk:** THEJAS32 lacks hardware floating-point instructions. All IEEE-754 `float` and `double` operations are computed via software emulation (`libgcc.a` routines: `__addsf3`, `__mulsf3`, `__divsf3`, `__divdf3`). An unconstrained 8 kHz flight loop will overrun the 125 µs task budget, starving the scheduler and freezing control.
- **The Rule:**
  - Always clamp the flight loop to **1.0 kHz** (`TASK_GYROPID_DESIRED_PERIOD 1000`).
  - Never use double-precision float literals (`1.0` -> use `1.0f`).
  - Maintain `-ffast-math` in `target.h`. Profile task execution time using the CLI `tasks` command.

### ⚠️ RSK-02: Single-Core Atomic Race Conditions (RV32IM without 'A')
- **The Risk:** Without LR/SC or AMO instructions (`lr.w`, `sc.w`, `amoadd.w`), standard atomic primitives cannot execute atomically in hardware.
- **The Rule:** Single-core atomic fallback routines (`__atomic_fetch_add_4`) and critical shared state access must disable interrupts globally (`__disable_irq()` / `__enable_irq()`) to prevent corruption from high-frequency peripheral ISRs.

### ⚠️ RSK-03: Timer Interrupt Acknowledgment (`EOI` Register)
- **The Risk:** THEJAS32 hardware timers (Synopsys DW_apb_timers) assert an interrupt on down-counter underflow. If the interrupt is not cleared, returning via `mret` immediately re-enters the ISR, deadlocking the core in an infinite interrupt loop.
- **The Rule:** The very first operation inside `thejas32_timer0_isr()` must be:
  ```c
  (void)THEJAS32_TIMER(0).EOI;
  ```

### ⚠️ RSK-04: Multi-Segment SPI Burst Transfers (`Bits.CSAAT`)
- **The Risk:** IMU sensor reads (e.g. ICM42688P or BMI270) stream 14 continuous bytes. If the SPI controller de-asserts Chip Select between address and data segments, the sensor resets its auto-increment pointer, returning invalid `0x00` or `0xFF` data.
- **The Rule:** Set `Bits.CSAAT = 1` during all intermediate segments and de-assert (`Bits.CSAAT = 0`) only on the final segment of the transaction.

### ⚠️ RSK-05: Flash Erase Latency & Wait Loop Bounding
- **The Risk:**
  - 4 KB sector erase on SPI NOR flash blocks for 30–50 ms. Config writes must **never** be permitted while armed (`ARMING_DISABLED_CLI`).
  - In simulation or if hardware SPI lines are unresponsive, unbounded `while (status & BUSY)` loops will freeze the CPU.
- **The Rule:** All SPI flash polling loops must include bounded cycle counters (e.g., 1,000 cycles for byte TX, 100 cycles for Write-Enable WEL polling).

### ⚠️ RSK-06: Clock Tree & Baud Rate Divisor Dependencies
- **The Risk:** Divisors assume a 100 MHz APB peripheral clock:
  $$\text{Divisor} = \frac{100,000,000}{16 \times \text{Baudrate}}$$
  (Divisor = 54 for 115200 baud). If the board's ROM bootloader configures a different crystal or PLL multiplier (e.g. 50 MHz or 40 MHz), the serial console and microsecond timers will run at incorrect speeds.
- **The Rule:** Keep core clock frequency centralized in `SystemCoreClock` and verify frequency against timer counter ticks.
