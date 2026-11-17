# Knowledge Evaluation Node: `src/platform/ARIES/system.c`
**Evaluated Log**: [`logs/code_critique/05_SYSTEM_C.md`](../../logs/code_critique/05_SYSTEM_C.md)  
**Target Code**: [`src/platform/ARIES/system.c`](../../src/platform/ARIES/system.c)  
**Reference Code**:
- [`lib/vega-sdk/vega-sdk/bsp/drivers/interrupt/interrupt.c`](../../lib/vega-sdk/vega-sdk/bsp/drivers/interrupt/interrupt.c)
- [`lib/vega-sdk/vega-sdk/bsp/drivers/timer/timer.c`](../../lib/vega-sdk/vega-sdk/bsp/drivers/timer/timer.c)  
**Component**: System Clocks, Cycle Counters, High-Resolution Timebase (`micros()`, `millis()`), Trap Handler (`handle_trap`), Non-RVC Memory Routines.

---

## 1. Context & Architectural Role
`system.c` manages clock frequencies (100 MHz SystemCoreClock), initializes Hardware Timer 0 as a down-counter with automatic rollover tracking, computes microsecond/millisecond timebases without pulling in 64-bit software division, handles synchronous CPU panics and external interrupts, and provides non-RVC bare-metal implementations of `memset`, `memcpy`, and `memmove`.

---

## 2. Decision & Action Scoring (0 to 10)

| Decision / Action Taken | Score (0-10) | Category | Technical Analysis & Rationale | References |
| :--- | :---: | :---: | :--- | :--- |
| **Fast 32-bit Modular Reciprocal Division in `micros()`** | **9.8 / 10** | **Masterpiece** | Normal 64-bit cycle conversion requires `cycles / 100ULL`, pulling in GCC's `__udivdi3` software division (taking 150+ cycles per call). Decomposing into high/low words and modular reciprocal math reduced execution time to $< 25$ cycles. | [`system.c#L112-L124`](../../src/platform/ARIES/system.c#L112-L124) |
| **Atomic 64-bit Cycle Counter Reading Loop** | **9.5 / 10** | **Brilliant** | Reading `mcycleh` and `mcycle` in a loop until `hi == hi2` prevented the catastrophic 4.29-second time warp bug that would have blown up the PID $D$-term. | [`system.c#L13-L23`](../../src/platform/ARIES/system.c#L13-L23) |
| **Handcrafted Non-RVC `memset`/`memcpy` with Loop Distribution Disabled** | **9.5 / 10** | **Brilliant** | `optimize("no-tree-loop-distribute-patterns")` prevented GCC from replacing loops with recursive calls while ensuring 0 compressed instructions entered the binary on non-RVC ET1031 silicon. | [`system.c#L360-L403`](../../src/platform/ARIES/system.c#L360-L403) |
| **Reading Timer 0 `EOI` in Interrupt Service Routine** | **9.0 / 10** | **Good** | Reading `THEJAS32_TIMER(0).EOI` cleared the active interrupt latch in the Synopsys DW_apb_timers IP, preventing an endless interrupt loop. | [`system.c#L50`](../../src/platform/ARIES/system.c#L50) vs [`timer.c#L48`](../../lib/vega-sdk/vega-sdk/bsp/drivers/timer/timer.c#L48) |
| **`systemReset()` Trapping in Infinite Loop (`while (1);`)** | **3.0 / 10** | **Flawed** | Hanging in `while(1);` on reboot meant that executing `save` in the CLI forced the user to physically button-reset the board instead of rebooting into Betaflight. | [`system.c#L330-L333`](../../src/platform/ARIES/system.c#L330-L333) |
| **Linear 32-Step Loop in Machine External Interrupt Dispatch** | **5.0 / 10** | **Mediocre** | Scanning 0..31 linearly on every external interrupt wasted instructions compared to using `__builtin_ctz(status)`. | [`system.c#L292-L298`](../../src/platform/ARIES/system.c#L292-L298) |

---

## 3. Deep-Dive Analysis

### 🌟 What Were the Good / Brilliant Parts?
1. **Elimination of `__udivdi3` in the Flight Loop**:
   Betaflight queries `micros()` dozens of times per millisecond (scheduler task timing, PID loop, sensor timeout checks, RC frame jitter). A standard division by 100 on a 64-bit integer takes over 150 cycles on an RV32IM core. The modular reciprocal trick saved thousands of CPU cycles per second, keeping CPU load under $40\%$.
2. **Safe Silicon Memory Primitives**:
   Because the Espressif toolchain was compiled with compressed instructions, calling newlib's `memset` crashed physical THEJAS32 silicon with `mcause = 2`. Providing verified byte loops with the GCC tree-distribution optimization disabled allowed clean compilation without recursive calls.

### ⚠️ What Went Wrong?
1. **Broken Soft-Reboot Architecture**:
   Leaving `systemReset()` as `while (1);` made automated CLI configuration saving impossible. After typing `save` in Betaflight CLI or saving settings in Configurator, the device hung indefinitely.
2. **Linear Interrupt Checking Overhead**:
   External interrupts fired at 1.0 kHz. Looping 32 times on every interrupt to find a single active bit in `INTR_STATUS` created unnecessary ISR latency.

### 💡 What Could Have Been Done Better?
- **Vector-Jump Soft Reset**:
  ```c
  void systemReset(void) {
      __disable_irq();
      extern uint32_t _estack;
      extern void _start(void);
      __asm__ volatile ("la sp, _estack; la t0, _start; jr t0;");
      while (1);
  }
  ```
- **CTZ Bit-Scan in Trap Handler**:
  ```c
  uint32_t status = THEJAS32_INTR_CTRL.INTR_STATUS;
  while (status) {
      uint32_t i = __builtin_ctz(status);
      if (interrupt_table[i]) interrupt_table[i]();
      status &= ~(1UL << i);
  }
  ```

---

## 4. Key Takeaways & Overall Score: 8.5 / 10
`system.c` is a high-performance bare-metal platform driver that solved two of the most difficult technical blockers on THEJAS32 (timebase software division and multilib RVC instructions).
