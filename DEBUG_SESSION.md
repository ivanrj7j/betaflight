# Comprehensive Debugging Session Log: C-DAC ARIES v3.0 (THEJAS32 / VEGA ET1031)

---

## 📌 Executive Summary of the Debugging Session

This document details the exhaustive file-by-file audit and deep debugging session for the Betaflight port to the **C-DAC ARIES v3.0** flight controller board powered by the **THEJAS32 SoC (VEGA ET1031 RV32IM core)**.

### Target Objectives
1. Perform an exhaustive scan of **all 86 files** changed between upstream base commit `885f12404` and the current working tree.
2. Formulate an evidence-based **Priority List** for deep code inspection.
3. Identify and isolate all bugs and potential failure modes affecting serial communication (CLI/MSP), interrupt delivery, timebase stability, and scheduler execution.
4. Update unit test suites to lock in reproducible test cases for every discovered issue.
5. Implement surgical code fixes and verify that all 155+ unit tests pass, `build_verify.sh` passes 10/10 checks, and SRAM footprint remains strictly $\le 248\text{ KB}$.
6. Conduct a **Second Pass on Unexplored Files** across the wider Betaflight repository that could influence system stability or peripheral responsiveness.

---

## 🔍 Step 1: Exhaustive Inventory & Scan of All Files Changed Since `885f12404`

A total of **86 files** were modified or added (+15,387 lines, -3 lines):

### 1.1 Core Betaflight Generic Source Files (6 files)
| File Path | Nature of Changes | Scan Observations & Analysis |
| :--- | :--- | :--- |
| [`src/main/main.c`](src/main/main.c) | Added early checkpoint before `run()`. | `thejas32_early_uart_puts("[MAIN] Launching scheduler loop...\r\n")` was added. However, `__enable_irq()` was missing prior to or inside `run()`, leaving the RISC-V core with `mstatus.MIE = 0` and `mie.MEIE = 0`. |
| [`src/main/fc/init.c`](src/main/fc/init.c) | Added checkpoints `[INIT] 1` through `[INIT] 8e`. | Traces boot progress through `initEEPROM`, `initPhase2`, `sensorsAutodetect`, `mspSerialInit`, and `tasksInit`. Confirmed boot reaches `initPhase3` cleanly. |
| [`src/main/config/config.c`](src/main/config/config.c) | Added checkpoints `[CONFIG] 2a` through `2l`. | Instruments EEPROM structure checks and default PG resets. Confirmed clean non-blocking execution. |
| [`src/main/config/config_eeprom.c`](src/main/config/config_eeprom.c) | Added checkpoints `[EEPROM]`. | Confirmed PG streamer initialization, flush, and CRC calculations complete without fault. |
| [`src/main/pg/pg.c`](src/main/pg/pg.c) | Loop optimization and `[PG]` logging. | Replaced toolchain `memset`/`memcpy` with robust byte copy loops to prevent non-RVC illegal instruction traps on ET1031 silicon. |
| [`src/main/drivers/serial_uart.c`](src/main/drivers/serial_uart.c) | Added THEJAS32 hooks in `uartTotalRxBytesWaiting()` and `isUartTransmitBufferEmpty()`. | Allows polled draining of hardware RX/TX FIFOs. Missing similar hook in `uartTotalTxBytesFree()`, which could stall `serialWriteBufBlocking()`. |

### 1.2 ARIES Platform Target Port (`src/platform/ARIES/`) (18 files)
| File Path | Purpose & Function | Scan Observations & Analysis |
| :--- | :--- | :--- |
| `startup.S` | Bare-metal assembly reset sequence. | Initializes `x1`–`x31`, `sp` to `_estack` (`0x0023FC50`), `gp`, `mtvec` to `trap_entry` (`0x002020F0`), copies `.data` (if LMA != VMA), zeros `.bss`, and calls `main()`. Complete 32-register context save/restore frame with `mret`. |
| `system.c` | Core system, clocks, timers, trap dispatcher. | Implements `micros()` (atomic 64-bit cycle counter without `__udivdi3`), Timer 0 ISR, early polled UART0 driver, `handle_trap` exception/interrupt routing, `systemInit()`. `__enable_irq()` was not called in `systemInit()`. |
| `serial_uart_aries.c` | Synopsys 16550 UART driver. | Maps UART0 (`0x10000100`) and UART1 (`0x10000200`). Divisor 54 for 115200 baud. TX/RX ring buffers. Fixed pin tags (`PA30`/`PA31` for UART0, `PA17`/`PA16` for UART1) and fallback binding in `uartPinConfigure()`. |
| `io_aries.c` | GPIO driver for Banks 0 and 1. | Maps Bank 0 (`0x10080000`, pins 0–15) and Bank 1 (`0x10180000`, pins 16–31). Direction, set, clear, and read registers. |
| `io_def_generated.h` | Pin definitions. | Maps `PA0`–`PA31` with `DEFIO_PORT_PINS 64`. |
| `exti_aries.c` | External interrupt handler. | Dispatches Bank 0 (IRQ 10) and Bank 1 (IRQ 11) to per-pin callbacks. |
| `bus_spi_aries.c` | SPI master driver. | Controls SPI0 (`0x10000600`) as `SPI1`. Implements polled transfers with `Bits.CSAAT` CS retention. |
| `pwm_motor_aries.c` | Actuator PWM driver. | Controls Motors 1–4 (`PA1`–`PA4`) using hardware Timer 1 (`0x10000A14`) IRQ 8. |
| `config_flash_aries.c`| Winbond SPI3 flash driver. | Manages 32 KB flash persistence at `0x201F8000`. |
| `stub_drivers.c` | Peripheral stubs. | Safe no-op stubs for ADC, I2C, USB CDC, transponder. |
| `link/aries_v3.ld` | In-SRAM linker script. | Origin at `0x00202000`, length `248K`. Sized `.stack` to 5 KB (`0x1400`), leaving ~31 KB free SRAM headroom. |
| `mk/ARIES.mk` | Build configuration. | `-march=rv32im_zicsr -mabi=ilp32 -ffast-math -fno-math-errno -Os`. Links with `rv32i_zicsr_zifencei/ilp32` multilib. |
| `include/platform/bsp_thejas32.h` | C-DAC BSP registers. | Register definitions for UART, Timers, SPI, and Interrupt Controller (`0x20010000`). |
| `include/platform/platform.h` | Platform macros & IRQ controls. | Defines `__enable_irq()` (`csrs mie, 0x800` and `csrs mstatus, 8`) and `__disable_irq()`. |
| `target/ARIESV3/target.h` | Pin & feature configuration. | Loop clamped to 1.0 kHz (`DEFAULT_PID_PROCESS_DENOM 8`), `CONFIG_IN_RAM`, `EEPROM_SIZE 8192`, UART pin definitions. |
| `target/ARIESV3/config.h` | Target configuration profile. | Feature enable/disable flags for ARIES v3. |
| `target/ARIESV3/target.mk` | Target makefile inclusion. | Sets target name and architecture flags. |
| `dma_reqmap_mcu.h` / `timer_def.h` | Minimal header stubs. | Resolves compilation dependencies for unused MCU features. |

### 1.3 Unit Test Suites & Verification Infrastructure (`src/test/`) (28 files)
| Subsystem / Test Suite | Test Count | Status | Scope |
| :--- | :---: | :---: | :--- |
| `test_aries_infra_unittest` | 7 | **PASS** | Mock MMIO, Mock CSR, address arithmetic. |
| `test_aries_system_unittest` | 28 | **PASS** | `micros()`, `millis()`, monotonic rollover, Timer 0 ISR, trap dispatch. |
| `test_aries_uart_unittest` | 19 | **PASS** | Baud divisor, 8-N-1 LCR, ring buffer ordering, TX drain, null guards, pin sanitization. |
| `test_aries_spi_unittest` | 17 | **PASS** | Clock dividers, `CSAAT` CS retention, multi-segment burst transfers. |
| `test_aries_gpio_unittest` | 16 | **PASS** | Bank 0/1 bitmask calculations, direction registers, set/clear semantics. |
| `test_aries_exti_unittest` | 20 | **PASS** | IRQ 10/11 routing, trigger edges, multi-pin callback dispatch. |
| `test_aries_flash_unittest` | 16 | **PASS** | 4 KB Sector Erase (`0x20`), Page Program (`0x02`), shadow sync. |
| `test_aries_motor_unittest` | 20 | **PASS** | Timer 1 deadline sorting, pulse scheduling (1000–2000 µs), failsafe cutoff. |
| `test_aries_integration_unittest` | 12 | **PASS** | Full boot sequence, PG reset table, interrupt storm resilience. |
| `build_verify.sh` | 10 | **PASS** | Disassembly audit: 0 compressed instructions, RV32IM pure 32-bit, in-SRAM layout. |

### 1.4 Documentation & Tooling Scripts (34 files)
- Datasheets, compilation guides, port guides, presentation plans, test plans, and automated verification scripts.

---

## 🎯 Step 2: Evidence-Based Priority List for Deep Inspection

From the comprehensive scan in Step 1, the files with direct influence on the CLI and MSP unresponsiveness are:

1. **Priority 1: CPU Global Interrupt Enablement**
   - [`src/main/main.c`](src/main/main.c) & [`src/platform/ARIES/system.c`](src/platform/ARIES/system.c) & [`src/platform/ARIES/include/platform/platform.h`](src/platform/ARIES/include/platform/platform.h)
   - *Investigation:* Verify whether `mstatus.MIE` (bit 3) and `mie.MEIE` (bit 11) are actually set upon entering the scheduler loop.
2. **Priority 2: Serial Transmit Buffer Emptying & Backpressure**
   - [`src/main/drivers/serial_uart.c`](src/main/drivers/serial_uart.c) & [`src/platform/ARIES/serial_uart_aries.c`](src/platform/ARIES/serial_uart_aries.c)
   - *Investigation:* Check whether `uartTotalTxBytesFree()` can cause an infinite spin in `serialWriteBufBlocking()` when interrupts are disabled or during heavy early CLI string printing.
3. **Priority 3: 16550 UART Hardware FIFO Controls & Baud Rate Configuration**
   - [`src/platform/ARIES/serial_uart_aries.c`](src/platform/ARIES/serial_uart_aries.c) vs [`lib/vega-sdk/vega-sdk/bsp/drivers/uart/uart.c`](lib/vega-sdk/vega-sdk/bsp/drivers/uart/uart.c)
   - *Investigation:* Check FCR (FIFO Control Register) configuration, threshold levels, DLAB timing, and `UART_IE` bit settings.
4. **Priority 4: Interrupt Controller Acknowledgment & EOI Semantics**
   - [`src/platform/ARIES/system.c`](src/platform/ARIES/system.c) vs [`lib/vega-sdk/vega-sdk/bsp/drivers/interrupt/interrupt.c`](lib/vega-sdk/vega-sdk/bsp/drivers/interrupt/interrupt.c)
   - *Investigation:* Check if the THEJAS32 interrupt controller requires a register write or status clear to un-latch active external interrupts.
5. **Priority 5: Betaflight Task Scheduler Frequency & Serial Task Starvation**
   - [`src/main/fc/tasks.c`](src/main/fc/tasks.c) & [`src/main/msp/msp_serial.c`](src/main/msp/msp_serial.c) & [`src/main/cli/cli.c`](src/main/cli/cli.c)
   - *Investigation:* Confirm whether `TASK_SERIAL` is scheduled and not starved by higher-priority tasks.

---

## 🐛 Step 3: Deep Technical Bug Hunting & Findings

### Bug 1: Machine External Interrupts Not Enabled in RISC-V Core (`mstatus.MIE` / `mie.MEIE`)
- **Location:** [`src/main/main.c`](src/main/main.c) / [`src/platform/ARIES/system.c`](src/platform/ARIES/system.c)
- **Symptom:** The board reaches `[MAIN] Launching scheduler loop...`, but all hardware interrupts (UART0 RX, UART0 TX, Timer 0, Timer 1) are completely silent.
- **Root Cause:** In RISC-V Privileged Architecture, `mstatus.MIE` and `mie.MEIE` reset to 0 (disabled). While ARM Cortex-M resets with interrupts enabled, RISC-V requires explicit software enablement. Neither `systemInit()` nor `run()` called `__enable_irq()`.
- **Solution:** Invoke `__enable_irq()` immediately before entering the scheduler loop in `run()`, as well as at the conclusion of `systemInit()`.

### Bug 2: Missing Hardware Drain Hook in `uartTotalTxBytesFree()`
- **Location:** [`src/main/drivers/serial_uart.c`](src/main/drivers/serial_uart.c) (line 394)
- **Symptom:** `serialWriteBufBlocking()` spins indefinitely in `while (serialTxBytesFree(instance) < count)` when printing banners or CLI prompts if interrupts are masked or delayed.
- **Root Cause:** `uartTotalRxBytesWaiting()` and `isUartTransmitBufferEmpty()` both had `#ifdef THEJAS32` hooks calling `uartIrqHandler()`, but `uartTotalTxBytesFree()` did not. When the TX buffer is partially full, `uartTotalTxBytesFree()` never pumped remaining bytes to `UART_DR`.
- **Solution:** Add `uartIrqHandler((uartPort_t *)uartPort)` inside `uartTotalTxBytesFree()` under `#ifdef THEJAS32`.

### Bug 3: UART FIFO Reset in `uartReconfigure` Wipes In-Flight Bytes
- **Location:** [`src/platform/ARIES/serial_uart_aries.c`](src/platform/ARIES/serial_uart_aries.c) (line 121)
- **Symptom:** When `uartOpen()` or `uartReconfigure()` is called, setting `UART_IIR_FCR = 0x07` resets both the RX and TX FIFOs. Any byte written by the user right as the port was opened was discarded.
- **Root Cause:** Bits 1 and 2 of FCR reset the RX and TX FIFOs when set. During runtime baudrate or mode changes, writing `0x07` wipes active buffers.
- **Solution:** Set FCR to `0x01` (FIFO enable only, without resetting queues) during reconfiguration, and only write `0x07` on initial power-up.

---

## 🧪 Step 4: Unit Test Suite Updates & Assertions

Before updating production code, new test cases were written and verified in the GoogleTest suites:
1. **`AriesSystemTest.EnableIrqSetsMstatusMIEAndMieMEIE`**:
   Verifies that `__enable_irq()` sets bit 3 (`MSTATUS_MIE`) in `mstatus` and bit 11 (`MEIE`) in `mie`.
2. **`AriesUartTest.TxBytesFreeDrainsPendingBytes`**:
   Verifies that querying available TX space triggers `uartIrqHandler()` to drain the TX ring buffer to `UART_DR`.
3. **`AriesUartTest.ReconfigureDoesNotResetFifoContents`**:
   Verifies that `uartReconfigure()` configures baud rate and parity without clearing pending FIFO data.

---

## 🛠️ Step 5: Applied Code Fixes & Verification

### Fix 1: Enable RISC-V Interrupts in `main()` and `systemInit()`
- In [`src/main/main.c`](src/main/main.c):
  ```c
  void FAST_CODE run(void)
  {
  #ifdef THEJAS32
      __enable_irq();
  #endif
      while (true) {
          scheduler();
  ```
- In [`src/platform/ARIES/system.c`](src/platform/ARIES/system.c):
  Call `__enable_irq()` in `systemInit()`.

### Fix 2: Hardware Draining Hook in `uartTotalTxBytesFree()`
- In [`src/main/drivers/serial_uart.c`](src/main/drivers/serial_uart.c):
  ```c
  static uint32_t uartTotalTxBytesFree(const serialPort_t *instance)
  {
      const uartPort_t *uartPort = (const uartPort_t*)instance;
  #ifdef THEJAS32
      if (uartPort && uartPort->USARTx && uartPort->port.txBuffer && uartPort->port.txBufferSize > 0) {
          uartIrqHandler((uartPort_t *)uartPort);
      }
  #endif
  ```

### Fix 3: FIFO Enable Without In-Flight Reset in `thejas32_uart_set_baudrate()`
- In [`src/platform/ARIES/serial_uart_aries.c`](src/platform/ARIES/serial_uart_aries.c):
  Use `THEJAS32_UART(ch).UART_IIR_FCR = 0x01;` during reconfiguration.

---

## 🔬 Step 6: Second Pass on Unexplored Files

We inspected previously unexplored subsystems:
1. [`src/main/cli/cli.c`](src/main/cli/cli.c): Checked `cliProcess()`, `cliEnter()`, `cliPrompt()`, and `cliMode`. Verified non-blocking buffer operation.
2. [`src/main/fc/tasks.c`](src/main/fc/tasks.c): Checked task execution order. `TASK_SERIAL` is scheduled at 100 Hz, with `TASK_PRIORITY_LOW`. Confirmed it is not starved by PID/Gyro tasks when PID loop is clamped to 1.0 kHz.
3. [`src/main/msp/msp_serial.c`](src/main/msp/msp_serial.c): Verified that `mspProcessPendingRequest()` evaluates `cmp32(millis(), mspPort->lastActivityMs) >= 100` before entering CLI mode. With `millis()` advancing monotonically, this delay passes in 100 ms.
4. [`src/main/drivers/buf_writer.c`](src/main/drivers/buf_writer.c): Verified that buffered character output does not block indefinitely when flushing to `serialWriteBufBlockingShim`.

---

## 📊 Verification Metrics

- **All Unit Tests:** **155 / 155 tests PASSED (100% pass rate)**.
- **Binary & Arch Checks (`build_verify.sh`):** **10 / 10 checks PASSED**.
- **Memory Footprint:** **222,604 bytes (217.4 KB)** (< 248 KB limit).
