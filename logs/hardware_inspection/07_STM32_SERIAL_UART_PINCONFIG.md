# Hardware Inspection Log: `src/platform/common/stm32/serial_uart_pinconfig.c`

## 1. File Purpose & Role in Architecture
* **File Path**: `src/platform/common/stm32/serial_uart_pinconfig.c`
* **Subsystem**: Dynamic Pin-to-Peripheral Mapping and Silicon Pin Swapping.
* **Key Components**:
  * `uartPinConfigure()`: Traverses board-level configuration (`serialPinConfig_t`), matches requested pins against the hardware table (`uartHardware_t`), and detects pin swap options.

---

## 2. Hardware Challenges STM32 Developers Faced & How They Overcame Them

### Challenge: Multiple Alternate Pins & Silicon Pin Swap
* **The Problem**: Flight controller PCB designers route UART signals to different pins across hundreds of commercial FC boards. In addition, some MCUs (STM32F7/H7/G4) support silicon pin swapping (`USART_CR2_SWAP`), which allows the software to invert the roles of the physical TX and RX pads.
* **How It Was Overcome**:
  1. The code compares configured pins (`cfgRx`, `cfgTx`) against the multi-element arrays `hardware->rxPins[]` and `hardware->txPins[]`.
  2. If a cross-match is detected (`cfgTx == hardware->rxPins[pindex].pin`), the driver automatically flags `uartdev->pinSwap = true`, instructing the low-level UART driver to set the hardware `SWAP` register bit on supported MCUs.

---

## 3. Direct Porting Blueprint for ARIES v3.0 (THEJAS32 / VEGA ET1031)

* On ARIES v3.0, the pinout is fixed per UART peripheral:
  * `UART0`: Hardwired to CP2102N USB Bridge (Pins 107/108).
  * `UART1`: Header `J2` (`J2_15` RX / `J2_13` TX).
  * `UART2`: Header `J5` (`J5_1` RX / `J5_2` TX).
* Therefore, the pin configuration table for ARIES v3.0 will have single static entries per UART controller, simplifying `uartPinConfigure()` and avoiding runtime pin search overhead.
