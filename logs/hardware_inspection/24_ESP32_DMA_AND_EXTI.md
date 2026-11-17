# Hardware Inspection Log: `src/platform/ESP32/dma_esp32.c` & `exti_esp32.c`
**Consulted Logs**: [08_STM32_IO_IMPL.md](08_STM32_IO_IMPL.md) & [10_STM32_BUS_SPI_HW.md](10_STM32_BUS_SPI_HW.md)

## 1. File Purpose & Role in Architecture
* **File Paths**:
  * [src/platform/ESP32/dma_esp32.c](../../src/platform/ESP32/dma_esp32.c)
  * [src/platform/ESP32/exti_esp32.c](../../src/platform/ESP32/exti_esp32.c)
* **Subsystem**: General DMA Channel Allocation and External Pin Interrupt (EXTI) Dispatch Engine.
* **Key Components**:
  * `dmaGetFreeIdentifier()` / `dmaSetHandler()`: Dynamic GDMA channel allocation and runtime CPU interrupt binding.
  * `EXTIInit()` / `EXTI_IRQHandler()`: Centralized GPIO interrupt vector decoding and low-latency per-pin callback dispatch using `__builtin_ctz()`.

---

## 2. Comparative Analysis: ESP32 vs. STM32 (`08_STM32_IO_IMPL.md` & `10_STM32_BUS_SPI_HW.md`)

| Feature / Hardware Challenge | STM32 Implementation (`08_STM32_IO_IMPL.md`, `10_STM32_BUS_SPI_HW.md`) | ESP32 Implementation (`dma_esp32.c`, `exti_esp32.c`) |
| :--- | :--- | :--- |
| **Pin Interrupt Multiplexing** | Rigid SYSCFG multiplexer: PA0, PB0, and PC0 share `EXTI0_IRQn`. Simultaneous interrupts on the same pin number across different ports are silicon-impossible. | Completely orthogonal: every GPIO pin (0–48) has an independent interrupt configuration and status flag. |
| **Interrupt Vector Architecture** | Multiple hardware NVIC vectors: `EXTI0`, `EXTI1`, `EXTI2`, `EXTI3`, `EXTI4`, `EXTI9_5`, `EXTI15_10`. | Single centralized CPU interrupt (`ESP32_CPU_INTR_GPIO`) routed from peripheral interrupt matrix (`ETS_GPIO_INTR_SOURCE`). |
| **ISR Dispatch Efficiency** | Multiple ISR entries branching into port-matching routines. | Fast single ISR loop using Count Trailing Zeros (`__builtin_ctz(status)`) to service multiple concurrent pin edges. |
| **DMA Channel Binding** | Fixed silicon routing: peripheral requests mapped to fixed streams/channels with severe collision risks. | Fully flexible GDMA: channels allocated dynamically from pool (`dmaGetFreeIdentifier`) and bound to any peripheral. |
| **DMA Interrupt Routing** | Fixed NVIC IRQ lines per DMA stream. | Dynamic interrupt line routing from CPU interrupt pool (`gdmaCpuIntrPool`). |

---

## 3. Hardware Challenges ESP32 Developers Faced & How They Overcame Them

### Challenge 1: Single-Vector GPIO Interrupt Contention Across 48 Pins
* **The Problem**: Unlike STM32 which provides dedicated vectors for pins 0 through 4, ESP32 routes all pin interrupts through a single shared interrupt line. If multiple pins transition simultaneously (e.g. Gyro Data Ready + Barometer Data Ready + RC Receiver pulse), a naive handler might miss edges or suffer excessive dispatch latency.
* **How It Was Overcome**:
  `EXTI_IRQHandler()` scans the 32-bit status register in a tight loop using compiler intrinsics:
  ```c
  gpio_ll_get_intr_status(&GPIO, 0, &status);
  while (status) {
      uint32_t pin = __builtin_ctz(status);
      status &= ~(1U << pin);
      gpio_ll_clear_intr_status(&GPIO, (1U << pin));
      if (pin < DEFIO_USED_COUNT && extiChannelRecs[pin].handler && extiChannelRecs[pin].handler->fn) {
          extiChannelRecs[pin].handler->fn(extiChannelRecs[pin].handler);
      }
  }
  ```
  `__builtin_ctz()` resolves the lowest asserted pin in 1 CPU cycle, immediately clears the status bit to prevent missing back-to-back edges, and dispatches the callback without any conditional branching ladders.

### Challenge 2: Dynamic CPU Interrupt Line Exhaustion
* **The Problem**: The ESP32 CPU interrupt controller has 32 interrupt levels, but many are claimed by the operating system, Wi-Fi MAC, and timer peripherals. Binding each GDMA channel permanently to a fixed CPU interrupt level would exhaust available interrupt lines.
* **How It Was Overcome**:
  `dma_esp32.c` maintains a small pool of free CPU interrupt levels (`gdmaCpuIntrPool[]`). An interrupt line is allocated dynamically only when a subsystem explicitly calls `dmaSetHandler()`.

---

## 4. Key Takeaways & Porting Blueprint for ARIES v3.0 (THEJAS32 / VEGA ET1031)

1. **Adopt ESP32's `__builtin_ctz()` Dispatch for ARIES v3.0 EXTI**:
   * On THEJAS32, GPIO pins `0` through `31` generate external interrupts through the core PLIC (Platform-Level Interrupt Controller).
   * Instead of copying STM32's rigid multi-vector SYSCFG approach, we can directly adopt ESP32's elegant centralized GPIO ISR:
     * Read the 32-bit GPIO Interrupt Pending Register.
     * Loop with `__builtin_ctz(status)`.
     * Dispatch to `extiChannelRecs[pin].handler->fn()`.
     * Write back to acknowledge and clear the pending interrupt.
   * This provides the lowest possible latency for Gyro EXTI data-ready synchronization (`TASK_GYROPID`).
2. **Minimal Target (<128KB) Optimization**:
   * Since our initial target focuses on the **Betaflight Configurator handshake over UART0**, DMA is completely optional!
   * Omitting the general DMA driver in the initial build eliminates ~4 KB of descriptor management code and RAM buffers, ensuring the firmware stays safely under 128 KB.
