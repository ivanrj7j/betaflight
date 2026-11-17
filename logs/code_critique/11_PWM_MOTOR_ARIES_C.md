# Code Critique: `src/platform/ARIES/pwm_motor_aries.c` vs `lib/vega-sdk/vega-sdk/bsp/drivers/timer/timer.c`

## 1. File Metadata & Overview
- **Source File**: [`src/platform/ARIES/pwm_motor_aries.c`](../../src/platform/ARIES/pwm_motor_aries.c)
- **Reference File**: [`lib/vega-sdk/vega-sdk/bsp/drivers/timer/timer.c`](../../lib/vega-sdk/vega-sdk/bsp/drivers/timer/timer.c)
- **Component**: 4-Channel motor PWM scheduler, Standard PWM (400 Hz) & OneShot125 support, Hardware Timer 1 (IRQ 8) non-blocking pulse deadline sequencer.

---

## 2. Structural & Architectural Comparison

| Parameter / Feature | Reference (`timer.c`) | Target (`pwm_motor_aries.c`) | Evaluation |
| :--- | :--- | :--- | :--- |
| **Timer Assignment** | Timer 0/1/2 generic delays | Hardware Timer 1 (`0x10000A14`, IRQ 8) | **Dedicated allocation**. Leaves Timer 0 exclusively for `micros()`/timebase and Timer 2 free. |
| **Interrupt Clearing** | `(void)Timer(n).EOI;` | `(void)THEJAS32_TIMER(1).EOI;` | **Exact match**. Required by Synopsys DW_apb_timers IP to clear pending IRQ. |
| **Actuator Pinout** | N/A | Motor 1 `PA1`, Motor 2 `PA2`, Motor 3 `PA3`, Motor 4 `PA4` | **Exact match**. Maps to Header J2 (pins 5, 3, 1) and Header J1 (pin 10). |
| **Scheduling Model** | Polled delays (`timer_put_delay`) | Ascending sorted deadline scheduler with dynamic Timer 1 reload | **Brilliant architectural innovation**. Overcomes THEJAS32 lack of hardware motor PWM timers with zero CPU-blocking delay loops! |

---

## 3. Critical Critique & Identified Vulnerabilities

### Issue 1: Race Condition in `pwmCompleteMotorUpdate`
- **Observation**:
  In `pwmCompleteMotorUpdate()`:
  ```c
  for (uint8_t i = 0; i < count; i++) {
      IOHi(pwmMotors[sortedMotors[i].index].io);
  }

  sortedMotorCount = count;
  currentMotorIdx = 0;

  THEJAS32_TIMER(1).Control = 0;
  THEJAS32_TIMER(1).LoadCount = firstCycles;
  THEJAS32_TIMER(1).Control = 0x03;
  ```
  If Timer 1 was previously active or if an unserviced interrupt fired before `THEJAS32_TIMER(1).Control = 0;`, `thejas32_pwm_timer1_isr` could execute concurrently with the pin-high loop, reading an inconsistent `currentMotorIdx`.
- **Remediation**:
  Disable Timer 1 (`THEJAS32_TIMER(1).Control = 0;`) at the very beginning of `pwmCompleteMotorUpdate()` before sorting or asserting pins HIGH.

### Issue 2: Sub-Microsecond Threshold Clamp
- **Observation**:
  In `thejas32_pwm_timer1_isr`:
  ```c
  if (delta < 50) {
      continue;
  }
  ```
  At 100 MHz, 50 clock cycles is $0.5\,\mu\text{s}$. If two motors have pulse widths within 50 cycles of each other, setting both LOW simultaneously is completely correct (the difference is $< 0.05\%$ of total pulse width, far below ESC jitter resolution).
  However, if `delta >= 50` but $< 100$ cycles, the latency of the interrupt return and next interrupt entry could exceed `delta`. A minimum clamp of 100 cycles ($1.0\,\mu\text{s}$) provides safe jitter margins.

---

## 4. Remediation Plan
1. Add Timer 1 disable at start of `pwmCompleteMotorUpdate()`.
2. Tune threshold clamp to 100 cycles in ISR.
3. Output hardened file to `newFiles/updatedFiles/src/platform/ARIES/pwm_motor_aries.c`.
