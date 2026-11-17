# Evaluation Node 08: External Interrupt Controller (`exti_aries.c`)

## 1. Executive Summary & File Traceability
- **Evaluated File**: [`src/platform/ARIES/exti_aries.c`](file:///home/ivan/Desktop/betaflight/src/platform/ARIES/exti_aries.c)
- **Source Log**: [`logs/code_critique/08_EXTI_ARIES_C.md`](file:///home/ivan/Desktop/betaflight/logs/code_critique/08_EXTI_ARIES_C.md)
- **Upstream References**:
  - [`src/main/drivers/exti.h`](file:///home/ivan/Desktop/betaflight/src/main/drivers/exti.h)
  - [`lib/vega-sdk/vega-sdk/bsp/drivers/gpio/gpio.c`](file:///home/ivan/Desktop/betaflight/lib/vega-sdk/vega-sdk/bsp/drivers/gpio/gpio.c)
  - [`lib/vega-sdk/vega-sdk/bsp/drivers/interrupt/interrupt.c`](file:///home/ivan/Desktop/betaflight/lib/vega-sdk/vega-sdk/bsp/drivers/interrupt/interrupt.c)
- **Domain**: GPIO External Interrupt Dispatcher, IMU DRDY notification, bank-level IRQ demultiplexing.

---

## 2. Decision & Action Scoring Rubric (0 to 10)

| Decision / Action | Score (0-10) | Evaluation Rationale |
| :--- | :---: | :--- |
| **Mapping GPIO Banks to IRQ 10 / IRQ 11** | 9/10 | Cleanly conforms to VEGA ET1031 interrupt topology where GPIO Bank 0 (pins 0-15) routes to IRQ 10 and Bank 1 (pins 16-31) routes to IRQ 11. |
| **Betaflight EXTI API Abstraction** | 8/10 | Correctly implements `EXTIInit()`, `EXTIConfig()`, `EXTIEnable()`, `EXTIDisable()`, and `EXTIRelease()`, allowing sensor drivers like ICM42688P to hook DRDY seamlessly. |
| **Linear 16-Pin Iteration in ISR** | 3/10 | Iterating through all 16 channels linearly in an ISR that fires at 1.0 kHz introduces branch overhead and cache pollution on every gyro sample. |
| **Missing Active Trigger Level / Edge Verification** | 4/10 | Stores trigger configuration (`EXTI_TRIGGER_RISING`/`FALLING`), but does not query actual GPIO pin status or edge registers to filter spurious interrupts. |
| **Memory Footprint of EXTI Descriptors** | 8/10 | Kept descriptor array compact (`sizeof(extiChannelRec_t) * 32`), fitting neatly within SRAM constraints. |

---

## 3. Deep-Dive Critique

### What Went Wrong
1. **Linear Loop Overhead in High-Frequency ISR**:
   - In [`src/platform/ARIES/exti_aries.c`](file:///home/ivan/Desktop/betaflight/src/platform/ARIES/exti_aries.c#L26-L34), `thejas32_gpio0_isr` and `thejas32_gpio1_isr` loop through 16 iterations:
     ```c
     for (int pin = 0; pin < 16; pin++) {
         if (extiChannels[pin].enabled && extiChannels[pin].handler && extiChannels[pin].handler->fn) {
             extiChannels[pin].handler->fn(extiChannels[pin].handler);
         }
     }
     ```
   - When Gyro DRDY fires at 1000 Hz, 15 out of 16 iterations perform useless null checks and array lookups, burning CPU cycles inside a critical interrupt context.
2. **Missing Edge Validation**:
   - The VEGA GPIO interrupt does not automatically latch edges on all silicon revisions. Failing to sample the pin state can result in redundant triggers or missed falling edges.

### What Could Have Been Done Better
1. **Bitmask Active Channel Tracking with `__builtin_ctz`**:
   - Maintain a 32-bit `uint32_t exti_enabled_mask`.
   - Update it in `EXTIEnable` (`mask |= (1 << pin)`) and `EXTIDisable` (`mask &= ~(1 << pin)`).
   - In the ISR:
     ```c
     uint32_t active = exti_enabled_mask & 0xFFFF;
     while (active) {
         int pin = __builtin_ctz(active);
         extiChannels[pin].handler->fn(extiChannels[pin].handler);
         active &= ~(1U << pin);
     }
     ```
   - For a single gyro DRDY interrupt on pin 0, this executes in $O(1)$ without scanning 15 empty entries.
2. **Atomic Registration**:
   - Enabling and disabling EXTI channels should be guarded by critical sections or atomic bit operations to prevent ISR reentrancy during runtime configuration changes.

### What Were the Good / Brilliant Parts
1. **True Hardware Demuxing**:
   - Splitting Bank 0 and Bank 1 cleanly into two independent ISRs (`thejas32_gpio0_isr` on IRQ 10, `thejas32_gpio1_isr` on IRQ 11) ensures Bank 1 activity never triggers Bank 0 handlers.
2. **Zero Upstream Core Modifications Needed**:
   - Betaflight's higher-level sensor architecture (`src/main/drivers/accgyro/`) expects standard EXTI hooks; this implementation fulfills the contract without hacking upstream sensor state machines.
