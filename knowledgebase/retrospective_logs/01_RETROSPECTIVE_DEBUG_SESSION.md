# Retrospective Log: `oldfiles/logs/DEBUG_SESSION.md`
**File Inspected**: `oldfiles/logs/DEBUG_SESSION.md`  
**Focus**: Deep analysis of past debugging decisions, code modifications, brilliant discoveries, critical planning errors, and scores (0–10).

---

## 1. File Overview & Context
`DEBUG_SESSION.md` documents an intensive debugging effort across 86 modified files attempting to solve serial communication unresponsiveness (CLI and Betaflight Configurator MSP timeouts), RISC-V interrupt silencing, and scheduler starvation on the ARIES v3.0 board.

---

## 2. Decision & Action Scoring (0 to 10)

| Decision / Action Taken | Score (0-10) | Category | Technical Analysis & Rationale |
| :--- | :---: | :---: | :--- |
| **Diagnosing RISC-V Core Interrupt Silencing (`mstatus.MIE` & `mie.MEIE`)** | **9.5 / 10** | **Brilliant** | Unlike ARM Cortex-M which resets with global interrupts enabled, RISC-V resets with `mstatus.MIE = 0` and `mie.MEIE = 0`. Catching that `systemInit()` and `run()` never called `__enable_irq()` was a critical, foundational breakthrough. |
| **Discovering C-DAC's Synchronous TX Pattern in `lib/vega-bsp`** | **9.5 / 10** | **Brilliant** | Recognizing that official C-DAC BSPs explicitly disabled 16550 TX interrupts (`UART_IE = 0x01` RX only) and used bounded synchronous draining. Adopting this completely resolved the 16550 THRE interrupt storm and unlatched the scheduler. |
| **Test-Driven Unit Test Suite Expansion (161 Unit Tests)** | **8.5 / 10** | **Brilliant** | Locking in bug fixes with automated GoogleTest cases (`AriesUartTest.IrqHandlerReadsIir...`, `UartReadDrainsHardwareFifo...`) prevented regressions and ensured deterministic logic verification. |
| **Pruning Unused Gyro Drivers & Features to Expand Stack to 8 KB** | **8.0 / 10** | **Good** | Pruning 15 unused gyro sensor drivers (e.g. MPU6050, LSM6DSO) and non-essential telemetry protocols reclaimed ~12.2 KB of code space, enabling the stack to expand from 5 KB to 8 KB. |
| **Injecting Polling Hooks into Generic Betaflight Core (`serial_uart.c`)** | **2.0 / 10** | **Stupid** | Adding `#ifdef THEJAS32` blocks inside generic core functions like `uartTotalTxBytesFree()` that called `uartIrqHandler()`. This mutated buffers and read hardware `IIR` inside supposedly `const` query functions, breaking architectural layering and introducing re-entrancy bugs! |
| **Direct ASCII Debug String Printing onto Active Telemetry UART** | **1.0 / 10** | **Catastrophic** | Injecting `[SERIAL ALIVE]` in `tasks.c` and `[MSP RX]` in `msp_serial.c` directly onto UART0. Betaflight Configurator expects pure binary MSP frames (`$M>...`). Flooding the stream with ASCII debug text broke framing and caused Configurator to time out after 10 seconds! |
| **Enabling 16550 THRE Interrupts Without Synchronization** | **2.0 / 10** | **Stupid** | Attempting to use edge-triggered Transmitter Holding Register Empty (`THRE`) interrupts on the 16550 IP. Reading `IIR` during RX checks acknowledged THRE prematurely, causing competition between the ISR and `uartEnableTxInterrupt()`, leading to an interrupt storm on IRQ 0 and freezing serial output. |
| **Targeting ~210 KB Binary on a 250 KB SRAM Chip** | **4.0 / 10** | **Poor Planning** | Even after pruning, the team targeted a 210 KB binary image, leaving only ~11 KB of free SRAM headroom (<5% margin). A minor increase in buffer sizes or stack depth risked immediate out-of-memory corruption. |

---

## 3. Deep-Dive: Brilliant Insights vs. Stupid Mistakes

### 🌟 The Brilliant Parts

1. **The RISC-V Privileged Interrupt Realization**:
   The team correctly diagnosed why the board was reaching `[MAIN] Launching scheduler loop...` but completely failing to receive UART characters. In ARM Cortex-M, interrupts are unmasked by default at boot. In RISC-V, Machine External Interrupts (`mie.MEIE`, bit 11) and Global Machine Interrupts (`mstatus.MIE`, bit 3) are hard-reset to `0`. Manually setting these CSRs via `__enable_irq()` was the key that unlocked hardware interrupts.

2. **Deconstructing the 16550 DW_apb_uart Edge Quirks**:
   The team dug into the official C-DAC BSP (`lib/vega-bsp/cores/arduino/HardwareSerial.cpp`) and identified that Synopsys DW_apb_uart 16550 IP clears its THRE interrupt whenever `UART_IIR_FCR` is read. Because `uartIrqHandler` was reading `IIR` during reception, it inadvertently wiped pending transmit interrupts. Switching to pure RX interrupts with bounded synchronous TX writes permanently fixed the serial lockup.

### ⚠️ The Stupid Parts

1. **Polluting Binary Telemetry Wire with ASCII Strings**:
   Logging human-readable debug prints (`[SERIAL ALIVE] #...` and `[MSP RX] c=...`) directly out of the primary serial port while Betaflight Configurator is connected. The Configurator state machine treats anything other than `$M` or `$X` as frame corruption. This single debugging mistake caused the "no configuration received in 10 seconds" failure.

2. **Contaminating Generic Betaflight Files with Platform Hacks**:
   Rather than confining platform behavior to `src/platform/ARIES/serial_uart_aries.c`, the team modified `src/main/drivers/serial_uart.c` with `#ifdef THEJAS32` blocks. Calling `uartIrqHandler()` from `uartTotalTxBytesFree()` caused stateful hardware register reads inside getter functions.

---

## 4. Key Takeaways for Current Rebuild

1. **Never Output ASCII Debug Logs on the Active MSP Port**: All MSP communication on UART0 must remain 100% pure binary. Debug status must use an auxiliary port, onboard LEDs, or memory buffers.
2. **Confine Hardware Logic Strictly to `src/platform/THEJAS32/`**: Zero `#ifdef THEJAS32` modifications inside `src/main/drivers/serial_uart.c` or core Betaflight files.
3. **Use Synchronous Bounded TX Drain for 16550 IP**: Do not enable 16550 THRE interrupts (`UART_IE = 0x01` for RX only). The CP2102N USB bridge accepts byte bursts instantly into its hardware FIFO; synchronous draining takes $<0.1\,\mu\text{s}$ per byte and avoids interrupt storms.
4. **Target <128 KB Footprint, Not 210 KB**: A 72–85 KB footprint leaves >170 KB of SRAM free, eliminating all memory pressure.
