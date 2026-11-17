# Hardware Inspection Log: `src/platform/common/stm32/serial_uart_hw.c`

## 1. File Purpose & Role in Architecture
* **File Path**: `src/platform/common/stm32/serial_uart_hw.c`
* **Subsystem**: Platform UART Hardware Abstraction, GPIO Pin Configuration (Alternate Functions), Inversion Control, NVIC IRQ Configuration, and DMA Stream Channel Allocation.
* **Key Components**:
  * `serialUART()`: Master initialization function opening a physical UART device, attaching ring buffers, configuring GPIO pin Alternate Functions, and starting interrupts/DMA.
  * `enableRxIrq()`: Configures NVIC priority and enables peripheral interrupt channel.
  * `uartConfigureDma()`: Allocates and maps DMA streams/channels for UART TX and RX.
  * `uartEnableTxInterrupt()`: Enables Transmit Data Register Empty (`TXE`) interrupt to drain software ring buffers.

---

## 2. Hardware Challenges STM32 Developers Faced & How They Overcame Them

### Challenge 1: Lack of Hardware Signal Inversion on STM32F4
* **The Problem**: Protocols such as FrSky SBUS and SmartPort use inverted TTL logic (idle LOW, active HIGH). STM32F7, H7, and G4 MCUs include hardware inverter bits in `USART_CR2` (`TXINV`, `RXINV`), but STM32F4 silicon completely lacks hardware UART inversion.
* **How It Was Overcome**:
  1. For normal full-duplex pins on F4, internal pull-ups were strictly enforced (`IOCFG_AF_PP_UP`).
  2. For inverted protocols, Betaflight relied on external dedicated hardware inverters controlled via GPIO:
     ```c
     void uartConfigureExternalPinInversion(uartPort_t *uartPort) {
     #if defined(USE_INVERTER)
         const bool inverted = uartPort->port.options & SERIAL_INVERTED;
         enableInverter(uartPort->port.identifier, inverted);
     #endif
     }
     ```

---

### Challenge 2: Single-Wire Half-Duplex (Bidirectional Telemetry)
* **The Problem**: Telemetry protocols (SmartPort, ESC sensor wire) communicate bidirectionally over a single wire. If two push-pull outputs drive opposite voltages simultaneously, a short circuit occurs.
* **How It Was Overcome**:
  When `SERIAL_BIDIR` is requested in `options`, the TX pin is switched to Open-Drain mode with a pull-up (`GPIO_MODE_AF_OD` / `GPIO_OType_OD`):
  ```c
  if ((options & SERIAL_BIDIR) && txIO) {
      const bool pushPull = serialOptions_pushPull(options);
      const ioConfig_t ioCfg = IO_CONFIG(
          pushPull ? GPIO_OType_PP : GPIO_OType_OD,
          GPIO_PuPd_UP
      );
      IOConfigGPIOAF(txIO, ioCfg, txAf);
  }
  ```

---

### Challenge 3: Alternate Function Pin Multiplexing
* **The Problem**: On STM32, peripheral signals can be routed to multiple different physical pins depending on the board PCB layout (e.g., USART1 TX can be on PA9 or PB6).
* **How It Was Overcome**:
  Betaflight abstracts pins using `IO_t` and `ioTag_t`. Rather than hardcoding pins, `serialUART()` queries the board target configuration for the assigned `tx.pin` and `rx.pin`, finds their respective Alternate Function indices (`txAf`, `rxAf`), and programs `GPIOx->AFR[]` dynamically.

---

## 3. Direct Porting Blueprint for ARIES v3.0 (THEJAS32 / VEGA ET1031)

1. **Non-Inverted Standard TTL for CP2102N**:
   * The primary target port for Betaflight Configurator is `UART0`.
   * On ARIES v3.0, `UART0` is hardwired to the Silicon Labs CP2102N USB bridge (Pins 107/108).
   * It operates on standard non-inverted 3.3V logic at 115,200 baud, completely avoiding the inverter challenges of STM32F4.
2. **16550 UART Driver Model**:
   * STM32 uses proprietary ST USART registers (`SR`/`ISR`, `DR`/`TDR`/`RDR`, `CR1..CR3`).
   * THEJAS32 uses standard 16550/16750 UART IP:
     * Transmit Data / Holding Register (`THR`): `0x10000100`
     * Receive Buffer Register (`RBR`): `0x10000100`
     * Interrupt Enable Register (`IER`): `0x10000104` (Bit 0 = Received Data Available, Bit 1 = Transmitter Holding Register Empty)
     * Line Status Register (`LSR`): `0x10000114` (Bit 0 = Data Ready `DR`, Bit 5 = Transmit Empty `THRE`)
3. **No DMA Required for Configurator Connection**:
   * For the 128KB minimal target, DMA can be bypassed completely. A simple ring buffer drained via `UART0` interrupts or `LSR` polling in `TASK_SERIAL` is more than fast enough for 115200 baud MSP communication.
