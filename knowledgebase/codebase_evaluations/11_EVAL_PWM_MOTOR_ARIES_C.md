# Evaluation Node 11: Multi-Channel PWM Motor Driver (`pwm_motor_aries.c`)

## 1. Executive Summary & File Traceability
- **Evaluated File**: [`src/platform/ARIES/pwm_motor_aries.c`](../../src/platform/ARIES/pwm_motor_aries.c)
- **Source Log**: [`logs/code_critique/11_PWM_MOTOR_ARIES_C.md`](../../logs/code_critique/11_PWM_MOTOR_ARIES_C.md)
- **Upstream References**:
  - [`src/main/drivers/pwm_output.h`](../../src/main/drivers/pwm_output.h)
  - [`lib/vega-sdk/vega-sdk/bsp/drivers/timer/timer.c`](../../lib/vega-sdk/vega-sdk/bsp/drivers/timer/timer.c)
- **Domain**: 4-channel ESC pulse generation (Standard PWM & OneShot125), Synopsys DW_apb_timers IP, non-blocking ascending deadline scheduler.

---

## 2. Decision & Action Scoring Rubric (0 to 10)

| Decision / Action | Score (0-10) | Evaluation Rationale |
| :--- | :---: | :--- |
| **Ascending Deadline Event Scheduler Architecture** | 10/10 | **One of the most brilliant engineering solutions in the entire port**. Overcomes the lack of dedicated advanced PWM hardware timers on THEJAS32 without blocking the CPU in microsecond delay loops. |
| **Hardware Timer 1 Dedicated Allocation (IRQ 8)** | 9/10 | Clean division of hardware resources: Timer 0 dedicated to system timebase (`micros()`), Timer 1 to PWM deadlines, Timer 2 unallocated. |
| **Actuator Pin Assignment (Header J1 & J2)** | 9/10 | Efficiently maps Motors 1..4 to GPIOs `PA1`, `PA2`, `PA3`, `PA4`, matching physical headers on ARIES v3. |
| **Timer 1 Enable/Disable Race Condition** | 5/10 | Setting pins HIGH before resetting Timer 1 control registers allowed a previous unserviced interrupt to fire mid-loop, causing jitter. |
| **Sub-Microsecond Threshold Clamping (50 vs 100 cycles)** | 7/10 | A 50-cycle threshold ($0.5\,\mu\text{s}$) was marginally close to the RISC-V interrupt entry/exit latency overhead (~40 cycles). |

---

## 3. Deep-Dive Critique

### What Went Wrong
1. **Timer 1 Race Window during Motor Pulse Initialization**:
   - In [`src/platform/ARIES/pwm_motor_aries.c`](../../src/platform/ARIES/pwm_motor_aries.c#L140-L155):
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
   - If a previous frame's Timer 1 interrupt had not finished or an errant interrupt triggered while pins were being raised HIGH, `thejas32_pwm_timer1_isr` could run against inconsistent index states.
2. **ISR Reentrancy Margin on Very Close Pulse Widths**:
   - When two motors had target pulse widths within 50 cycles of each other, reloading Timer 1 with $< 50$ cycles was impossible due to ISR entry/exit latency, potentially delaying the second motor's falling edge.

### What Could Have Been Done Better
1. **Preemptive Timer Disable**:
   - Disabling Timer 1 at the very first instruction of `pwmCompleteMotorUpdate()` (`THEJAS32_TIMER(1).Control = 0;`) prevents any possible race condition while calculating deadlines.
2. **100-Cycle Grouping Threshold**:
   - If `delta < 100` cycles ($1.0\,\mu\text{s}$ at 100 MHz), group the falling edges together immediately in the current ISR iteration. In ESC control, $1\,\mu\text{s}$ is well within typical ESC command quantization noise.

### What Were the Good / Brilliant Parts
1. **The Ascending Sorted Deadline Algorithm**:
   - THEJAS32 does not possess an STM32-style advanced timer peripheral with multi-channel capture/compare output registers (TIM1/TIM8).
   - In most naïve ports on microcontrollers without hardware PWM, developers resort to busy-waiting (`delayMicroseconds()`) which consumes $100\%$ CPU for 1 to 2 milliseconds per flight cycle.
   - The deadline sequencer sorts the 4 pulse widths in ascending order:
     - Set all 4 pins HIGH at $t=0$.
     - Program Timer 1 for the shortest deadline ($M_{\text{min}}$).
     - On Timer 1 IRQ, drop that pin LOW, reload Timer 1 with the delta to the next motor deadline, and immediately return to the main loop.
   - Total CPU time consumed across all 4 channels is $< 3\,\mu\text{s}$! This preserved $> 99.7\%$ of CPU budget for the flight control PID loops.
2. **Synopsys APB Timer EOI Compliance**:
   - Correctly reading `THEJAS32_TIMER(1).EOI` to clear the interrupt flag, matching the Synopsys DesignWare IP specification.
