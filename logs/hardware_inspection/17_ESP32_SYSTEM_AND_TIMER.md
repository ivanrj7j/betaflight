# Hardware Inspection Log: `src/platform/ESP32/system.c` & `timer_esp32.c`

## 1. File Purpose & Role in Architecture
* **File Paths**: `src/platform/ESP32/system.c` and `src/platform/ESP32/timer_esp32.c`
* **Subsystem**: System Lifecycle, Timebase Generation (`micros()`, `millis()`), and Hardware Timer Abstraction on Non-ARM / RISC-V Architectures (ESP32-S3, ESP32-C5).
* **Consulted Log Comparison**:
  * [01_STM32_COMMON_SYSTEM.md](01_STM32_COMMON_SYSTEM.md) (STM32 Clock tree, SysTick down-counter race condition, DWT lock)
  * [09_STM32_TIMER_PWM_HW.md](09_STM32_TIMER_PWM_HW.md) (STM32 14-timer table, APB prescalers, DMA collisions)

---

## 2. Similarities & Differences: STM32 vs ESP32

| Dimension | STM32 Implementation ([Log 01](01_STM32_COMMON_SYSTEM.md) & [Log 09](09_STM32_TIMER_PWM_HW.md)) | ESP32 Implementation (`system.c` & `timer_esp32.c`) | Porting Implication for ARIES v3.0 (THEJAS32) |
| :--- | :--- | :--- | :--- |
| **Microsecond Timebase (`micros()`)** | Complex 24-bit down-counter SysTick with rollover flag checking (`COUNTFLAG`) and pending millisecond arithmetic. | Monotonic 64-bit hardware counter (`SYSTIMER`) ticking at 16 MHz; divided directly into microseconds (`ticks / 16`). | Follows the **ESP32 model**: RISC-V 64-bit `mtime` up-counter divided by 100 (for 100MHz). Zero rollover races. |
| **Cycle Counting** | ARM CoreSight DWT hardware cycle counter with magic unlock register (`0xC5ACCE55`). | Xtensa `ccount` or RISC-V `cycle` CSR / `SYSTIMER` counter. | Use standard RISC-V CSR `cycle` / `cycleh` via `csrr`. |
| **Timer Hardware Matrix (`fullTimerHardware[]`)** | Massive 200+ line table mapping 14 hardware timers and 4 channels per pin, tracking fixed DMA channel collisions. | **Completely empty stub** (`const timerHardware_t fullTimerHardware[1] = { { 0 } };`). General-purpose timers are not used. | On ARIES v3.0, the dedicated 8-channel PWM controller avoids the need for STM32's massive timer table. |
| **Delay Generation** | Busy-wait checking `micros()` in software loops. | ROM function call (`esp_rom_delay_us()`) provided by manufacturer mask ROM. | ARIES v3.0 can use a simple busy-wait loop against RISC-V `mtime` or cycle count. |
| **System Core Clock** | Inferred via complex RCC APB/AHB register trees and PLL dividers. | Read directly from ROM bootloader configuration (`esp_rom_get_cpu_ticks_per_us()`). | On ARIES v3.0, clock is fixed at 100 MHz by external oscillator U4 on `CLK` pin 44. |

---

## 3. Deep Architectural Analysis: How ESP32 Solved the Timer Problem

On STM32, motor PWM and DShot are driven by general-purpose timers (TIM1–TIM14) using Output Compare and Timer DMA. This caused massive complexity:
* APB1 timers run at half frequency compared to APB2.
* DMA streams collide when multiple timers share the same DMA controller.

On ESP32, developers completely discarded general-purpose timers for motor outputs:
* Standard PWM is delegated to the **LEDC** (LED Controller) peripheral.
* DShot pulses are generated via the **RMT** (Remote Control) peripheral.
* As a result, `src/platform/ESP32/timer_esp32.c` is only 76 lines long! It provides empty stubs (`timerInit() = NOOP`, `timerGetByTagAndIndex() = NULL`) simply to satisfy linker symbols in `cli.c` and `config.c`.

---

## 4. Porting Blueprint for ARIES v3.0 (THEJAS32 / VEGA ET1031)

1. **Timebase Implementation**:
   * Implement `micros()` using the 64-bit RISC-V machine timer:
     ```c
     FAST_CODE timeUs_t micros(void) {
         return (timeUs_t)(get_mtime_64() / 100); // 100 MHz -> 1 MHz (us)
     }
     timeMs_t millis(void) {
         return micros() / 1000;
     }
     ```
2. **Minimal Timer Stubs for Prototype**:
   * For our <128KB minimal Configurator target, we can adopt ESP32's approach: provide minimal/stub timer functions to satisfy common references, while motor control uses the dedicated 8-channel THEJAS32 PWM block.
