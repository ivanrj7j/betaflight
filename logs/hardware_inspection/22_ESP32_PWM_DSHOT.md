# Hardware Inspection Log: `src/platform/ESP32/pwm_motor_esp32.c` & `dshot_esp32.c`
**Consulted Log**: [09_STM32_TIMER_PWM_HW.md](09_STM32_TIMER_PWM_HW.md)

## 1. File Purpose & Role in Architecture
* **File Paths**:
  * [src/platform/ESP32/pwm_motor_esp32.c](../../src/platform/ESP32/pwm_motor_esp32.c)
  * [src/platform/ESP32/dshot_esp32.c](../../src/platform/ESP32/dshot_esp32.c)
* **Subsystem**: Motor Output Subsystem. Implements standard analog PWM, OneShot125/42, MultiShot, and digital DShot150/300/600 motor protocols.
* **Key Components**:
  * `motorPwmDevInit()`: Configures the ESP32 LED Controller (`LEDC`) peripheral for driving 8 motor channels with fractional clock division.
  * `dshotPwmDevInit()`: Configures the ESP32 Remote Control (`RMT`) peripheral for pulse-train symbol generation, driving digital DShot without general-purpose DMA.
  * `dshotEncodePacket()`: Encodes 16-bit DShot frames into RMT duration/level symbols.

---

## 2. Comparative Analysis: ESP32 vs. STM32 (`09_STM32_TIMER_PWM_HW.md`)

| Feature / Hardware Challenge | STM32 Implementation (`09_STM32_TIMER_PWM_HW.md`) | ESP32 Implementation (`pwm_motor_esp32.c`, `dshot_esp32.c`) |
| :--- | :--- | :--- |
| **PWM Hardware Engine** | General-purpose Timers (TIM1–TIM14) with Capture/Compare registers (CCR1–CCR4). | Dedicated LED Controller (`LEDC`) peripheral with 8 independent low-speed channels. |
| **Clock Tree Complexity** | Asymmetric APB1 (84 MHz with $2\times$ PLL doubler) vs APB2 (168 MHz) bus clocks. Requires runtime pointer checks. | Uniform 80 MHz APB clock feeding LEDC slow clock (`LEDC_SLOW_CLK_APB`). |
| **DShot Signal Generation** | Timer DMA Burst or GPIO Bitbang DMA. Buffers pushed via memory-to-timer DMA streams. | Dedicated Remote Control (`RMT`) peripheral with dedicated internal SRAM (`RMT_MEM_BASE`). Zero DMA streams used! |
| **Resource Collisions** | Severe silicon DMA stream/channel conflicts (e.g. TIM3_CH4 vs TIM5_CH1 sharing `DMA1_Stream2`). | No DMA collisions for motor outputs; LEDC and RMT route directly to any pin via GPIO matrix. |
| **Pin Mapping Flexibility** | Rigid pin-to-timer Alternate Function table (`fullTimerHardware`). | Fully dynamic: any GPIO pin can be bound to any LEDC or RMT channel. |

---

## 3. Hardware Challenges ESP32 Developers Faced & How They Overcame Them

### Challenge 1: Fractional Clock Divider & Duty Resolution Underflow
* **The Problem**: LEDC uses a fractional divider with 8 fractional bits ($Q8$) and a counter resolution up to 14 bits (`SOC_LEDC_TIMER_BIT_WIDTH`). For high PWM rates (e.g. OneShot or MultiShot), if the divider calculation results in a divider $< 1.0$, the hardware register overflows or underflows, resulting in 100% or 0% duty cycle.
* **How It Was Overcome**:
  The driver dynamically walks down the resolution bit-width until the divider is $\ge 1.0$:
  ```c
  uint8_t resolution = SOC_LEDC_TIMER_BIT_WIDTH;
  uint32_t maxDuty = 1u << resolution;
  uint32_t dividerQ8 = ((uint64_t)LEDC_APB_HZ << 8) / ((uint64_t)pwmRate * maxDuty);
  while (dividerQ8 < (1u << 8) && resolution > 8) {
      resolution--;
      maxDuty = 1u << resolution;
      dividerQ8 = ((uint64_t)LEDC_APB_HZ << 8) / ((uint64_t)pwmRate * maxDuty);
  }
  ```

### Challenge 2: Null-Pointer VTable Panic on Motor Testing
* **The Problem**: Betaflight's mixer and CLI `motor` commands call `motorConvertFromExternal()` and `motorConvertToExternal()` through the driver's `vTable` without checking for `NULL`. If a platform leaves these pointers `NULL`, entering the Motors tab in Betaflight Configurator immediately causes a hard fault exception!
* **How It Was Overcome**:
  The driver explicitly implements passthrough conversion functions:
  ```c
  static float pwmMotorConvertFromExternal(uint16_t externalValue) { return (float)externalValue; }
  static uint16_t pwmMotorConvertToExternal(float motorValue) { return (uint16_t)motorValue; }
  ```

### Challenge 3: DShot Frame Overlap in RMT Peripheral
* **The Problem**: If the flight controller scheduler triggers a new motor update before the previous DShot packet finishes transmitting over the wire (e.g. at 8 kHz loop rates with DShot150), rewriting the RMT memory corrupts the active frame.
* **How It Was Overcome**:
  In `dshotUpdateComplete()`, before resetting pointers and starting transmission, the driver polls the raw interrupt flag `TX_DONE` with a 500 µs safety timeout:
  ```c
  const timeUs_t startUs = micros();
  while (!(rmt_ll_tx_get_interrupt_status_raw(&RMT, ch) & RMT_LL_EVENT_TX_DONE(ch))) {
      if (cmpTimeUs(micros(), startUs) >= 500) break;
  }
  rmt_ll_clear_interrupt_status(&RMT, RMT_LL_EVENT_TX_DONE(ch));
  ```

---

## 4. Key Takeaways & Porting Blueprint for ARIES v3.0 (THEJAS32 / VEGA ET1031)

1. **Dedicated Hardware PWM (8 Channels)**:
   * THEJAS32 possesses a dedicated **8-channel PWM controller** (`PWM[0..7]`), all running off the uniform 100 MHz clock.
   * This matches the clean architecture of ESP32's LEDC, avoiding all the timer-DMA collision headaches of STM32.
2. **Protocol Strategy for <128KB Minimal SRAM Target**:
   * Initial port target must use standard PWM (400 Hz) or OneShot125 via the dedicated PWM controller registers.
   * This requires no complex DMA streaming, no RMT pulse-train coprocessor, and consumes less than 1 KB of Flash/SRAM.
3. **Mandatory VTable Functions**:
   * Ensure `convertExternalToMotor` and `convertMotorToExternal` are never `NULL` in the ARIES v3.0 `motorVTable_t` to guarantee compatibility with Betaflight Configurator's Motor Test sliders.
