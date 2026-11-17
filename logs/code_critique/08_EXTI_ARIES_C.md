# Code Critique: `src/platform/ARIES/exti_aries.c` vs `lib/vega-sdk/vega-sdk/bsp/`

## 1. File Metadata & Overview
- **Source File**: [`src/platform/ARIES/exti_aries.c`](../../src/platform/ARIES/exti_aries.c)
- **Reference Files**:
  - [`lib/vega-sdk/vega-sdk/bsp/drivers/gpio/gpio.c`](../../lib/vega-sdk/vega-sdk/bsp/drivers/gpio/gpio.c)
  - [`lib/vega-sdk/vega-sdk/bsp/drivers/interrupt/interrupt.c`](../../lib/vega-sdk/vega-sdk/bsp/drivers/interrupt/interrupt.c)
- **Component**: External Interrupt driver (EXTI), mapping GPIO Bank 0 (IRQ 10) and Bank 1 (IRQ 11) to Betaflight callback handlers (IMU DRDY, RX sync).

---

## 2. Structural & Architectural Comparison

| Parameter / Feature | C-DAC SDK | Target (`exti_aries.c`) | Evaluation |
| :--- | :--- | :--- | :--- |
| **Interrupt Routing** | IRQ 10 (GPIO0), IRQ 11 (GPIO1) | Registered via `thejas32_interrupt_register(THEJAS32_IRQ_GPIO0/1)` | **Exact match**. Fully aligned with 32-bit THEJAS32 interrupt table. |
| **Bank Splitting** | Bank 0: Pins 0–15; Bank 1: Pins 16–31 | `thejas32_gpio0_isr` handles 0..15, `thejas32_gpio1_isr` handles 16..31 | **Clean partition**. Avoids cross-bank polling overhead. |
| **Betaflight Interface** | N/A | Implements `EXTIInit`, `EXTIConfig`, `EXTIEnable`, `EXTIDisable`, `EXTIRelease` | **Complete API compliance**. Allows Betaflight sensor drivers (e.g. ICM42688P DRDY) to request interrupt notifications. |

---

## 3. Critical Critique & Identified Vulnerabilities

### Issue 1: Linear 16-Iteration Scanning in Time-Critical EXTI ISR
- **Observation**:
  In `thejas32_gpio0_isr`:
  ```c
  for (int pin = 0; pin < 16; pin++) {
      if (extiChannels[pin].enabled && extiChannels[pin].handler && extiChannels[pin].handler->fn) {
          extiChannels[pin].handler->fn(extiChannels[pin].handler);
      }
  }
  ```
  In flight controller firmware, IMU Data Ready (DRDY) fires at 1.0 kHz (or higher). Executing a 16-iteration loop with multiple pointer dereferences on every single IMU sample introduces unnecessary latency into the control loop.
- **Remediation**:
  Maintain a single 32-bit bitmask `exti_enabled_mask` updated on `EXTIEnable`/`EXTIDisable`.
  The ISR can then do:
  ```c
  uint32_t active = exti_enabled_mask & 0xFFFF;
  while (active) {
      int pin = __builtin_ctz(active);
      extiChannels[pin].handler->fn(extiChannels[pin].handler);
      active &= ~(1U << pin);
  }
  ```
  If only 1 EXTI pin is enabled (e.g. `PA0` for Gyro DRDY), the loop executes exactly once and exits immediately.

### Issue 2: Edge Trigger Verification
- **Observation**:
  `extiChannels[pin].trigger` is recorded but never evaluated against the live pin level. For active-high pulses (rising edge), checking if the pin is high prevents spurious falling edge invocations.

---

## 4. Remediation Plan
1. Add `exti_enabled_mask` and optimize `thejas32_gpio0_isr` and `thejas32_gpio1_isr` with `__builtin_ctz`.
2. Output optimized file to `newFiles/updatedFiles/src/platform/ARIES/exti_aries.c`.
