# Hardware Inspection Log: `src/platform/common/stm32/system.c`

## 1. File Purpose & Role in Architecture
* **File Path**: `src/platform/common/stm32/system.c`
* **Subsystem**: Low-Level System Timebase, Core Clock Management, Cycle Counting, and Memory Section Relocation.
* **Key Exports**:
  * `cycleCounterInit()`: Configures high-resolution CPU cycle counter.
  * `SysTick_Handler()`: 1 kHz system uptime interrupt.
  * `micros()` / `microsISR()`: Microsecond resolution system timestamping.
  * `millis()`: Millisecond resolution uptime.
  * `getCycleCounter()`: Returns raw hardware CPU cycle counter.
  * `initialiseMemorySections()`: Copies critical flight algorithms to tightly-coupled fast RAM (TCM/CCM).

---

## 2. Hardware Challenges STM32 Developers Faced & How They Overcame Them

### Challenge 1: Multi-Bus Clock Tree Discrepancies
* **The Problem**: On advanced STM32 families (like STM32N6 or dual-core STM32H7), the CPU core clock runs at a different frequency than the system bus. On STM32N6, `HAL_RCC_GetSysClockFreq()` reported the bus clock (400 MHz) instead of the CPU clock (600 MHz).
* **The Bug**: SysTick and DWT count at the CPU clock. When `usTicks` was calculated using the bus clock (400 instead of 600), `micros()` computed `(400000 - SysTick->VAL) / 400` against a 600,000 reload value. Whenever `VAL` was above 400,000, integer underflow caused `micros()` to jump forward by ~10.7 seconds for a third of every millisecond!
* **How It Was Overcome**:
  ```c
  #elif defined(STM32N6)
      cpuClockFrequency = SystemCoreClock;
  #elif defined(CORE_CM4)
      cpuClockFrequency = HAL_RCC_GetHCLKFreq();
  #else
      cpuClockFrequency = HAL_RCC_GetSysClockFreq();
  #endif
  ```
  The code strictly isolates the exact core clock source per silicon revision.

---

### Challenge 2: CoreSight DWT Software Lockout
* **The Problem**: On ARM Cortex-M7 (STM32F7, STM32H7) and newer Cortex-M4 variants, ARM CoreSight features a hardware Lock Access Register (LAR) that defaults to locked state on reset. If not unlocked, writing to `DWT->CTRL` to enable the cycle counter (`CYCCNT`) is completely ignored by the silicon.
* **The Additional Complication**: In the official ST CMSIS header for STM32F4, the `DWT_Type` struct was missing the `LAR` field entirely.
* **How It Was Overcome**:
  The developers hardcoded the CoreSight magic unlock constant `0xC5ACCE55` and manually cast the peripheral base register offset:
  ```c
  #define DWT_LAR_UNLOCK_VALUE 0xC5ACCE55
  __O uint32_t *DWTLAR = (uint32_t *)(DWT_BASE + 0x0FB0);
  *(DWTLAR) = DWT_LAR_UNLOCK_VALUE;
  DWT->CYCCNT = 0;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
  ```

---

### Challenge 3: SysTick Down-Counter Rollover Race Condition in `micros()`
* **The Problem**: `SysTick` is a 24-bit down-counter that decrements from `LOAD` (e.g., 100,000) to 0, firing an interrupt when it reaches zero. A severe race condition occurs when `micros()` is called right after the counter rolls over to zero, but before the interrupt handler `SysTick_Handler()` has run to increment `sysTickUptime`. Reading `sysTickUptime` at that instant would result in a timestamp that is 1000 µs in the past!
* **How It Was Overcome**:
  1. For interrupt/privileged contexts (`microsISR()`), they wrap the read in `ATOMIC_BLOCK(NVIC_PRIO_MAX)` and check the hardware `SysTick_CTRL_COUNTFLAG_Msk`. If the flag is set, they compensate by artificially adding 1 ms:
     ```c
     if (SysTick->CTRL & SysTick_CTRL_COUNTFLAG_Msk) {
         sysTickPending = 1;
         cycle_cnt = SysTick->VAL;
     }
     return ((ms + pending) * 1000) + (usTicks * 1000 - cycle_cnt) / usTicks;
     ```
  2. For main loop thread context, they implemented a lock-free retry loop verifying that `sysTickUptime` didn't tick while reading `VAL`:
     ```c
     do {
         ms = sysTickUptime;
         cycle_cnt = SysTick->VAL;
     } while (ms != sysTickUptime || cycle_cnt > sysTickValStamp);
     ```

---

### Challenge 4: Flash Wait States & TCM Memory Relocation
* **The Problem**: STM32 internal Flash runs at 30–100 MHz, requiring 3–7 wait states at 168–480 MHz CPU speeds. Executing time-critical flight loops directly from Flash resulted in unpredictable jitter and degraded gyro loop frequency.
* **How It Was Overcome**:
  Developers carved out ITCM (Instruction Tightly Coupled Memory) and CCM (Core Coupled Memory) sections in the linker script. `initialiseMemorySections()` performs a runtime `memcpy` from Flash to RAM:
  ```c
  memcpy(&tcm_code_start, &tcm_code, (size_t)(&tcm_code_end - &tcm_code_start));
  ```

---

## 3. Direct Porting Blueprint for ARIES v3.0 (THEJAS32 / VEGA ET1031)

1. **Cycle Counter**:
   * Replace ARM DWT with RISC-V 64-bit performance counters.
   * On RV32IM, read CSRs `cycle` and `cycleh`:
     ```c
     static inline uint32_t getCycleCounter(void) {
         uint32_t count;
         __asm__ volatile ("csrr %0, cycle" : "=r" (count));
         return count;
     }
     ```
2. **Microsecond Timebase**:
   * On RISC-V, the machine timer (`mtime` / `mtimecmp`) counts **upwards continuously** as a 64-bit value at a known frequency (100 MHz).
   * **Massive Advantage**: Because a 64-bit up-counter never rolls over in practice (takes over 5,000 years), the complex down-counter wrap-around compensation required on STM32 SysTick is completely unnecessary!
   * `micros()` can simply be:
     ```c
     uint32_t micros(void) {
         return (uint32_t)(get_mtime() / (100000000 / 1000000));
     }
     ```
3. **Memory Relocation**:
   * On ARIES v3.0, the entire application executes directly in 250 KB SRAM at `0x200000`.
   * Therefore, all code already executes with zero wait states, making `initialiseMemorySections()` a safe no-op.
