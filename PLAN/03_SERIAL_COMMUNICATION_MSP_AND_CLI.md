# Domain Plan 03: Serial Communication, MSP & CLI

> **Component Plan ID**: `PLAN-DOM-03`  
> **Parent Architecture Document**: [`PLAN/00_MASTER_PLAN_OVERVIEW.md`](file:///home/ivan/Desktop/betaflight/PLAN/00_MASTER_PLAN_OVERVIEW.md)  
> **Primary Knowledge References**:
> - [`knowledgebase/codebase_evaluations/06_EVAL_SERIAL_UART_ARIES_C.md`](file:///home/ivan/Desktop/betaflight/knowledgebase/codebase_evaluations/06_EVAL_SERIAL_UART_ARIES_C.md)
> - [`knowledgebase/codebase_evaluations/15_EVAL_CORE_DIFFS_MAIN_CONFIG_PG_MSP.md`](file:///home/ivan/Desktop/betaflight/knowledgebase/codebase_evaluations/15_EVAL_CORE_DIFFS_MAIN_CONFIG_PG_MSP.md)
> - [`knowledgebase/components/03_SERIAL_UART_AND_MSP.md`](file:///home/ivan/Desktop/betaflight/knowledgebase/components/03_SERIAL_UART_AND_MSP.md)
> - [`knowledgebase/MSP_CONFIGURATOR_HANDSHAKE_GRAPH.md`](file:///home/ivan/Desktop/betaflight/knowledgebase/MSP_CONFIGURATOR_HANDSHAKE_GRAPH.md)
> - [`knowledgebase/CRITICAL_KNOWLEDGE_GAPS_AND_ASSUMPTIONS.md`](file:///home/ivan/Desktop/betaflight/knowledgebase/CRITICAL_KNOWLEDGE_GAPS_AND_ASSUMPTIONS.md)

---

## 1. Executive Domain Summary

The serial communication subsystem provides the life-line telemetry, configuration, and control links on C-DAC ARIES v3.0:
1. **Primary USB-UART Link (`UART0` @ `0x10000100`)**: Hardwired to the onboard Silicon Labs CP2102N USB-UART bridge on pins `PA30` (TX) and `PA31` (RX), running at 115,200 baud (Divisor 54, $+0.47\%$ error) for Betaflight Configurator MSP handshakes and serial CLI interaction.
2. **Serial Receiver Link (`UART1` @ `0x10000200`)**: Dedicated to non-inverted RC serial protocols (e.g. ExpressLRS / CRSF on `PA16`/`PA17`) running at 420,000 baud (Divisor 15, $-0.79\%$ error).
3. **16550 Hardware IP Adaptation**:
   - Leverages 16-byte internal hardware FIFOs (`FCR = 0x07`).
   - Suppresses 16550 level-triggered THRE interrupts (`IER = 0x01` RX-only) to prevent catastrophic CPU interrupt storms.
   - Implements bounded burst FIFO draining (max 16 bytes per call) to prevent blocking the 1.0 kHz flight loop during large MSP responses (e.g., 200-byte packets that would otherwise stall execution for $> 17\text{ ms}$).
4. **Enhanced CLI Usability & Upstream Isolation**:
   - Enables immediate CLI activation on Enter (`\r` or `\n`) or `#` without the fragile 100 ms guard-time requirement.
   - Encapsulates target logging within clean platform hooks rather than scattering `#ifdef THEJAS32` macros across upstream source files.

---

## 2. Microcomponent Decomposition

```
PLAN/03_SERIAL_COMMUNICATION_MSP_AND_CLI.md
 ├── Microcomponent 03_A: 16550 APB UART Hardware Driver & Clock Baud Engine (UART0 & UART1)
 ├── Microcomponent 03_B: Bounded Synchronous FIFO Transmit Drain & Anti-Starvation Engine
 ├── Microcomponent 03_C: Zero-Allocation RX Interrupt Ring Buffer & Overrun Error Recovery
 ├── Microcomponent 03_D: Zero-Allocation MSP Protocol State Machine & 7-Step Handshake Engine
 └── Microcomponent 03_E: High-Usability Serial CLI Engine & Clean Platform Isolation Hooks
```

---

### Microcomponent 03_A: 16550 APB UART Hardware Driver & Clock Baud Engine (UART0 & UART1)

#### 1. Why (Silicon Necessity & Architectural Traceability)
* **The 16550 Industry Standard IP**: Unlike STM32 (proprietary USART) or ESP32 (custom UART controller with GPIO matrix), the THEJAS32 SoC implements standard 16550-compatible APB UART peripherals.
* **Baud Rate Clocks**: THEJAS32 operates on a uniform 100 MHz system clock ($100{,}000{,}000\,\text{Hz}$). The baud rate generator relies on the 16-bit Divisor Latch (`DLL` and `DLM`) accessed when `LCR[7]` (`DLAB`) is asserted:
  $$\text{Divisor} = \frac{f_{\text{in}}}{16 \times \text{BaudRate}}$$
* **Physical Pin Mapping**:
  - `UART0`: Hardwired to CP2102N on `PA30` (TX) and `PA31` (RX).
  - `UART1`: Header pins `PA16` (TX) and `PA17` (RX).
* **Missing Inversion Hardware**: The 16550 IP has no hardware pin inversion. Uninverted protocols (CRSF / ELRS) work out-of-the-box, while inverted protocols (SBUS) require external hardware inverters.

#### 2. How (Detailed Algorithmic & Register Execution)
1. **Peripheral Base Addresses & Register Map**:
   - `UART0_BASE`: `0x10000100` (IRQ 2)
   - `UART1_BASE`: `0x10000200` (IRQ 3)
   - Register offsets (32-bit APB alignment):
     - `+0x00`: `RBR` (read) / `THR` (write) / `DLL` (when DLAB=1)
     - `+0x04`: `IER` (Interrupt Enable) / `DLM` (when DLAB=1)
     - `+0x08`: `IIR` (Interrupt Identification, read) / `FCR` (FIFO Control, write)
     - `+0x0C`: `LCR` (Line Control)
     - `+0x10`: `MCR` (Modem Control)
     - `+0x14`: `LSR` (Line Status)
2. **Baud Rate Calculation & DLAB Configuration**:
   - Formula:
     ```c
     uint32_t divisor = (100000000UL) / (16UL * baudRate);
     ```
   - For 115,200 baud: $\text{Divisor} = 54$ ($115,740.7\text{ bps}$, $+0.47\%$ error).
   - For 420,000 baud: $\text{Divisor} = 15$ ($416,666.7\text{ bps}$, $-0.79\%$ error).
   - Sequence:
     1. Read `LCR`, assert `LCR_DLAB` (`0x80`).
     2. Write `DLL = (uint8_t)(divisor & 0xFF)`.
     3. Write `DLM = (uint8_t)((divisor >> 8) & 0xFF)`.
     4. Clear `LCR_DLAB`, writing framing configuration (`0x03` for 8 data bits, 1 stop bit, no parity `8N1`).
3. **16-Byte Hardware FIFO Activation**:
   - Write `FCR = 0x07` (Bit 0: FIFO Enable, Bit 1: RX FIFO Reset, Bit 2: TX FIFO Reset).
   - Sets 16-byte hardware depth for both TX and RX buffers.
4. **RX-Only Interrupt Assertion**:
   - Write `IER = 0x01` (Enable Received Data Available Interrupt).
   - Explicitly clear bit 1 (`ETBEI` - Transmitter Holding Register Empty Interrupt).
5. **Driver VTable Binding**:
   - Bind to Betaflight `serialPort_t` virtual function table (`serialWrite`, `serialRead`, `serialRxBytesWaiting`, `serialTxBytesFree`, `setBaudRate`).

#### 3. Expected Inputs
* **Baud Rates**: 115200 (Configurator / CLI) or 420000 (CRSF/ELRS).
* **Port Identifiers**: `UART0` or `UART1`.
* **System Clock**: Verified 100.0 MHz.

#### 4. Expected Results
* Baud rate configured with $< 1.0\%$ error.
* 16-byte hardware FIFOs enabled and operational.
* RX interrupts enabled; TX interrupts strictly suppressed.
* VTable attached to Betaflight serial port registry.

---

### Microcomponent 03_B: Bounded Synchronous FIFO Transmit Drain & Anti-Starvation Engine

#### 1. Why (Silicon Necessity & Architectural Traceability)
* **The 16550 THRE Storm Trap**: Standard 16550 UARTs use level-triggered Transmit Holding Register Empty (`THRE`) interrupts. When the TX buffer is empty, enabling `THRE` interrupts causes the ISR to fire continuously, consuming $100\%$ CPU time and starving flight tasks.
* **The Blocking Drain Bug**: In [`src/platform/ARIES/serial_uart_aries.c`](file:///home/ivan/Desktop/betaflight/src/platform/ARIES/serial_uart_aries.c#L175-L181), earlier code resolved this by disabling TX interrupts and executing a synchronous `while(tail != head)` loop that waited for `LSR_THRE`.
* **The Flight Loop Stall**: At 115,200 baud, each serial byte requires $86.8\,\mu\text{s}$. A typical MSP response frame (such as `MSP_BOXIDS` or `MSP_STATUS`) ranges from 100 to 200 bytes. Draining 200 bytes synchronously stalls the CPU for:
  $$200 \times 86.8\,\mu\text{s} = 17.36\text{ ms}$$
  In a 1.0 kHz flight loop ($1000\,\mu\text{s}$ period), an unbuffered 17 ms stall causes 17 dropped PID cycles, triggering failsafe and motor cutoffs.
* **The Solution**: A **Bounded FIFO Burst Drain** that transfers up to 16 bytes (the hardware FIFO depth) into `THR` only while `LSR_THRE` is active, and immediately returns to the scheduler.

#### 2. How (Detailed Algorithmic & Timing Implementation)
1. **Ring-Buffered Software Transmit Buffer**:
   - Allocate software `txRingBuffer[256]` per active UART.
   - Applications (`msp.c`, `cli.c`) write bytes into `txRingBuffer` via `uartWrite()`.
2. **Bounded Burst Drain Routine (`uartEnableTxInterrupt` / `uartDrainTxQueue`)**:
   - Instead of spinning until the entire ring buffer is empty:
     ```c
     void uartDrainTxQueue(uartPort_t *s) {
         uint8_t bytesWritten = 0;
         // Read Line Status Register
         while ((s->port.txBufferTail != s->port.txBufferHead) && 
                (regRead(s->base + UART_LSR) & UART_LSR_THRE) && 
                (bytesWritten < 16)) {
             regWrite(s->base + UART_THR, s->port.txBuffer[s->port.txBufferTail]);
             s->port.txBufferTail = (s->port.txBufferTail + 1) % s->port.txBufferSize;
             bytesWritten++;
         }
     }
     ```
   - Maximum execution time: Writing 16 bytes to the hardware FIFO while `LSR_THRE` is asserted takes $< 0.8\,\mu\text{s}$ (bus access cycles only).
   - If the software ring buffer still has remaining bytes, they will be drained in subsequent calls during `TASK_SERIAL` execution or periodic scheduler cycles.

#### 3. Expected Inputs
* **Software TX Queue**: Pending outgoing bytes in `s->port.txBuffer`.
* **Hardware State**: `LSR` bit 5 (`UART_LSR_THRE`).

#### 4. Expected Results
* Maximum blocking time during TX drain strictly $\le 2.0\,\mu\text{s}$.
* Zero 1.0 kHz flight loop deadline misses during MSP telemetry bursts.
* CP2102N USB packetizer fed smoothly without buffer starvation.

---

### Microcomponent 03_C: Zero-Allocation RX Interrupt Ring Buffer & Overrun Error Recovery

#### 1. Why (Silicon Necessity & Architectural Traceability)
* **The Overrun Error (`ORE`) Freeze**: If high-priority ISRs (such as SPI IMU or soft-PWM timer) delay the UART handler, incoming bytes fill the 16-byte FIFO and trigger an Overrun Error (`LSR` bit 1). On 16550 IP, reading the `LSR` register automatically clears line status errors (`ORE`, `PE`, `FE`). If `LSR` is not checked and read, error state flags persist.
* **Buffer Wrap-Around Corruption**: In earlier iterations, advancing `rxHead` without checking against `rxTail` resulted in queue corruption when high-speed telemetry packets flooded the receiver.
* **Zero-Allocation Mandate**: No dynamic heap allocation (`malloc`); fixed 256-byte circular ring buffers statically allocated in `.bss`.

#### 2. How (Detailed Algorithmic & ISR Implementation)
1. **Static Buffer Definition**:
   ```c
   #define UART_RX_BUFFER_SIZE 256
   static uint8_t uart0RxBuffer[UART_RX_BUFFER_SIZE];
   ```
2. **Interrupt Service Routine Architecture (`uart0_irq_handler`)**:
   - Serviced via PLIC Interrupt Vector (IRQ 2 for UART0, IRQ 3 for UART1).
   - Execution Sequence:
     1. Read `LSR = regRead(UART0_BASE + UART_LSR)`. (Clears hardware Overrun Error flag).
     2. Loop while `LSR & UART_LSR_DR` (Data Ready, bit 0) is set:
        - Read incoming byte: `uint8_t byte = (uint8_t)(regRead(UART0_BASE + UART_RBR) & 0xFF)`.
        - Calculate next head pointer: `uint16_t nextHead = (rxHead + 1) % UART_RX_BUFFER_SIZE`.
        - **Collision Check**: If `nextHead != rxTail`, store byte into `rxBuffer[rxHead]` and update `rxHead = nextHead`. If full, drop the newest byte to prevent corrupting unread history.
        - Re-read `LSR = regRead(UART0_BASE + UART_LSR)`.
3. **Polled Hardware Fallback in `serialRxBytesWaiting()`**:
   - If Betaflight executes a prolonged critical section (`mstatus.MIE = 0`), incoming bytes queue in the 16-byte hardware FIFO.
   - When higher-level code queries `serialRxBytesWaiting()`, the driver polls `LSR_DR` and empties any residual bytes into the software ring buffer before reporting the count:
     ```c
     uint32_t uartRxBytesWaiting(const serialPort_t *instance) {
         uartPort_t *s = (uartPort_t *)instance;
         // Flush any bytes sitting in hardware FIFO
         while (regRead(s->base + UART_LSR) & UART_LSR_DR) {
             uint8_t byte = (uint8_t)(regRead(s->base + UART_RBR) & 0xFF);
             uint16_t nextHead = (s->port.rxBufferHead + 1) % s->port.rxBufferSize;
             if (nextHead != s->port.rxBufferTail) {
                 s->port.rxBuffer[s->port.rxBufferHead] = byte;
                 s->port.rxBufferHead = nextHead;
             }
         }
         return (s->port.rxBufferHead - s->port.rxBufferTail) & (s->port.rxBufferSize - 1);
     }
     ```

#### 3. Expected Inputs
* **Incoming Serial Stream**: 115200 baud stream on `PA31` (CP2102N).
* **Hardware Interrupt**: PLIC IRQ 2 triggering on `UART_IER_RDA` (bit 0).

#### 4. Expected Results
* 100% byte reception reliability with 0 missed frames.
* Overrun errors automatically unlatched by reading `LSR`.
* Buffer head/tail corruption prevented under all throughput loads.

---

### Microcomponent 03_D: Zero-Allocation MSP Protocol State Machine & 7-Step Handshake Engine

#### 1. Why (Silicon Necessity & Architectural Traceability)
* **The Configurator Handshake**: When connecting to Betaflight Configurator, the host GUI initiates a strict 7-step sequence of MultiWii Serial Protocol (MSP v1/v2) queries.
* **Minimal Footprint**: Supporting all 150+ MSP commands wastes significant code and RAM space. The minimal configuration prototype requires strictly the core 7 handshake commands plus sensor/motor status commands to unlock the GUI dashboard.
* **Framing Standards**:
  - MSP v1: `$M<` (Header, Size, Command ID, Payload, XOR Checksum).
  - MSP v2: `$X<` (Header, Flag, 16-bit Command, 16-bit Size, Payload, CRC8-DVB).

#### 2. How (Detailed Algorithmic & Protocol Implementation)
1. **Stream Parsing State Machine (`TASK_SERIAL` / `msp_serial.c`)**:
   - Zero-allocation state machine transitions:
     `IDLE` $\to$ `HEADER_START ('$')` $\to$ `HEADER_M / HEADER_X` $\to$ `HEADER_ARROW ('<')` $\to$ `PAYLOAD_SIZE` $\to$ `COMMAND` $\to$ `PAYLOAD` $\to$ `CHECKSUM`.
2. **The 7 Mandatory Configurator Handshake Commands**:
   1. `MSP_API_VERSION` (ID 1): Responds with `protocol=1`, `apiMajor=1`, `apiMinor=45`.
   2. `MSP_FC_VARIANT` (ID 2): Responds with `"BTFL"` (Betaflight identifier).
   3. `MSP_FC_VERSION` (ID 3): Responds with `major=4`, `minor=5`, `patch=0`.
   4. `MSP_BOARD_INFO` (ID 4): Responds with `boardIdentifier="ARV3"`, `hwRevision=0`, `targetName="ARIESV3"`.
   5. `MSP_BUILD_INFO` (ID 5): Responds with build timestamp and git commit identifier.
   6. `MSP_UID` (ID 160): Responds with 12 bytes of unique silicon ID (derived from SoC hardware registers or static signature).
   7. `MSP_STATUS` (ID 101): Responds with `cycleTime` ($1000\,\mu\text{s}$), `i2cErrors=0`, `sensorsPresent` (ACC | GYRO), `flightModeFlags`, and `armingDisableFlags`.
3. **Response Frame Assembly via `sbuf_t`**:
   - Build responses into static output stream buffer (`sbuf_t`).
   - Automatically append calculated XOR checksum (v1) or CRC8-DVB (v2).
   - Hand buffer over to `uartDrainTxQueue()` for bounded burst transmission.

#### 3. Expected Inputs
* **Serial Requests**: Packets arriving over `UART0` from Configurator.
* **Internal State**: Flight status, cycle time ($1000\,\mu\text{s}$), board metadata.

#### 4. Expected Results
* All 7 handshake commands negotiate successfully in $< 200\text{ ms}$.
* Betaflight Configurator transitions from "Connecting..." to green **"Connected"** state.
* Model visualization and status tabs unlock and respond interactively.

---

### Microcomponent 03_E: High-Usability Serial CLI Engine & Clean Platform Isolation Hooks

#### 1. Why (Silicon Necessity & Architectural Traceability)
* **The CLI Activation Friction**: Standard upstream Betaflight requires three consecutive `#` characters within a 100 ms time window with preceding silence to enter the CLI terminal. From manual serial terminals (e.g. `picocom`, `minicom`, `screen`), users frequently fail to trigger CLI mode.
* **Codebase Cleanliness**: In previous iterations, upstream files ([`init.c`](file:///home/ivan/Desktop/betaflight/src/main/fc/init.c), [`config.c`](file:///home/ivan/Desktop/betaflight/src/main/config/config.c), [`msp_serial.c`](file:///home/ivan/Desktop/betaflight/src/main/msp/msp_serial.c)) were polluted with `#ifdef THEJAS32` and `THEJAS_LOG` calls, creating upstream merge conflicts and bloat.

#### 2. How (Detailed Algorithmic & Code Architecture)
1. **Immediate CLI Activation on Enter (`\r` / `\n`) or `#`**:
   - In [`src/main/msp/msp_serial.c`](file:///home/ivan/Desktop/betaflight/src/main/msp/msp_serial.c):
     ```c
     if (c == '#' || c == '\r' || c == '\n') {
         // Activate CLI immediately without requiring 100ms guard time
         cliEnter(s);
     }
     ```
   - Hitting Enter in any terminal emulator instantly outputs:
     ```text
     # Entering CLI Mode, type 'exit' to return, or 'help'
     # Betaflight / ARIESV3 (ARV3) 4.5.0
     # 
     ```
2. **Encapsulated Platform Hooks & Conditional Debug Flags**:
   - Remove intrusive `#ifdef THEJAS32` blocks from `src/main/fc/init.c` and `src/main/config/config.c`.
   - Provide standard platform initialization hooks:
     `void platformEarlyInit(void);`
     `void platformPostInit(void);`
   - Implement them in `src/platform/ARIES/system.c`.
   - Define a single compile-time debug flag in `platform.h`:
     ```c
     #if defined(DEBUG_THEJAS_LOG)
         #define THEJAS_LOG(fmt, ...) serialPrint(UART0, fmt, ##__VA_ARGS__)
     #else
         #define THEJAS_LOG(fmt, ...) ((void)0)
     #endif
     ```
   - In production builds, `DEBUG_THEJAS_LOG` is disabled, eliminating 100% of debug string literals from SRAM.

#### 3. Expected Inputs
* **Keystrokes**: `\r`, `\n`, or `#` over `UART0` terminal session.
* **Configuration**: `DEBUG_THEJAS_LOG` toggle in `platform.h`.

#### 4. Expected Results
* Instantaneous CLI mode entry with zero typing friction.
* Standard CLI commands (`status`, `tasks`, `version`, `dump`, `exit`) execute flawlessly.
* Clean separation between upstream Betaflight core and ARIES platform code.

---

## 3. Subsystem Traceability Matrix

| Microcomponent ID | Functionality / Feature | Target Source Files | Silicon Verification Target |
| :--- | :--- | :--- | :--- |
| **03_A** | 16550 APB UART Driver & Baud Engine | `src/platform/ARIES/serial_uart_aries.c` | Divisor 54 (115200 baud, $+0.47\%$ error) and Divisor 15 (420000 baud, $-0.79\%$ error). |
| **03_B** | Bounded Synchronous FIFO Transmit Drain | `src/platform/ARIES/serial_uart_aries.c` | Max 16 bytes drained per call; TX blocking time strictly $< 2.0\,\mu\text{s}$. |
| **03_C** | Zero-Allocation RX Ring Buffer & LSR Clear | `src/platform/ARIES/serial_uart_aries.c` | Reading `LSR` clears `ORE`; circular buffer head/tail collision protected. |
| **03_D** | Zero-Allocation MSP State Machine & Handshake | `src/main/msp/msp_serial.c`, `msp.c` | Successful 7-step handshake; Configurator unlocks in $< 200\text{ ms}$. |
| **03_E** | High-Usability CLI & Platform Hooks | `src/main/msp/msp_serial.c`, `platform.h` | Immediate Enter (`\r`) CLI activation; 0 debug strings in production SRAM. |

---

## 4. Anti-Assumption Guardrails

1. **No Inversion Hardware on 16550 IP**:
   - *Never* assume UART0 or UART1 can invert signals via internal register flags. SBUS requires an external inverter; CRSF / ELRS is natively uninverted.
2. **Never Enable 16550 THRE Level-Triggered Interrupts**:
   - *Never* set `IER` bit 1 (`ETBEI`). The level-triggered empty state will swamp the CPU in an interrupt storm. Always use bounded FIFO burst draining.
3. **No Unbounded Transmit Loops**:
   - *Never* spin waiting for a multi-byte software buffer to drain. Always clamp to 16 bytes per cycle to preserve the 1.0 kHz flight loop deadline.
4. **No Direct Flash Access for Serial Parameters**:
   - *Never* attempt to write serial port configurations directly to SPI flash. All configuration resides in the 8 KB RAM parameter buffer (`CONFIG_IN_RAM`).