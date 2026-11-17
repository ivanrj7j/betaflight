# Hardware Inspection Log: `src/platform/STM32/serial_uart_stm32f4xx.c`

## 1. File Purpose & Role in Architecture
* **File Path**: `src/platform/STM32/serial_uart_stm32f4xx.c`
* **Subsystem**: Low-Level Peripheral Hardware Table, UART Interrupt Handler, Overrun Handling, and TX Pin Line State Monitoring.
* **Key Components**:
  * `uartHardware[UARTDEV_COUNT]`: Static hardware capability table defining base register pointers, pin options, and NVIC channels for USART1–USART6.
  * `uartIrqHandler()`: Core interrupt service routine handling `RXNE` (Receive), `TXE` (Transmit drain), `ORE` (Overrun recovery), and `IDLE` (End-of-frame detection).
  * `checkUsartTxOutput()` / `uartTxMonitor()`: Bus collision avoidance state machine for half-duplex UARTs.

---

## 2. Hardware Challenges STM32 Developers Faced & How They Overcame Them

### Challenge 1: UART Overrun Freeze (`USART_IT_ORE`)
* **The Problem**: If high-speed incoming serial traffic (such as 115200 baud MSP packets or 2 Mbaud CRSF) arrives faster than the CPU handles the interrupt, the hardware sets the Overrun Error bit (`ORE`). On STM32 USART silicon, if the `ORE` interrupt is enabled and not properly cleared, the CPU enters an unrecoverable interrupt loop, freezing the flight controller completely!
* **How It Was Overcome**:
  In `uartIrqHandler()`, developers explicitly clear the `ORE` bit to unblock the hardware receiver immediately:
  ```c
  if (USART_GetITStatus(USARTx, USART_IT_ORE) == SET) {
      USART_ClearITPendingBit(USARTx, USART_IT_ORE);
  }
  ```

---

### Challenge 2: STM32 Hardware IDLE Line Clear Sequence
* **The Problem**: Frame-based serial protocols (e.g., SBUS, CRSF) require detecting when a frame has finished transmitting. STM32 provides an `IDLE` line interrupt. However, clearing the `IDLE` flag requires a specific two-step hardware read sequence: reading `SR` (Status Register) followed by reading `DR` (Data Register). Calling standard clear macros fails to clear the flag.
* **How It Was Overcome**:
  The driver performs the explicit dual-read sequence:
  ```c
  if (USART_GetITStatus(USARTx, USART_IT_IDLE) == SET) {
      if (s->port.idleCallback) {
          s->port.idleCallback();
      }
      // Explicit read sequence to clear hardware IDLE flag
      (void) USARTx->SR;
      (void) USARTx->DR;
  }
  ```

---

### Challenge 3: Half-Duplex Single-Wire Bus Collisions
* **The Problem**: In single-wire shared bus protocols (SmartPort telemetry, ESC telemetry), transmitting while an external sensor or ESC is driving the wire low causes bus contention and frame destruction.
* **How It Was Overcome**:
  A dynamic pin state machine transitions the pin between `TX_PIN_ACTIVE` and `TX_PIN_MONITOR`. When a transmission completes (`USART_IT_TC`), the transmitter is disabled (`USART_CR1_TE` cleared) and the GPIO is reconfigured as an Input with internal pull-up (`IOCFG_IPU`). Before initiating the next transfer, `checkUsartTxOutput()` tests if the line has floated back to HIGH before enabling the transmitter.

---

### Challenge 4: TX Interrupt CPU Spinning
* **The Problem**: The `TXE` (Transmit Data Register Empty) interrupt fires continuously as long as the UART transmit holding register is empty. If software does not disable the `TXE` interrupt when the software ring buffer is empty, the interrupt fires endlessly, consuming 100% of CPU cycles.
* **How It Was Overcome**:
  ```c
  if (!s->txDMAResource && (USART_GetITStatus(USARTx, USART_IT_TXE) == SET)) {
      if (s->port.txBufferTail != s->port.txBufferHead) {
          USART_SendData(USARTx, s->port.txBuffer[s->port.txBufferTail]);
          s->port.txBufferTail = (s->port.txBufferTail + 1) % s->port.txBufferSize;
      } else {
          // Ring buffer drained: immediately disable TXE interrupt!
          USART_ITConfig(USARTx, USART_IT_TXE, DISABLE);
      }
  }
  ```

---

## 3. Direct Porting Blueprint for ARIES v3.0 (THEJAS32 / VEGA ET1031)

1. **Mapping to THEJAS32 16550 UART**:
   * The structure of `uartIrqHandler()` maps directly to the 16550 UART on THEJAS32:
     * Check Interrupt Identification Register (`IIR` at offset `0x08`):
       * `IIR_RX_AVAILABLE` (`0x04`) $\rightarrow$ Read `RBR` (`0x10000100`), append to `rxBuffer`.
       * `IIR_TX_EMPTY` (`0x02`) $\rightarrow$ If bytes remain in `txBuffer`, write to `THR` (`0x10000100`); else disable TX interrupt in `IER` (`0x10000104`).
       * `IIR_LINE_STATUS` (`0x06`) $\rightarrow$ Read `LSR` (`0x10000114`) to clear overrun/framing errors.
2. **Static Ring Buffer Sizing for Configurator**:
   * For `UART0` (CP2102N USB link), define `uart0TxBuffer[256]` and `uart0RxBuffer[256]`.
   * This guarantees sufficient buffer depth for Configurator MSP packet bursts while keeping RAM footprint small.
