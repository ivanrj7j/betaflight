# Domain Plan 06: Sensors, RC Telemetry & Scheduler

> **Component Plan ID**: `PLAN-DOM-06`  
> **Parent Architecture Document**: [`PLAN/00_MASTER_PLAN_OVERVIEW.md`](00_MASTER_PLAN_OVERVIEW.md)  
> **Primary Knowledge References**:
> - [`knowledgebase/codebase_evaluations/12_EVAL_STUB_DRIVERS_C.md`](../knowledgebase/codebase_evaluations/12_EVAL_STUB_DRIVERS_C.md)
> - [`knowledgebase/components/08_ANALOG_ADC_SUBSYSTEM.md`](../knowledgebase/components/08_ANALOG_ADC_SUBSYSTEM.md)
> - [`knowledgebase/components/10_SCHEDULER_AND_TASK_ARCHITECTURE.md`](../knowledgebase/components/10_SCHEDULER_AND_TASK_ARCHITECTURE.md)
> - [`knowledgebase/CRITICAL_KNOWLEDGE_GAPS_AND_ASSUMPTIONS.md`](../knowledgebase/CRITICAL_KNOWLEDGE_GAPS_AND_ASSUMPTIONS.md)

---

## 1. Executive Domain Summary

The Sensors, Radio Control (RC) Telemetry, Cooperative Task Scheduler, and Runtime Stub subsystem orchestrates the overall flight operations on C-DAC ARIES v3.0 (THEJAS32 / VEGA ET1031):
1. **Serial Receiver Engine (CRSF / ExpressLRS on `UART1`)**:
   - The 16550 UART IP lacks hardware signal inversion. Inverted protocols (e.g. legacy FrSky SBUS) fail with $100\%$ framing errors without an external hardware transistor inverter.
   - Employs **ExpressLRS (CRSF)** natively over pins `PA16` (TX) and `PA17` (RX) at 420,000 baud (Divisor 15, $-0.79\%$ error on the 100 MHz APB bus), providing low-latency non-inverted serial RC telemetry.
2. **Battery Voltage & Analog Telemetry Architecture**:
   - Header J10 analog inputs connect to **`U9`**, an external 4-channel I2C ADC chip on `I2C2` (`PA32`/`PA31`).
   - Implements a dual-mode strategy: an anti-lockout safe fallback (nominal 12.6V battery cache / `#undef USE_ADC`) to eliminate fatal `ARMING_DISABLED_BATTERY` lockouts, backed by an asynchronous periodic reader for `U9`.
3. **1.0 kHz Cooperative Task Scheduler (`scheduler.c`)**:
   - RV32IM soft-float processing budget is clamped strictly to **1.0 kHz** (`DEFAULT_PID_PROCESS_DENOM 8`), allocating $1000\,\mu\text{s}$ per flight cycle.
   - PID math executes in $\approx 290\,\mu\text{s}$ to $420\,\mu\text{s}$, leaving $> 58\%$ idle CPU headroom.
   - Zero-overhead cooperative execution shares a single stack (`_estack`), saving $> 20\text{ KB}$ of SRAM compared to preemptive RTOS stacks.
4. **State-Preserving RV32IM Software Atomics & Isolation Shims (`stub_drivers.c`)**:
   - Provides GCC 13+ `__atomic_fetch_add_4` and atomic built-ins using state-preserving CSR `mstatus.MIE` manipulation, preventing nested interrupt corruption.
   - Supplies flat DMA and persistent reset-reason stubs, isolating upstream Betaflight code.

---

## 2. Microcomponent Decomposition

```
PLAN/06_SENSORS_RC_TELEMETRY_AND_SCHEDULER.md
 ├── Microcomponent 06_A: Serial Receiver Subsystem & Non-Inverted CRSF/ELRS Engine (UART1)
 ├── Microcomponent 06_B: Analog ADC Architecture & Safe Battery Telemetry Fallback (adc_aries.c, U9)
 ├── Microcomponent 06_C: 1.0 kHz Cooperative Task Scheduler & Phased System Initialization (main.c)
 ├── Microcomponent 06_D: State-Preserving RV32IM Software Atomics & Linker Isolation Shims (stub_drivers.c)
 └── Microcomponent 06_E: Flight Loop Timing Budgets, Soft-Float Math Profiling & CPU Headroom Verification
```

---

### Microcomponent 06_A: Serial Receiver Subsystem & Non-Inverted CRSF/ELRS Engine (UART1)

#### 1. Why (Silicon Necessity & Architectural Traceability)
* **The 16550 Hardware Inversion Limitation**: STM32F3/F7/H7 microcontrollers feature hardware GPIO pin inverters capable of flipping signal polarities internally. The 16550 UART on THEJAS32 silicon has **no internal hardware inverters**.
* **The SBUS Inversion Hazard**: Standard SBUS uses inverted UART logic ($V_{\text{idle}} = \text{LOW}$, $3.3\text{V} = \text{SPACE}$). Connecting an SBUS receiver directly to `PA17` produces $100\%$ framing errors, garbled RC channels, and immediate failsafe lockout.
* **The Solution (Native Non-Inverted CRSF / ExpressLRS)**:
  - Crossfire (CRSF) / ExpressLRS uses standard non-inverted 3.3V TTL UART logic ($V_{\text{idle}} = \text{HIGH}$).
  - Runs natively on `PA16` (TX) and `PA17` (RX) at 420,000 baud.
* **Divisor Precision**:
  $$\text{Divisor} = \frac{100{,}000{,}000}{16 \times 420{,}000} \approx 14.88 \implies \text{Divisor } 15 \ (416{,}666.7\text{ baud}, -0.79\%\text{ error})$$
  This error rate is well within the standard asynchronous serial tolerance ($\pm 2.0\%$).

#### 2. How (Detailed Algorithmic & Receiver Implementation)
1. **Target Header Configuration**:
   - In [`src/platform/ARIES/target/ARIESV3/target.h`](../src/platform/ARIES/target/ARIESV3/target.h):
     ```c
     #define USE_SERIALRX
     #define USE_SERIALRX_CRSF
     #define SERIALRX_UART        SERIAL_PORT_USART2 // Maps to UART1
     #define SERIALRX_PROVIDER    SERIALRX_CRSF
     ```
2. **UART1 Hardware Initialization**:
   - Base Address: `0x10000200` (IRQ 3).
   - Set `LCR_DLAB = 1`, write `DLL = 15`, `DLM = 0`, clear `LCR_DLAB`.
   - Configure framing: `8N1` (`0x03`).
   - Enable 16-byte hardware FIFO: `FCR = 0x07`.
   - Enable RX-only interrupt: `IER = 0x01`.
3. **CRSF Packet Parser Integration (`rx_crsf.c`)**:
   - `TASK_RX` polls `serialRxBytesWaiting()` on `UART1`.
   - Ingests incoming 64-byte telemetry frames:
     `[Device Address] [Frame Length] [Type] [Payload...] [CRC8]`.
   - Unpacks 16 RC channels ($11\text{-bit}$ resolution, $172$–$1811$ range) into Betaflight `rcData[]`.
   - Detects Link Statistics frames to report RSSI, SNR, and transmitter RF power to Betaflight OSD/telemetry.

#### 3. Expected Inputs
* **Physical Interface**: ExpressLRS receiver wired to `PA16` (TX) and `PA17` (RX).
* **Baud Rate**: 420,000 bps.

#### 4. Expected Results
* 16-channel RC control updated at up to 250 Hz.
* Zero framing errors or dropped packets.
* Receiver Link Quality (LQ) and RSSI accurately reported.

---

### Microcomponent 06_B: Analog ADC Architecture & Safe Battery Telemetry Fallback (adc_aries.c, U9)

#### 1. Why (Silicon Necessity & Architectural Traceability)
* **The Physical Hardware Reality**: ARIES v3.0 schematic confirms that Header J10 analog inputs (`ADC_CH0`–`ADC_CH3`) are wired to **`U9`**, an external 4-channel I2C ADC chip on `I2C2` (`PA32`/`PA31`).
* **The Fatal Arming Lockout Trap**: In previous iterations, `stub_drivers.c` implemented `adcGetValue()` returning raw `0`. Betaflight's battery monitor interpreted this as 0.0V, declaring a dead battery and permanently asserting the `ARMING_DISABLED_BATTERY` flag.
* **The Dual-Mode Strategy**:
  - **Mode 1 (Safety Fallback)**: When `USE_ADC` is undefined, battery status reports `BATTERY_NOT_PRESENT`, preventing arming lockouts.
  - **Mode 2 (U9 I2C Periodic Telemetry)**: When battery sensing is required, poll `U9` asynchronously over `I2C2` at 10 Hz into a static RAM cache, without blocking the 1.0 kHz flight loop.

#### 2. How (Detailed Algorithmic & Telemetry Implementation)
1. **Static Memory Cache**:
   ```c
   static volatile uint16_t aries_adc_raw[4] = {
       2048, // Nominal 12.6V on 11:1 voltage divider (~3S full)
       0,    // Zero current draw
       1024, // RSSI
       0     // Aux
   };
   ```
2. **Safe Value Getter (`adcGetValue`)**:
   ```c
   uint16_t adcGetValue(adcSource_e source) {
       switch (source) {
           case ADC_BATTERY:
               return aries_adc_raw[0];
           case ADC_CURRENT:
               return aries_adc_raw[1];
           case ADC_RSSI:
               return aries_adc_raw[2];
           default:
               return 0;
       }
   }
   ```
3. **Asynchronous I2C2 Reader for U9 (`TASK_BATTERY`)**:
   - `TASK_BATTERY` runs at 20 Hz (every 50 ms).
   - If I2C2 bus is idle:
     - Issue read command to `U9` (I2C address `0x48` or `0x49`).
     - Clock in 8 bytes (4 channels $\times 12\text{-bit}$ resolution).
     - Store into `aries_adc_raw[0..3]`.
   - If I2C2 transaction is busy or times out, retain previous cached readings.

#### 3. Expected Inputs
* **Hardware Sources**: `U9` I2C ADC chip on Header J10.
* **Scheduler Task**: `TASK_BATTERY` (20 Hz).

#### 4. Expected Results
* Battery voltage reports accurately without blocking the flight loop.
* Zero `ARMING_DISABLED_BATTERY` lockouts when battery sensing is disabled.
* Configurator GUI top status bar reflects valid battery telemetry.

---

### Microcomponent 06_C: 1.0 kHz Cooperative Task Scheduler & Phased System Initialization (main.c)

#### 1. Why (Silicon Necessity & Architectural Traceability)
* **Cooperative Determinism**: Betaflight's scheduler is strictly non-preemptive. Tasks execute sequentially on a single shared stack (`_estack`), saving $10\text{--}40\text{ KB}$ of SRAM compared to preemptive RTOS stacks (e.g. FreeRTOS).
* **Phased Initialization Sequencing**: Accessing peripheral registers (e.g. SPI IMU, Motor PWM, or Flash) before system clocks, GPIO directions, and Parameter Groups are initialized results in fatal bus faults.
* **1.0 kHz Loop Clamping**: On an RV32IM soft-float core at 100 MHz, the flight control task (`TASK_GYROPID`) must run at 1.0 kHz (`DEFAULT_PID_PROCESS_DENOM 8`) to guarantee $> 58\%$ idle CPU headroom.

#### 2. How (Detailed Algorithmic & Phased Boot Architecture)
1. **Phase 1: Core Runtime, Memory & Serial Link**:
   - Initialize system clocks and 64-bit microsecond counter (`systemInit()`).
   - Initialize 32-bit flat GPIO controller (`ioInit()`).
   - Load Parameter Groups from 8 KB RAM buffer (`pgResetAll()`).
   - Initialize UART0 at 115,200 baud (`uart0_init(115200)`).
   - Initialize PLIC interrupt controller (`plic_init()`).
   - *Result: Serial terminal and Configurator handshake can now communicate.*
2. **Phase 2: Actuators, Buses & Sensors**:
   - Initialize Timer 1 motor PWM scheduler (`motorInit()`).
   - Initialize SPI0 bus master on Header J1 (`spiInit(SPIDEV_1)`).
   - Probe ICM-42688P `WHO_AM_I` register (`0x75 == 0x47`).
   - Initialize EXTI DRDY interrupt on `PA0`.
   - *Result: Sensors and motors active and calibrated.*
3. **Phase 3: Battery Monitoring, Arming Checks & Flight Loop**:
   - Initialize ADC cache (`adcInit()`).
   - Verify arming flags (`armingDisableFlags`).
   - Illuminate onboard status LED (`PA22`).
   - **Crucial: Set `mstatus.MIE = 1`** immediately prior to entering `scheduler()`:
     ```c
     __asm__ volatile ("csrs mstatus, 8"); // Enable Machine Interrupts
     while (1) {
         scheduler();
     }
     ```
4. **Cooperative Task Scheduling Table**:
   - `TASK_GYROPID`: Clamped to 1000 µs (1.0 kHz), priority `TASK_PRIORITY_REALTIME`.
   - `TASK_SERIAL`: 100 Hz, priority `TASK_PRIORITY_LOW` (non-blocking byte ingestion).
   - `TASK_RX`: 50 Hz, priority `TASK_PRIORITY_HIGH` (CRSF channel decoding).
   - `TASK_BATTERY`: 20 Hz, priority `TASK_PRIORITY_LOW`.
   - `TASK_SYSTEM`: 10 Hz, priority `TASK_PRIORITY_MEDIUM` (CPU load calculation).

#### 3. Expected Inputs
* **Hardware Interrupts**: Timer 0 timebase, Timer 1 PWM, PLIC UART/EXTI.
* **Task Table**: Betaflight cooperative task definitions.

#### 4. Expected Results
* Complete boot sequence executes in $< 45\text{ ms}$.
* Status LED illuminates solid green on boot completion.
* Tasks execute deterministically with zero deadline misses.

---

### Microcomponent 06_D: State-Preserving RV32IM Software Atomics & Linker Isolation Shims (stub_drivers.c)

#### 1. Why (Silicon Necessity & Architectural Traceability)
* **The Missing Atomic Extension ('A')**: The VEGA ET1031 core implements RV32IM. It does not possess hardware atomic instructions (`lr.w`/`sc.w`). Standard GCC 13+ libraries require atomic built-in functions (e.g. `__atomic_fetch_add_4`). Without custom symbols, linking fails with unresolved external references.
* **The Interrupt Nesting Bug in Earlier Code**: In [`src/platform/ARIES/stub_drivers.c#L35-L42`](../src/platform/ARIES/stub_drivers.c#L35-L42), `__atomic_fetch_add_4` unconditionally executed `__enable_irq()` on exit. If called inside an active ISR, it prematurely set `mstatus.MIE = 1`, unmasking global interrupts and corrupting interrupt nesting.
* **The Solution (State-Preserving MSTATUS Atomic Sequence)**: Capture previous `MIE` status via CSR instruction `csrrci` and restore it only if it was previously set.

#### 2. How (Detailed Algorithmic & CSR Implementation)
1. **State-Preserving Atomic Fetch-Add**:
   ```c
   unsigned int __atomic_fetch_add_4(volatile void *ptr, unsigned int val, int memorder) {
       (void)memorder;
       uint32_t prev_mstatus;
       // Atomically read mstatus and clear bit 3 (MIE)
       __asm__ volatile ("csrrci %0, mstatus, 8" : "=r"(prev_mstatus) :: "memory");
       
       unsigned int old_val = *(volatile unsigned int *)ptr;
       *(volatile unsigned int *)ptr = old_val + val;
       
       // Restore previous interrupt state only if it was originally enabled
       if (prev_mstatus & 8) {
           __asm__ volatile ("csrs mstatus, 8" ::: "memory");
       }
       return old_val;
   }
   ```
2. **Comprehensive Atomic Built-In Shims**:
   - Implement `__atomic_load_4`, `__atomic_store_4`, `__atomic_compare_exchange_4` following the exact state-preserving CSR pattern.
3. **Flat DMA Isolation Stubs**:
   - Provide empty implementations of `dmaInit()`, `dmaAllocate()`, `dmaEnable()`, returning `NULL` / `false` to satisfy Betaflight core dependencies without compiling large DMA controller stacks.
4. **Persistent Reboot Reason Cache**:
   - Maintain an uninitialized 32-bit word in SRAM:
     ```c
     static uint32_t persistentResetCode __attribute__((section(".noinit")));
     ```
   - Retains warm soft-reboot codes (`RESET_BOOTLOADER`, `RESET_MSC`) across CPU resets.

#### 3. Expected Inputs
* **Atomic Requests**: Multi-task counters and ring buffer pointers.
* **Linker Demands**: `libatomic` symbols from GCC.

#### 4. Expected Results
* 100% clean linking with zero unresolved atomic symbols.
* Strict preservation of interrupt nesting inside ISR contexts.
* Zero DMA code bloat in final binary.

---

### Microcomponent 06_E: Flight Loop Timing Budgets, Soft-Float Math Profiling & CPU Headroom Verification

#### 1. Why (Silicon Necessity & Architectural Traceability)
* **The Soft-Float Reality**: THEJAS32 operates on integer-only RV32IM hardware at 100 MHz ($100{,}000{,}000\text{ cycles/second} = 100\text{ cycles}/\mu\text{s}$). Single-precision float operations (Butterworth biquads, PT1 filters, D-term derivations, quaternion matrices) require multi-cycle software emulation in `libgcc.a`.
* **The 1.0 kHz Verification Ceiling**: Running at 1.0 kHz allocates exactly $1000\,\mu\text{s}$ per cycle.
* **Headroom Target**: The flight loop must consume $\le 420\,\mu\text{s}$ WCET (Worst-Case Execution Time), guaranteeing $> 58\%$ idle headroom for serial telemetry and system housekeeping.

#### 2. How (Detailed Timing Budget Breakdown)

```
+-----------------------------------------------------------------------------+
|          1.0 kHz FLIGHT LOOP CYCLE BUDGET: 1,000.0 µs (100,000 Cycles)      |
+------------------------------------+----------------------------------------+
| SUB-TASK COMPONENT                 | WCET DURATION  | CPU LOAD FRACTION     |
+------------------------------------+----------------------------------------+
| 1. ICM-42688P SPI0 Polled Read    |   ~8.96 µs     |       0.90 %          |
| 2. Gyro Lowpass & Biquad Filtering |  ~95.00 µs     |       9.50 %          |
| 3. Attitude Estimation (Ahrs/IMU)  |  ~75.00 µs     |       7.50 %          |
| 4. PID Controller Calculation      | ~120.00 µs     |      12.00 %          |
| 5. Motor Mixer & Range Clamping    |  ~18.00 µs     |       1.80 %          |
| 6. Timer 1 PWM Motor Scheduling    |   ~2.80 µs     |       0.28 %          |
+------------------------------------+----------------------------------------+
| TOTAL ACTIVE FLIGHT CONTROL WCET   | ~319.76 µs     |      31.98 %          |
+------------------------------------+----------------------------------------+
| 7. Available Idle / Telemetry / BG | ~680.24 µs     |      68.02 % IDLE     |
+------------------------------------+----------------------------------------+
```

3. **Runtime Profiling & Assertion Verification**:
   - In `scheduler.c`, instrument `TASK_GYROPID` execution timing:
     ```c
     timeUs_t start = micros();
     taskGyroPid();
     timeUs_t elapsed = micros() - start;
     if (elapsed > maxGyroPidDuration) maxGyroPidDuration = elapsed;
     ```
   - Expose `maxGyroPidDuration` in CLI `tasks` command.
   - Assert that `maxGyroPidDuration < 500` µs under active stick movement and filtering.

#### 3. Expected Inputs
* **Clock Frequency**: 100 MHz.
* **Flight Tasks**: Full PID controller, notch filters, mixer, Timer 1 soft-PWM.

#### 4. Expected Results
* Measured PID execution time $\approx 320\,\mu\text{s}$ ($< 350\,\mu\text{s}$).
* Idle CPU headroom confirmed $> 65\%$.
* Zero task starvation; 0 watchdog resets.

---

## 3. Subsystem Traceability Matrix

| Microcomponent ID | Functionality / Feature | Target Source Files | Silicon Verification Target |
| :--- | :--- | :--- | :--- |
| **06_A** | Non-Inverted CRSF / ExpressLRS Receiver | `src/main/rx/crsf.c`, `serial_uart_aries.c` | 420,000 baud (Divisor 15, $-0.79\%$ error) on `PA16`/`PA17`; 0 framing errors. |
| **06_B** | Analog ADC & Battery Telemetry Fallback | `src/platform/ARIES/adc_aries.c`, `target.h` | Safe nominal cache / `#undef USE_ADC`; 0 `ARMING_DISABLED_BATTERY` lockouts. |
| **06_C** | 1.0 kHz Cooperative Task Scheduler | `src/main/main.c`, `scheduler/scheduler.c` | 1.0 kHz loop rate; phased boot completes in $< 45\text{ ms}$; Status LED active. |
| **06_D** | State-Preserving RV32IM Software Atomics | `src/platform/ARIES/stub_drivers.c` | State-preserving `mstatus.MIE` CSR sequence; clean linking; 0 ISR unmasking traps. |
| **06_E** | Soft-Float Flight Timing & Headroom Budget | `scheduler/scheduler.c`, `flight/pid.c` | Measured WCET $\approx 320\,\mu\text{s}$; verified $> 65\%$ idle CPU headroom. |

---

## 4. Anti-Assumption Guardrails

1. **No Inversion Hardware on UART1**:
   - *Never* attempt to run SBUS without an external hardware inverter transistor. Always prioritize native non-inverted **ExpressLRS (CRSF)**.
2. **Never Return Raw 0V from ADC**:
   - *Never* allow an ADC stub to report 0V, which permanently blocks arming. Either `#undef USE_ADC` or return a nominal cache value.
3. **Never Unconditionally Enable Interrupts in Software Atomics**:
   - *Never* call `__enable_irq()` inside `__atomic_fetch_add_4`. Always restore the prior state of `mstatus.MIE`.
4. **Never Exceed 1.0 kHz Flight Loop Rate**:
   - *Never* configure PID process denominator to 4 kHz or 8 kHz. Soft-float math requires clamping to 1.0 kHz to guarantee adequate CPU headroom.