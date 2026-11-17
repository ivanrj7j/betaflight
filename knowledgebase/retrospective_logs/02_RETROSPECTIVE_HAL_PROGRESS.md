# Retrospective Log 02: HAL Progress & Hardware Hurdles (H1–H22)

> **Source File:** [`oldfiles/logs/HAL_PROGRESS.md`](file:///home/ivan/Desktop/betaflight/oldfiles/logs/HAL_PROGRESS.md)  
> **Target Platform:** C-DAC THEJAS32 SoC (VEGA ET1031 RISC-V RV32IM @ 100 MHz, 256 KB SRAM, no XIP Flash)  
> **Analysis Date:** 2026-11-17  
> **Status:** Completed

---

## 1. Executive Summary & Context

`HAL_PROGRESS.md` is the most comprehensive technical record in the previous attempt's repository. It documents:
1. **Phases 1 through 8** of the original HAL implementation (Startup, UART, Timer, GPIO, SPI, Config, PWM/RC, Soft-Float Tuning).
2. **The "Execution Topology & In-SRAM Boot Protocol" Discovery**, where the entire initial architectural premise (External SPI Flash XIP at `0x20000000`, SRAM at `0x10000000`) collapsed upon physical hardware bench testing.
3. **In-SRAM Execution Bring-up & Diagnostics Remediation**, documenting bootloader quirks, `.bin` vs `.hex` flashing pitfalls, `CONFIG_IN_RAM`, toolchain multilib RVC traps, and early UART output.
4. **Hurdles H1 to H22 Comprehensive Log**, listing 20 detailed post-mortems of system bugs, traps, and their engineering resolutions.

---

## 2. Decision & Action Scoring Matrix (0 to 10)

| ID | Phase / Hurdle | Decision / Action Description | Category | Score (0–10) | Justification & Technical Impact |
| :--- | :--- | :--- | :--- | :---: | :--- |
| **D01** | Phase 1 | Initial Linker Script assuming XIP Flash at `0x20000000` & RAM at `0x10000000` | Architectural Assumption | **1.0 / 10** | **Catastrophic Blind Assumption.** `0x10000000` is peripheral MMIO register space on THEJAS32, not SRAM. THEJAS32 has no XIP controller. Setting stack to `0x1000FEF0` wrote stack frames directly into peripheral registers. Developed for weeks in Renode without reading C-DAC `link1.lds`. |
| **D02** | Phase 1 | Context Switch / Trap Dispatcher in `startup.S` saving all 32 GPRs | Trap Architecture | **9.0 / 10** | **Solid Engineering.** Correctly saved 32 integer registers (`x1`–`x31`), handled `mcause` parsing, extracted interrupt bit 31, and routed interrupts through a 32-slot dispatcher table. Handled nested traps cleanly. |
| **D03** | Phase 2 | UART0 Base Address `0x10000100` & Divisor 54 Calculation | Peripheral Config | **9.5 / 10** | **Accurate Math.** Correctly derived standard 16550 baud divisor: $100\,\text{MHz} / (16 \times 115200) \approx 54.25 \rightarrow 54$ (0.47% baud error, well within 2% UART tolerance). Set `UART_LCR = 0x83` (DLAB=1, 8-N-1), programmed `DLL=54`, `DLH=0`, cleared DLAB to `0x03`. |
| **D04** | Phase 3 | Monotonic 64-bit Timer via `mcycle` / `mcycleh` Atomic Double-Read | Timing Architecture | **9.5 / 10** | **Brilliant RISC-V Pattern.** Standard `csrr(mcycle)` rolls over every 42.9 seconds at 100 MHz. Double-reading high/low/high CSRs in a loop eliminated rollover race condition, providing a 49.7-day monotonic microsecond timebase. |
| **D05** | Phase 4 | Bank 0 / Bank 1 Address-Decoded GPIO in `io_aries.c` | Pin Control | **8.5 / 10** | **Clean Abstraction.** Correctly mapped 32 pins per bank with proper register offsets (`GPIO_DATA_REG`, `GPIO_DIR_REG`) and bound onboard heartbeat LED `LD4` to `PA22`. |
| **D06** | Phase 5 | SPI Master Burst CS Retention via `Bits.CSAAT` | Bus Driver | **9.0 / 10** | **Critical Hardware Insight.** Standard THEJAS32 SPI controller deasserts CS between bytes unless `CSAAT` (Chip Select Active After Transfer) is set. Setting this bit allowed multi-byte burst register reads for ICM42688P/BMI270 IMUs. |
| **D07** | Phase 6 | Initial Config Storage Attempt via SPI3 Flash (`CONFIG_IN_FLASH` at `0x201F8000`) | Non-Volatile Storage | **1.5 / 10** | **Fatal Blunder.** Attempted memory-mapped dereference `*(volatile uint32_t*)0x201F8000` on an SPI controller without XIP. Immediate synchronous Load Access Fault (`mcause = 5`). |
| **D08** | Phase 7 | Soft PWM via Timer 1 IRQ 8 Non-Blocking Pulse Scheduler | Motor Output | **8.0 / 10** | **Clever Workaround.** THEJAS32 lacks hardware motor PWM timers with complementary outputs. Using Timer 1 IRQ to schedule sorted motor pin fall times allowed non-blocking 400 Hz PWM and OneShot125. |
| **D09** | Phase 8 | PID Decimation (`DEFAULT_PID_PROCESS_DENOM 8`) & Soft-Float Math Flags | Performance Tuning | **9.5 / 10** | **Masterful Profiling.** Clamping 8 kHz gyro to 1.0 kHz PID loop gave 100,000 cycles per iteration. Adding `-ffast-math -fno-math-errno` eliminated libc reentrancy calls. Disassembly audit verified 0 double-precision instructions. |
| **D10** | H1 / Arch | Pivot from Flash XIP to Mandatory In-SRAM Boot Protocol (`0x00202000`) | Architecture Pivot | **10.0 / 10** | **Pivotal Breakthrough.** Discovered empirical bootloader truth: C-DAC bootloader copies 248 KB from SPI Flash offset 0 into SRAM `0x00202000` and executes `jalr 0x00202000`. Unified linker script `RAM (wxa!ri) : ORIGIN = 0x00202000, LENGTH = 248K`. |
| **D11** | H2 / H8 / H20 | Clamping Build Footprint (< 240 KB Budget) via Modular Exclusion | Binary Sizing | **9.0 / 10** | **Aggressive & Effective Sizing.** Used `-DUSE_CONFIG`, `config.h`, `-Os`, and pruned non-flight bloat (OSD, Blackbox, GPS, 15+ serial RX drivers, 13 unused IMUs), shrinking the binary from 760 KB to 205 KB. |
| **D12** | H3 | Stack Size Reduction from 12 KB to 8 KB | Memory Allocation | **8.5 / 10** | **Pragmatic Fix.** Discovered linker overflow due to over-allocated 12 KB stack. Reverted to C-DAC BSP standard 8 KB stack (`link1.lds`), freeing 4 KB and fitting within 248 KB. |
| **D13** | H5 | Supplying Intel HEX (`.hex`) to `flasher.bat` | Tooling & Flashing | **2.0 / 10** | **Tooling Misunderstanding.** `vega-flasher.exe` streams raw bytes via XMODEM without Intel HEX parsing. Flashing `.hex` wrote ASCII text (`:1000...`) into flash, which the core executed as illegal instructions. |
| **D14** | H6 | Migration from `CONFIG_IN_FLASH` to `CONFIG_IN_RAM` | Configuration Storage | **9.0 / 10** | **Reliable Workaround.** Eliminated unmapped load traps by allocating `eepromData` in SRAM `.bss` (`0x0023B640`). Enables full parameter validation without risking bus faults. |
| **D15** | H7 / H12 | Polled Early UART0 Telemetry & Register-Based Hex Formatter | Diagnostics | **9.5 / 10** | **Essential Diagnostic Tooling.** Bypassed interrupts and circular buffers to output boot progress banners immediately in `systemInit()`. Fixed bounded timeout (50,000 cycles) and register ALU hex formatting (eliminating stack `memcpy`). |
| **D16** | H9 | Sizing `EEPROM_SIZE` at 4096 bytes without Checking PG Allocations | Memory Budgeting | **3.0 / 10** | **Over-Optimistic Truncation.** Betaflight PGs required > 4 KB. `resetEEPROM()` overflowed the buffer, corrupting PG headers and triggering `failureMode(FAILURE_CONFIG_STORE_FAILURE)`. Fixed by expanding to 8192 bytes. |
| **D17** | H10 | Pruning `serial_4way` without Undefining Bootloader Macros | Dependency Management | **5.0 / 10** | **Incomplete Pruning.** Pruned `io/serial_4way.c` but forgot that `common_post.h` auto-redefines interface macros if bootloader macros are present, causing unresolved linker symbols in `msp.c`. |
| **D18** | H13 | Toolchain Multilib Mismatch (`riscv32-esp-elf-gcc` injecting RVC instructions) | Toolchain & Compilation | **9.5 / 10** | **Masterful Compiler Debugging.** Discovered that Espressif GCC fell back to root multilib `.` (`rv32imafc`), injecting 16-bit compressed instructions into `libc.a` (`memset`, math) which crashed the RV32IM ET1031 core. Explicitly linked `rv32i_zicsr_zifencei/ilp32` multilib and provided bare-metal `memset`/`memcpy`. |
| **D19** | H14 / H17 | RISC-V Global Interrupts (`mstatus.MIE` / `mie.MEIE`) Left Unarmed | Architecture Integration | **2.0 / 10** | **Fundamental Blind Spot.** Assumed interrupts would be enabled by default (like ARM Cortex-M). Betaflight never called `__enable_irq()`, leaving the system completely deaf to hardware interrupts. |
| **D20** | H15 | Circular Buffer Modulo with Zero Divisor (`rxBufferSize == 0`) | Defensive Coding | **4.0 / 10** | **Missing NULL / Zero Guard.** Queried `uartTotalRxBytesWaiting()` on uninitialized serial ports during early boot, triggering a division-by-zero exception (`mcause = 0x1C`). |
| **D21** | H16 | Setting `UART0_RX_PIN` & `UART0_TX_PIN` to `NONE` (0) | Hardware Abstraction | **2.5 / 10** | **Core Contract Violation.** Thought assigning `NONE` was appropriate because UART0 pins are dedicated on-chip. Betaflight's serial driver stripped `MODE_RX` and `MODE_TX` and dropped writes if pin tags were 0. |
| **D22** | H18 | DW_apb_uart 16550 `UART_IIR` Unacknowledged Interrupt Latch | Peripheral Hardware | **8.5 / 10** | **Subtle Hardware Nuance.** Recognized that reading `UART_IIR_FCR` is mandatory to acknowledge and clear pending THRE status in Synopsys DW_apb_uart IP. |
| **D23** | H21 | Emitting Diagnostic ASCII Telemetry (`[SERIAL ALIVE]`, `[MSP RX]`) on Wire | Protocol Discipline | **1.0 / 10** | **Terrible Protocol Violation.** Injected ASCII text into binary MSP stream on UART0, corrupting frames and causing Betaflight Configurator to time out. |
| **D24** | H21 / H22 | Enabling 16550 THRE Interrupt (`UART_IE |= 0x02`) causing Interrupt Storm | Interrupt Architecture | **2.0 / 10** | **Flawed Architecture.** Attempted to drive TX via THRE interrupt on DW_apb_uart IP. Reading `IIR` cleared the status, causing concurrent race conditions and continuous IRQ storms. |
| **D25** | H22 | Synchronous Bounded TX Draining Matching C-DAC BSP (`UART_IE = 0x01`) | Interrupt Architecture | **9.5 / 10** | **Decisive Architectural Correction.** Consulted official C-DAC `HardwareSerial.cpp`, permanently disabled TX interrupts, and implemented synchronous bounded FIFO draining in `uartEnableTxInterrupt()`, restoring clean CLI and MSP operation. |

---

## 3. Deep-Dive: Brilliant Insights vs. Critical Failures

### 3.1 The Brilliant Parts (Highlights of Ingenuity)

1. **The Multilib RVC Trap Unmasking (Score: 9.5/10 - Hurdle H13)**:
   - *Discovery:* The physical ET1031 silicon rebooted in an infinite loop during `pgResetAll()`.
   - *Diagnosis:* The Espressif RISC-V toolchain does not provide a separate `rv32im` multilib. When passing `-march=rv32im`, GCC defaulted to the root multilib compiled with `-march=rv32imafc`. Standard C functions like `memset()`, `memcpy()`, and software floating-point routines in `libgcc.a` contained 16-bit compressed instructions.
   - *Fix:* Written custom bare-metal `memset()`, `memcpy()`, `memmove()` in `system.c` with `-fno-builtin`, and explicitly directed linker flags to the `rv32i_zicsr_zifencei/ilp32` multilib directory.
   - *Takeaway:* Always audit disassembly with `objdump -d | grep -E '\b(c\.[a-z]+)'` to ensure zero RVC instructions exist on RV32IM targets.

2. **The In-SRAM Execution Reality (Score: 10.0/10 - Hurdle H1)**:
   - *Discovery:* The ARIES v3 board on-chip bootloader outputted:
     ```text
     Copying from FLASH to IRAM
     [INFO] Copying 250KB from address: 0x000000.
     Starting program ...
     ```
   - *Diagnosis:* There is no XIP flash controller on THEJAS32. The bootloader performs an SPI DMA transfer of 248 KB into SRAM at `0x00202000` and jumps to `0x00202000`.
   - *Fix:* Redesigned the memory map into a single unified SRAM region:
     ```ld
     MEMORY { RAM (wxa!ri) : ORIGIN = 0x00202000, LENGTH = 248K }
     ```
   - *Takeaway:* Firmware MUST fit within 248 KB combined (`.text` + `.rodata` + `.data` + `.bss` + `.stack`).

3. **C-DAC Synchronous TX Architecture (Score: 9.5/10 - Hurdle H22)**:
   - *Discovery:* Trying to manage TX via 16550 THRE interrupts led to race conditions, IIR latch clearing, and scheduler stalls.
   - *Diagnosis:* The Synopsys DW_apb_uart IP does not support reliable THRE interrupt handling when concurrent foreground operations read `IIR`. Official C-DAC BSPs explicitly set `UART_IE = 0x01` (RX interrupt only) and never use TX interrupts.
   - *Fix:* Synchronous bounded FIFO draining in `uartEnableTxInterrupt()` with a timeout of 50,000 cycles (~2.5 ms), and pure RX interrupt servicing in `uartIrqHandler()`.

---

### 3.2 The Flawed & Stupid Parts (What Must Never Be Repeated)

1. **Simulating in Renode Without Checking Hardware Memory Maps (Score: 1.0/10)**:
   - The team spent Phases 1 through 7 writing a complete HAL based on a fictional Renode script where Flash was at `0x20000000` and RAM was at `0x10000000`.
   - On actual THEJAS32 silicon, `0x10000000` is peripheral MMIO! Writing stack frames to `0x1000FEF0` corrupted hardware peripheral control registers.
   - *Rule:* Always consult the vendor reference BSP linker script (`link1.lds`) and official datasheet BEFORE designing memory maps.

2. **ASCII Logging on the Binary MSP Wire (Score: 1.0/10)**:
   - Emitting `[SERIAL ALIVE]` and `[MSP RX]` debug strings directly onto UART0 completely broke Betaflight Configurator.
   - *Rule:* Never print unpacketized ASCII strings to a serial port configured for MSP or telemetry. If debug logging is needed, use a dedicated secondary UART (UART1 on Header J2) or early boot polling before serial initialization.

3. **Flashing Intel HEX with a Raw XMODEM Flasher (Score: 2.0/10)**:
   - `flasher.bat` / `vega-flasher.exe` was fed `betaflight.hex`. The utility blindly sliced the ASCII HEX text into 128-byte XMODEM packets and flashed it.
   - The bootloader copied raw ASCII text into SRAM and jumped to it, immediately crashing on invalid opcodes.
   - *Rule:* Always flash raw binary (`.bin`) images when using C-DAC XMODEM flasher tools.

4. **Assigning `NONE` (0) to UART0 Hardware Pin Tags (Score: 2.5/10)**:
   - Because UART0 was connected to the onboard CP2102N USB bridge without reconfigurable pin multiplexing, the developers set `UART0_RX_PIN` and `UART0_TX_PIN` to `NONE`.
   - Betaflight core logic checked `tx.pin` and `rx.pin`; encountering `0` (NONE), it stripped `MODE_RX` and `MODE_TX` and disabled writes.
   - *Rule:* In Betaflight, every active serial port MUST have unique, valid pin tags assigned (e.g. `PA30` / `PA31`), even if fixed in hardware.

5. **Omitting `__enable_irq()` (Score: 2.0/10)**:
   - On RISC-V, interrupts reset to disabled (`mstatus.MIE = 0`, `mie.MEIE = 0`).
   - The team tested for days wondering why UART and Timer interrupts never fired, unaware that `__enable_irq()` had never been invoked in `main()`.

---

## 4. Key Takeaways for the Minimal 128 KB SRAM Build

1. **Target Budget: 128 KB Flash / SRAM:**
   - The previous attempt reached **205 KB** for flight-ready code (PID + ICM42688P + PWM + CRSF + MSP).
   - For our specific target (minimal USB-UART MSP Configurator handshake), we can strip IMU drivers, PWM motor scheduling, CRSF/ELRS receiver protocols, and non-volatile config storage to easily fit inside **< 128 KB**.
2. **Mandatory Build System Directives:**
   - Unified Linker Script: `ORIGIN = 0x00202000, LENGTH = 128K` (or 248K with strict size assertion).
   - Multilib Flags: Force `-march=rv32i_zicsr_zifencei -mabi=ilp32` for GCC C runtime libraries.
   - No RVC / No FPU: Enforce zero compressed instructions.
   - Stack Size: 4 KB to 8 KB max.
   - UART Driver: Polled or RX-only interrupt (`UART_IE = 0x01`), bounded synchronous TX drain, zero TX interrupts.
   - Serial Pins: Explicitly assign non-zero pin tags (`PA30` / `PA31`) to UART0.
   - Global Interrupts: Explicitly call `__enable_irq()` in `main()`.
   - Flashing Format: Output and flash strictly `.bin` files via XMODEM.

---
*Retrospective log compiled and verified against historical `oldfiles/logs/HAL_PROGRESS.md` records.*
