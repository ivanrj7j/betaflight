# Hardware Inspection Log: `src/platform/STM32/timer_stm32f4xx.c`

## 1. File Purpose & Role in Architecture
* **File Path**: `src/platform/STM32/timer_stm32f4xx.c`
* **Subsystem**: Hardware Timer Peripheral Definitions, Multi-Bus Clock Frequency Calculation, Pin-to-Timer Channel Mapping Table (`fullTimerHardware`), and DMA Request Matrix.
* **Key Components**:
  * `timerDefinitions[]`: Static base addresses, peripheral clock gates (`RCC_APB1`/`RCC_APB2`), and NVIC IRQ numbers for TIM1 through TIM14.
  * `fullTimerHardware[]`: Exhaustive pin-multiplexing table mapping physical MCU pins to specific Timer Output Compare channels.
  * `timerClockFromInstance()`: Solves the APB bus clock divider and internal PLL doubler to return exact timer input frequency.

---

## 2. Hardware Challenges STM32 Developers Faced & How They Overcame Them

### Challenge 1: Asymmetric APB1 vs APB2 Bus Multipliers
* **The Problem**: On STM32F4, timers are partitioned across two different peripheral buses:
  * APB2 timers (TIM1, TIM8, TIM9, TIM10, TIM11) run up to 168 MHz.
  * APB1 timers (TIM2, TIM3, TIM4, TIM5, TIM12..14) run on a bus with a maximum clock limit of 42 MHz. However, STM32 silicon includes an internal $2\times$ clock multiplier if the APB prescaler is $>1$, clocking APB1 timers at 84 MHz.
  * If software assumed all timers ran at the same frequency, motor PWM pulse widths and DShot bit periods would be doubled or halved depending on which timer channel the motor pad was wired to!
* **How It Was Overcome**:
  `timerClockFromInstance()` dynamically inspects the peripheral pointer:
  ```c
  const TIM_TypeDef *tim_ptr = (const TIM_TypeDef *)tim;
  if (tim_ptr == TIM8 || tim_ptr == TIM1 || tim_ptr == TIM9 || tim_ptr == TIM10 || tim_ptr == TIM11) {
      return SystemCoreClock;      // 168 MHz
  } else {
      return SystemCoreClock / 2;  // 84 MHz
  }
  ```

---

### Challenge 2: Rigid Silicon DMA Stream/Channel Collisions
* **The Problem**: In STM32F4, peripheral DMA requests are permanently hardwired to fixed Stream and Channel intersections. For example:
  * `TIM3_CH4` is bound to `DMA1_Stream2_Channel5`.
  * `TIM5_CH1` is also bound to `DMA1_Stream2_Channel6`.
  * If a flight controller designer assigned Motor 1 to a pin using `TIM3_CH4` and Motor 2 to a pin using `TIM5_CH1`, both motors fought for `DMA1_Stream2`. It was physically impossible to run DMA-driven DShot or LED strip simultaneously on those pins.
* **How It Was Overcome**:
  Betaflight developers built an extensive DMA collision resolution engine (`dma_reqmap.c` and `dmaAllocate()`) that verifies whether another peripheral has claimed the stream before allowing DShot initialization.

---

## 3. Direct Porting Blueprint for ARIES v3.0 (THEJAS32 / VEGA ET1031)

1. **Uniform 100 MHz Clock Source**:
   * On ARIES v3.0, the THEJAS32 SoC runs all timers and PWM modules directly from the main 100 MHz clock.
   * There are no asymmetric APB1/APB2 bus multipliers or half-speed timer clocks. Calculating timer prescalers is straightforward:
     $$f_{\text{timer}} = 100\,\text{MHz}$$
2. **Dedicated 8-Channel PWM Subsystem**:
   * Unlike STM32 where PWM channels are scattered across 14 general-purpose timers competing with input capture and interrupts, ARIES v3.0 provides a **dedicated 8-channel PWM controller** (`PWM[0..7]`):
     * `PWM[0..2]` on Header `J2` (Pins 86, 85, 82) $\rightarrow$ Motors 1, 2, 3
     * `PWM[3..4]` on Header `J1` (Pins 81, 80) $\rightarrow$ Motor 4, Motor 5 / Servo
     * `PWM[5..7]` on Header `J3` (Pins 77, 76, 75) $\rightarrow$ Auxiliary Motors / Servos / Beeper
   * This dedicated PWM architecture simplifies timer configuration and eliminates DMA stream collision conflicts entirely for standard motor outputs.
