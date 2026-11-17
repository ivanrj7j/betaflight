# Domain Plan 04: GPIO, EXTI & Hardware Timers

> **Component Plan ID**: `PLAN-DOM-04`  
> **Parent Architecture Document**: [`PLAN/00_MASTER_PLAN_OVERVIEW.md`](file:///home/ivan/Desktop/betaflight/PLAN/00_MASTER_PLAN_OVERVIEW.md)  
> **Primary Knowledge References**:
> - [`knowledgebase/codebase_evaluations/07_EVAL_IO_ARIES_C_AND_DEF.md`](file:///home/ivan/Desktop/betaflight/knowledgebase/codebase_evaluations/07_EVAL_IO_ARIES_C_AND_DEF.md)
> - [`knowledgebase/codebase_evaluations/08_EVAL_EXTI_ARIES_C.md`](file:///home/ivan/Desktop/betaflight/knowledgebase/codebase_evaluations/08_EVAL_EXTI_ARIES_C.md)
> - [`knowledgebase/codebase_evaluations/11_EVAL_PWM_MOTOR_ARIES_C.md`](file:///home/ivan/Desktop/betaflight/knowledgebase/codebase_evaluations/11_EVAL_PWM_MOTOR_ARIES_C.md)
> - [`knowledgebase/components/04_GPIO_AND_PIN_IO.md`](file:///home/ivan/Desktop/betaflight/knowledgebase/components/04_GPIO_AND_PIN_IO.md)
> - [`knowledgebase/components/05_TIMER_AND_MOTOR_PWM.md`](file:///home/ivan/Desktop/betaflight/knowledgebase/components/05_TIMER_AND_MOTOR_PWM.md)
> - [`knowledgebase/CRITICAL_KNOWLEDGE_GAPS_AND_ASSUMPTIONS.md`](file:///home/ivan/Desktop/betaflight/knowledgebase/CRITICAL_KNOWLEDGE_GAPS_AND_ASSUMPTIONS.md)

---

## 1. Executive Domain Summary

The GPIO, External Interrupt (EXTI), and Hardware Timer subsystem provides the real-time physical control and sensory synchronization infrastructure for C-DAC ARIES v3.0:
1. **Flat Dual-Bank GPIO Architecture (`io_aries.c`)**:
   - Manages 32 pins split across two 16-pin hardware banks: Bank 0 (`0x10080000`, pins 0..15) and Bank 1 (`0x10180000`, pins 16..31).
   - Utilizes THEJAS32's unique **address-decoded bus** (`BASE + ((1 << pin) << 2)`), achieving single-instruction atomic bit writes without read-modify-write hazards ($< 10\text{ ns}$).
   - Enforces RISC-V memory barriers (`fence`) to prevent out-of-order peripheral pin manipulation.
   - Emulates open-drain capability for bit-banged protocols by toggling direction registers.
2. **High-Speed External Interrupt Engine (`exti_aries.c`)**:
   - Demultiplexes PLIC IRQ 10 (Bank 0) and IRQ 11 (Bank 1).
   - Replaces linear 16-channel ISR scanning with an $O(1)$ bitmask dispatch engine powered by `__builtin_ctz`, eliminating latency during 1.0 kHz IMU DRDY triggers.
3. **Timer 1 Ascending Deadline Soft-PWM Scheduler (`pwm_motor_aries.c`)**:
   - Dedicates Synopsys DW_apb_timer Hardware Timer 1 (IRQ 8) to driving 4 motor channels (`PA1`–`PA4`).
   - Uses an ascending sorted deadline event algorithm, setting pins HIGH at $t=0$ and scheduling hardware timer compare interrupts for trailing falling edges, reducing CPU utilization across all 4 channels to $< 3\,\mu\text{s}$ ($< 0.3\%$ CPU load).
   - Connects a complete non-null `motorVTable_t` interface, preventing hard faults during Betaflight Configurator "Motors" slider testing.

---

## 2. Microcomponent Decomposition

```
PLAN/04_GPIO_EXTI_AND_HARDWARE_TIMERS.md
 ├── Microcomponent 04_A: Flat Dual-Bank Address-Decoded Atomic GPIO Driver (io_aries.c)
 ├── Microcomponent 04_B: Open-Drain Simulation & RISC-V Memory Ordering Fences
 ├── Microcomponent 04_C: EXTI Controller, PLIC IRQ 10/11 Demuxing & __builtin_ctz Fast-Path (exti_aries.c)
 ├── Microcomponent 04_D: Hardware Timer 1 Non-Blocking Ascending Deadline Motor Scheduler (pwm_motor_aries.c)
 └── Microcomponent 04_E: Non-Null Motor VTable & Configurator Interactive Control
```

---

### Microcomponent 04_A: Flat Dual-Bank Address-Decoded Atomic GPIO Driver (io_aries.c)

#### 1. Why (Silicon Necessity & Architectural Traceability)
* **Flat 32-Pin Architecture**: Unlike STM32 (which scatters GPIO across 9 sparse ports `GPIOA`..`GPIOI`, requiring complex popcount tables), THEJAS32 exposes a flat single port of 32 pins (`GPIO0` to `GPIO31`).
* **SRAM Efficiency**: A fixed array of 32 `ioRec_t` records occupies exactly 256 bytes in `.bss`, with $O(1)$ direct array index lookup `&ioRecs[tag - 1]`.
* **Hardware Address-Decoded IO Bus**: THEJAS32 does not use ARM-style `BSRR` or Bit-Band registers. Instead, the peripheral bus decoder maps each individual bit to a distinct memory address:
  $$\text{Address} = \text{Base} + ((1 \ll \text{pin}) \ll 2)$$
  Writing to this calculated address modifies only that specific bit. A single RISC-V store instruction (`sw`) sets or clears the pin atomically, completely eliminating read-modify-write race hazards without disabling global interrupts.

#### 2. How (Detailed Algorithmic & Register Execution)
1. **Bank Division & Base Addresses**:
   - Bank 0: `0x10080000` (Pins 0 to 15, `PA0` to `PA15`)
   - Bank 1: `0x10180000` (Pins 16 to 31, `PA16` to `PA31`)
   - Direction Registers: `+0x00` (1 = Output, 0 = Input).
2. **Direct Atomic Pin Write Logic**:
   - Determine bank base and relative pin:
     ```c
     uint32_t base = (pin < 16) ? 0x10080000 : 0x10180000;
     uint32_t pin_in_bank = (pin < 16) ? pin : (pin - 16);
     volatile uint32_t *reg = (volatile uint32_t *)(base + ((1U << pin_in_bank) << 2));
     ```
   - Pin High (`IOHi`): `*reg = (1U << pin_in_bank);`
   - Pin Low (`IOLo`): `*reg = 0;`
   - Single-cycle execution, race-free, zero critical section overhead.
3. **Tag Mapping Engine**:
   - `DEFIO_TAG_MAKE(pin)`: Evaluates to `(ioTag_t)(pin + 1)`.
   - `DEFIO_TAG_PIN(tag)`: Evaluates to `(tag - 1)`.
   - Fast lookup: `IOGetByTag(tag)` validates `1 <= tag <= 32` and returns `&ioRecs[tag - 1]`.

#### 3. Expected Inputs
* **Pin Tags**: `DEFIO_TAG_MAKE(0)` through `DEFIO_TAG_MAKE(31)`.
* **Operations**: `IOInit`, `IOConfigGPIO`, `IOHi`, `IOLo`, `IOToggle`, `IORead`.

#### 4. Expected Results
* Direct $O(1)$ pointer resolution in $< 5\text{ ns}$.
* Atomic pin setting/clearing with 0 read-modify-write races.
* Static `.bss` allocation of exactly 256 bytes for all 32 pins.

---

### Microcomponent 04_B: Open-Drain Simulation & RISC-V Memory Ordering Fences

#### 1. Why (Silicon Necessity & Architectural Traceability)
* **Weak Memory Ordering on RISC-V**: The RV32IM architecture allows memory store reordering unless bounded by a memory fence. In high-speed bit-banging or SPI CS toggling, a CS pin could theoretically be raised or lowered after data transfers begin if stores are reordered by the pipeline.
* **Absence of Hardware Open-Drain Pads**: THEJAS32 IO pads are push-pull CMOS buffers. They lack open-drain output drivers. If a pin configured for I2C (or shared bus) is driven actively HIGH while a slave pulls LOW, direct electrical contention occurs, potentially damaging the pad or locking the bus.

#### 2. How (Detailed Algorithmic & Register Execution)
1. **Explicit Memory Fence Insertion**:
   - After every pin modification in `IOHi()`, `IOLo()`, and `IOConfigGPIO()`, emit an inline memory barrier:
     ```c
     __asm__ __volatile__("fence" ::: "memory");
     ```
   - Guarantees all preceding peripheral bus stores retire before any subsequent bus transactions are issued.
2. **Open-Drain Simulation (`IOCFG_OUT_OD`)**:
   - Maintain a bitmask `uint32_t open_drain_mask` in driver state.
   - When configured as `IOCFG_OUT_OD`:
     - Clear the data output bit to `0`.
   - In `IOWrite()` / `IOHi()` / `IOLo()`:
     - To drive LOW (active state): Configure pin direction as **Output** (`DIR |= (1 << pin)`). The pin actively drives to 0V.
     - To drive HIGH (released state): Configure pin direction as **Input** (`DIR &= ~(1 << pin)`). The external pull-up resistor pulls the bus up to 3.3V without electrical conflict.

#### 3. Expected Inputs
* **IO Modes**: `IOCFG_OUT_PP`, `IOCFG_OUT_OD`, `IOCFG_IN_FLOATING`.
* **Output Levels**: High (true) or Low (false).

#### 4. Expected Results
* 100% strict memory ordering for all GPIO transactions.
* Safe electrical open-drain behavior on shared lines without bus contention.

---

### Microcomponent 04_C: EXTI Controller, PLIC IRQ 10/11 Demuxing & `__builtin_ctz` Fast-Path (exti_aries.c)

#### 1. Why (Silicon Necessity & Architectural Traceability)
* **The High-Frequency Gyro ISR**: The ICM-42688P IMU asserts its Data Ready (`DRDY`) interrupt pin (`PA0`) at 1.0 kHz (every $1000\,\mu\text{s}$) to trigger the flight controller PID calculation.
* **Linear Iteration Bottleneck**: In earlier code ([`src/platform/ARIES/exti_aries.c#L26-L34`](file:///home/ivan/Desktop/betaflight/src/platform/ARIES/exti_aries.c#L26-L34)), the EXTI handler looped through all 16 pins linearly, executing 15 useless branch checks on every single gyro interrupt.
* **Silicon Topology**:
  - Bank 0 (Pins 0..15): Mapped to PLIC IRQ 10.
  - Bank 1 (Pins 16..31): Mapped to PLIC IRQ 11.
* **The Solution**: An $O(1)$ bitmask dispatcher using the GCC `__builtin_ctz` (Count Trailing Zeros) intrinsic.

#### 2. How (Detailed Algorithmic & ISR Architecture)
1. **Channel Tracking Architecture**:
   - Maintain an active channel bitmask: `static volatile uint32_t exti_bank0_active_mask = 0;` and `exti_bank1_active_mask = 0;`.
   - In `EXTIEnable(channel)`:
     ```c
     if (pin < 16) exti_bank0_active_mask |= (1U << pin);
     else exti_bank1_active_mask |= (1U << (pin - 16));
     ```
   - In `EXTIDisable(channel)`:
     ```c
     if (pin < 16) exti_bank0_active_mask &= ~(1U << pin);
     else exti_bank1_active_mask &= ~(1U << (pin - 16));
     ```
2. **Fast-Path $O(1)$ ISR Dispatch**:
   - Bank 0 Handler (`thejas32_gpio0_isr` - IRQ 10):
     ```c
     void thejas32_gpio0_isr(void) {
         uint32_t active = exti_bank0_active_mask;
         while (active) {
             int pin = __builtin_ctz(active);
             extiChannelRec_t *rec = &extiChannels[pin];
             if (rec->handler && rec->handler->fn) {
                 rec->handler->fn(rec->handler);
             }
             active &= ~(1U << pin); // Clear processed bit
         }
     }
     ```
   - For `PA0` (Gyro DRDY), `__builtin_ctz(1)` evaluates to `0` in a single machine cycle. The handler executes immediately and loops zero extra times.
3. **Betaflight EXTI API Compliance**:
   - Implements full abstraction contract: `EXTIInit()`, `EXTIConfig()`, `EXTIEnable()`, `EXTIDisable()`, `EXTIRelease()`.
   - Clean integration with `src/main/drivers/accgyro/accgyro_spi_icm42688p.c`.

#### 3. Expected Inputs
* **Hardware Interrupts**: PLIC IRQ 10 (Bank 0) and IRQ 11 (Bank 1).
* **Sensor Events**: ICM-42688P DRDY rising edge on `PA0`.

#### 4. Expected Results
* EXTI interrupt dispatch latency reduced from $> 1.8\,\mu\text{s}$ down to $< 0.15\,\mu\text{s}$.
* Zero branch overhead or cache pollution from inactive GPIO channels.
* 100% reliable IMU synchronization at 1.0 kHz.

---

### Microcomponent 04_D: Hardware Timer 1 Non-Blocking Ascending Deadline Motor Scheduler (pwm_motor_aries.c)

#### 1. Why (Silicon Necessity & Architectural Traceability)
* **The Missing Advanced Timer IP**: Unlike STM32 (which features advanced multi-channel PWM timers like `TIM1`/`TIM8` with automated hardware compare registers), THEJAS32 provides Synopsys DesignWare APB Timers (simple 32-bit countdown timers with single reload registers).
* **The Busy-Wait Trap**: A naive implementation without hardware multi-channel PWM would use software busy loops (`delayMicroseconds()`), locking the CPU for 1.0 to 2.0 ms on every flight cycle and starving the PID controller.
* **Ground Truth 6 (Ascending Deadline Event Scheduling)**:
  - Timer 0 (`0x10000500`, IRQ 7): Dedicated to system timebase (`micros()`).
  - Timer 1 (`0x10000514`, IRQ 8): Dedicated to ESC pulse generation.
  - Generates 4 independent motor pulses (`PA1`–`PA4`) using $< 3\,\mu\text{s}$ total CPU time ($> 99.7\%$ idle headroom).

#### 2. How (Detailed Algorithmic & Timer State Machine)
1. **Physical Actuator Pin Assignments**:
   - Motor 1: `PA1` (Header J2, Pin 86)
   - Motor 2: `PA2` (Header J2, Pin 85)
   - Motor 3: `PA3` (Header J2, Pin 82)
   - Motor 4: `PA4` (Header J1, Pin 81)
2. **Cycle Initiation (`pwmCompleteMotorUpdate()`)**:
   - **Step 1: Preemptive Timer Disable**:
     ```c
     THEJAS32_TIMER(1).Control = 0; // Disable Timer 1 immediately to prevent race conditions
     ```
   - **Step 2: Ascending Deadline Sorting**:
     - Extract the 4 pulse width requests (in CPU clock cycles at 100 MHz: $1000\,\mu\text{s} = 100{,}000\text{ cycles}$; $2000\,\mu\text{s} = 200{,}000\text{ cycles}$).
     - Sort motor indices into array `sortedMotors[4]` such that:
       $$\text{cycles}[0] \le \text{cycles}[1] \le \text{cycles}[2] \le \text{cycles}[3]$$
   - **Step 3: Simultaneous Pin Raise**:
     - Set all 4 motor pins HIGH simultaneously using atomic address-decoded IO writes.
   - **Step 4: Arm First Deadline**:
     - Set `currentMotorIdx = 0`.
     - Load `THEJAS32_TIMER(1).LoadCount = sortedMotors[0].cycles`.
     - Enable Timer 1: `THEJAS32_TIMER(1).Control = 0x03` (User-defined countdown mode + Interrupt Enable).
3. **Interrupt Service Routine Execution (`thejas32_pwm_timer1_isr` - IRQ 8)**:
   - **Step 1: Clear Hardware EOI**:
     - Read `volatile uint32_t dummy = THEJAS32_TIMER(1).EOI;` to clear the Synopsys DW APB timer interrupt request.
   - **Step 2: Drop Pin LOW**:
     - Drop the active motor pin LOW: `IOLo(pwmMotors[sortedMotors[currentMotorIdx].pin].io);`.
   - **Step 3: Process Coincident or Sub-Threshold Deadlines (100-Cycle Grouping)**:
     - If the next motor's deadline is within 100 cycles ($1.0\,\mu\text{s}$), drop its pin LOW immediately to avoid ISR reentrancy latency:
       ```c
       while ((currentMotorIdx + 1 < sortedMotorCount) &&
              (sortedMotors[currentMotorIdx + 1].cycles - sortedMotors[currentMotorIdx].cycles < 100)) {
           currentMotorIdx++;
           IOLo(pwmMotors[sortedMotors[currentMotorIdx].pin].io);
       }
       ```
   - **Step 4: Reload Next Delta or Terminate**:
     - Advance `currentMotorIdx++`.
     - If `currentMotorIdx < sortedMotorCount`:
       - Calculate delta: $\Delta = \text{cycles}[current] - \text{cycles}[current - 1]$.
       - Reload: `THEJAS32_TIMER(1).LoadCount = \Delta;`
       - Re-enable: `THEJAS32_TIMER(1).Control = 0x03;`
     - Else:
       - All 4 motors complete; leave Timer 1 disabled (`Control = 0`) until the next flight cycle.

#### 3. Expected Inputs
* **Motor Commands**: 4 floating-point throttle values from Betaflight mixer ($1000$ to $2000\,\mu\text{s}$).
* **Hardware Timer**: Synopsys DW APB Timer 1 on 100 MHz clock.

#### 4. Expected Results
* 4 flawless motor PWM pulses delivered at 400 Hz (Standard PWM) or OneShot125.
* Total CPU time for all 4 channels $< 3\,\mu\text{s}$ per cycle.
* Zero jitter or missed falling edges; completely race-free operation.

---

### Microcomponent 04_E: Non-Null Motor VTable & Configurator Interactive Control

#### 1. Why (Silicon Necessity & Architectural Traceability)
* **The Configurator Slider Trap**: When testing motors in the Betaflight Configurator "Motors" tab, the GUI issues commands that call `motorConvertFromExternal()` and `motorConvertToExternal()`.
* **The ESP32 Post-Mortem Bug**: If a platform leaves these function pointers `NULL` in `motorVTable_t`, touching any motor slider immediately triggers an unrecoverable Illegal Instruction / Jump Exception (`mcause = 2/12`).
* **Interactive Motor Spinning**: Allows verifying motor direction and ESC calibration directly on the bench.

#### 2. How (Detailed Algorithmic & Code Architecture)
1. **Linear Passthrough Conversion Functions**:
   ```c
   static float thejas32PwmConvertFromExternal(uint16_t externalValue) {
       return (float)externalValue; // Non-null passthrough prevents hard fault
   }

   static uint16_t thejas32PwmConvertToExternal(float motorValue) {
       return (uint16_t)motorValue;
   }
   ```
2. **Complete Virtual Table Definition**:
   ```c
   static const motorVTable_t thejas32MotorVTable = {
       .convertExternalToMotor = thejas32PwmConvertFromExternal,
       .convertMotorToExternal = thejas32PwmConvertToExternal,
       .enable = thejas32PwmEnable,
       .disable = thejas32PwmDisable,
       .isMotorEnabled = thejas32PwmIsEnabled,
       .write = thejas32PwmWrite,
       .writeInt = thejas32PwmWriteInt,
   };
   ```
3. **Safety Clamping**:
   - In `thejas32PwmWrite()`:
     - Constrain command values strictly between $1000.0\text{f}$ and $2000.0\text{f}$.
     - When disarmed or disabled, force all 4 motor duty cycles to idle ($1000\,\mu\text{s}$ / $100{,}000\text{ counts}$) or zero.

#### 3. Expected Inputs
* **Configurator Commands**: Interactive motor slider inputs from MSP commands.
* **Mixer Throttle Values**: Flight controller PID outputs.

#### 4. Expected Results
* Configurator "Motors" slider tool functions interactively with zero hard faults.
* Motors spin smoothly on physical pins `PA1`–`PA4`.
* Safe disarm behavior guaranteed.

---

## 3. Subsystem Traceability Matrix

| Microcomponent ID | Functionality / Feature | Target Source Files | Silicon Verification Target |
| :--- | :--- | :--- | :--- |
| **04_A** | Address-Decoded Atomic GPIO Driver | `src/platform/ARIES/io_aries.c` | Single `sw` instruction atomic pin writes; 256 bytes SRAM allocation. |
| **04_B** | Memory Ordering Fences & Open-Drain Emulation | `src/platform/ARIES/io_aries.c` | Strict memory consistency; no electrical bus contention on open-drain pins. |
| **04_C** | EXTI Demuxing & `__builtin_ctz` Fast-Path | `src/platform/ARIES/exti_aries.c` | $O(1)$ dispatch for 1.0 kHz ICM-42688P DRDY on `PA0`; $< 0.15\,\mu\text{s}$ latency. |
| **04_D** | Timer 1 Non-Blocking Ascending Deadline Scheduler | `src/platform/ARIES/pwm_motor_aries.c` | 4 ESC channels (`PA1`–`PA4`) updated with $< 3\,\mu\text{s}$ total CPU load. |
| **04_E** | Non-Null Motor VTable & Configurator Control | `src/platform/ARIES/pwm_motor_aries.c` | Non-null `convertExternalToMotor`; 0 slider crashes in Configurator. |

---

## 4. Anti-Assumption Guardrails

1. **No Hardware Bit-Band or BSRR Registers**:
   - *Never* assume ARM-style `BSRR` exists. Always use THEJAS32's address-decoded bus offset `BASE + ((1 << pin) << 2)`.
2. **Never Linear Scan 16 Pins in High-Frequency EXTI ISR**:
   - *Never* iterate through 16 pin descriptors on every gyro interrupt. Always use `__builtin_ctz` over active bitmasks.
3. **No Busy-Wait Delay Loops for ESC PWM**:
   - *Never* use `delayMicroseconds()` in the motor driver. Always use Timer 1 ascending deadline scheduling.
4. **Mandatory Non-Null Motor VTable Pointers**:
   - *Never* leave `convertExternalToMotor` or `convertMotorToExternal` as `NULL`. Always bind valid passthrough functions.