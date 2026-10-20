# GEMINI Documentation for Betaflight ARIES V3 (THEJAS32 RISC-V)

---

> [!IMPORTANT]
> **To Any Agent or Developer Picking Up this Project:**
> Before continuing work, you **must load and follow the porting skill**:
>
> 📁 **Skill File:** [`skills/ariesv3-porting-workflow/SKILL.md`](file:///home/ivan/Desktop/betaflight/skills/ariesv3-porting-workflow/SKILL.md)
>
> This skill contains the active build instructions, toolchain configuration (`riscv32-esp-elf-gcc` v13.2.0), peripheral register mappings from the C-DAC VEGA SDK BSP, and the required logging protocol for [`HAL_PROGRESS.md`](file:///home/ivan/Desktop/betaflight/HAL_PROGRESS.md).

---

## 1. Project Overview & Current State

This repository contains the Betaflight flight controller firmware adapted for the **C-DAC ARIES v3.0** development board powered by the **THEJAS32 SoC** (VEGA ET1031 32-bit RISC-V core at 100 MHz, RV32IM without hardware floating point or atomic extensions).

### Milestones Completed

- **Phase 1: Bare-Metal Startup & Trap Vector (`Completed`)**
  - Full GPR initialization (`x1`–`x31`), stack pointer setup (`_estack` = `0x1000FEF0`), global pointer setup (`__global_pointer$` = `0x10000800`).
  - Machine trap vector routing (`mtvec` = `trap_entry` at `0x200000F0`).
  - 32-register context save/restore frame with `mret`.
  - C-level trap dispatcher in `system.c` distinguishing synchronous exceptions from Machine External Interrupts (`cause 11`).
  - 32-slot interrupt table (`interrupt_table[32]`) hooked into the THEJAS32 interrupt controller at `0x20010000`.

- **Phase 2: Console & Communication (`Completed`)**
  - Full hardware UART driver in `src/platform/ARIES/serial_uart_aries.c` mapping 16550 registers (`0x10000100`).
  - Baud rate divisor formula: $\text{Divisor} = \frac{100,000,000}{16 \times \text{Baudrate}}$ ($54$ for 115200 baud).
  - Interrupt-driven RX ring buffer and transmit FIFO priming.
  - Enabled `USE_UART0` (CP2102N USB-UART bridge for CLI/MSP) and `USE_UART1` (serial RX).
  - `#define SERIAL_UART_FIRST_INDEX 0` configured in `platform.h`.
  - Clean compilation with exit code `0` (~723.7 KB Flash / ~65.5 KB RAM).

- **Phase 3: High-Resolution Timebase (`Completed`)**
  - Eliminated 42.9-second 32-bit `mcycle` rollover jitter in `micros()` by implementing atomic 64-bit cycle reading (`getCycleCounter64()`) using `mcycleh` and `mcycle`.
  - Fast inlined reciprocal division by constant `100ULL` without `__udivdi3` software division calls.
  - Aligned `millis()` to standard 49.7-day rollover via `getCycleCounter64() / 100000ULL`.
  - Integrated C-DAC THEJAS32 hardware Timer 0 (`0x10000A00`): periodic user-defined reload (`LoadCount = 0xFFFFFFFF`), interrupt registration on IRQ 7 (`THEJAS32_IRQ_TIMER0`), interrupt service routine (`thejas32_timer0_isr`) reading `EOI` register, and overflow tracking.
  - Clean compilation with exit code `0` (~735.9 KB Flash / ~65.5 KB RAM).

- **Phase 4: GPIO & External Interrupts (`Completed`)**
  - Implemented THEJAS32 address-decoded GPIO driver in `src/platform/ARIES/io_aries.c` for Bank 0 (`0x10080000`, pins 0–15) and Bank 1 (`0x10180000`, pins 16–31).
  - Populated `src/platform/ARIES/io_def_generated.h` mapping 32 pins (`PA0` through `PA31`) with `#define DEFIO_PORT_PINS 64`.
  - Implemented external interrupt driver in `src/platform/ARIES/exti_aries.c` routing Bank 0 (IRQ 10) and Bank 1 (IRQ 11) interrupts into per-pin callback records (`extiChannels[32]`).
  - Enabled `#define USE_EXTI` and configured `#define LED0_PIN PA22` (Heartbeat LED `LD4`) in `target.h`.
  - Clean compilation with exit code `0` (~762.7 KB Flash / ~66.5 KB RAM).

---

## 2. Directory Navigation & Key Documentation

| File | Purpose / Description |
| :--- | :--- |
| [`skills/ariesv3-porting-workflow/SKILL.md`](file:///home/ivan/Desktop/betaflight/skills/ariesv3-porting-workflow/SKILL.md) | **Authoritative workflow skill** containing build commands, toolchain paths, and HAL rules. |
| [`IMPLEMENTATION_PLAN.md`](file:///home/ivan/Desktop/betaflight/IMPLEMENTATION_PLAN.md) | Full 8-Phase Architectural Plan covering registers, pins, risks, and roadmap. |
| [`HAL_PROGRESS.md`](file:///home/ivan/Desktop/betaflight/HAL_PROGRESS.md) | **Living progress log** with problems, solutions, and binary metrics for every phase. |
| [`COMPILATION_GUIDE.md`](file:///home/ivan/Desktop/betaflight/COMPILATION_GUIDE.md) | Step-by-step flashing guide (OpenOCD, JTAG, VEGA Flasher) and CLI verification. |
| [`PORT_GUIDE.md`](file:///home/ivan/Desktop/betaflight/PORT_GUIDE.md) | Architectural roadmap, hardware pinout diagram, and soft-float CLI tuning. |
| [`src/platform/ARIES/`](file:///home/ivan/Desktop/betaflight/src/platform/ARIES/) | Core target directory containing all ARIES v3.0 platform source files. |
| [`lib/vega-sdk/vega-sdk/bsp/`](file:///home/ivan/Desktop/betaflight/lib/vega-sdk/vega-sdk/bsp/) | Official C-DAC VEGA SDK reference drivers (UART, SPI, Timer, GPIO, Interrupts). |

---

## 3. Quick Build Commands

Execute directly from the repository root:
```bash
# 1. Clean previous artifacts
make TARGET=ARIESV3 clean

# 2. Build complete firmware image
make TARGET=ARIESV3 -j$(nproc)

# 3. Generate Intel HEX output
make TARGET=ARIESV3 hex
```

### Inspect Output Binary
```bash
# Sizing
/home/ivan/.espressif/tools/riscv32-esp-elf/esp-13.2.0_20250707/riscv32-esp-elf/bin/riscv32-esp-elf-size obj/main/betaflight_ARIESV3.elf

# Disassembly check of startup & trap vector
/home/ivan/.espressif/tools/riscv32-esp-elf/esp-13.2.0_20250707/riscv32-esp-elf/bin/riscv32-esp-elf-objdump -d obj/main/betaflight_ARIESV3.elf --start-address=0x20000000 --stop-address=0x20000150
```

---

## 4. Renode Simulation Validation

1. **Start Renode** and load the ARIES v3 platform:
   ```text
   mach create
   machine LoadPlatformDescription @/home/ivan/ariesv3-sim/ariesv3.repl
   ```
   *(Note: Ensure `cpuType` in `ariesv3.repl` is set to `rv32im_zicsr_c` to support compressed instructions).*
2. **Load ELF and Run**:
   ```text
   sysbus LoadELF @/home/ivan/Desktop/betaflight/obj/main/betaflight_ARIESV3.elf
   start
   ```
3. **Expected Behavior**:
   - PC starts at `0x20000000` (`_start`).
   - Stack pointer is loaded with `0x1000FEF0`.
   - `mtvec` is loaded with `0x200000F0` (`trap_entry`).
   - No invalid writes to unmapped memory (`0xFFFFFFFC`).

---

## 5. Next Steps for Next Developer / Agent

Proceed directly to **Phase 5: SPI Bus & IMU Sensor Driver**:
1. Implement `src/platform/ARIES/bus_spi_aries.c` mapping SPI0 master registers (`0x10000600`) from C-DAC VEGA SDK `bsp/drivers/spi/spi.c`.
2. Configure SPI control word: 8-bit transfers, CPOL/CPHA Mode 0/3, and Chip Select Active After Transfer (`Bits.CSAAT`) for burst reads.
3. Configure `target.h` with SPI0 pins on Header J1 (`J1_5` SCK, `J1_6` MISO, `J1_7` MOSI, `J1_8` CS).
4. Verify IMU sensor detection (`ICM42688P` or `BMI270`) via CLI `status`.
5. Document all changes and problems encountered in [`HAL_PROGRESS.md`](file:///home/ivan/Desktop/betaflight/HAL_PROGRESS.md).
