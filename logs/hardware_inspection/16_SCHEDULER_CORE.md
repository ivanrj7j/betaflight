# Hardware Inspection Log: `src/main/scheduler/scheduler.c`

## 1. File Purpose & Role in Architecture
* **File Path**: `src/main/scheduler/scheduler.c`
* **Subsystem**: Cooperative Multi-Tasking Scheduler, Gyro Sync Phase-Locked Loop, and Dynamic Priority Queue.
* **Key Components**:
  * `scheduler()`: Master scheduling function executed continuously inside `run()`.
  * Realtime Gyro Lock: Synchronizes the main loop phase directly with the Gyro Data-Ready EXTI interrupt (`gyro->gyroSyncEXTI`).
  * Dynamic Priority Aging: Time-driven and event-driven task dispatching.
  * `TASK_SERIAL` Protection: Explicit starvation bypass guaranteeing serial MSP/CLI traffic is processed without jitter.

---

## 2. Hardware Challenges STM32 Developers Faced & How They Overcame Them

### Challenge 1: Gyro Sampling Jitter and Clock Drift (Phase Lock Loop)
* **The Problem**: The IMU sensor (e.g. MPU6000 or ICM42688P) has its own internal RC oscillator that drifts with temperature. The MCU system clock (driven by HSE crystal) also drifts slightly. If the flight controller scheduler runs at a fixed timer interval, the MCU and Gyro drift in and out of phase, causing beat frequencies, aliasing, and violent motor oscillations!
* **How It Was Overcome**:
  The scheduler implements a software Phase Locked Loop (PLL):
  1. The Gyro Data-Ready pin triggers an EXTI interrupt which stamps `gyro->gyroSyncEXTI` with the hardware cycle counter (`getCycleCounter()`).
  2. The scheduler dynamically measures the phase skew (`gyroSkew = cmpTimeCycles(nextTargetCycles, gyro->gyroSyncEXTI) % desiredPeriodCycles`).
  3. Every `GYRO_LOCK_COUNT` cycles, the scheduler shifts its target execution time (`lastTargetCycles -= accGyroSkew / GYRO_LOCK_COUNT`), pulling the flight controller into perfect lockstep with the sensor silicon!

---

### Challenge 2: Serial Starvation by Heavy Flight Tasks
* **The Problem**: When the PID loop, filter calculations, and blackbox logging consume large slices of CPU cycles, `TASK_SERIAL` could be postponed past its deadline, causing USB packet buffer overflows and lost Configurator commands.
* **How It Was Overcome**:
  Betaflight hardcodes an explicit starvation exemption in the task selector:
  ```c
  if ((taskRequiredTimeCycles < schedLoopRemainingCycles) ||
      ((scheduleCount & SCHED_TASK_DEFER_MASK) == 0) ||
      ((task - tasks) == TASK_SERIAL)) {
      selectedTaskDynamicPriority = task->dynamicPriority;
      selectedTask = task;
  }
  ```
  `TASK_SERIAL` is never deferred due to execution time guards, ensuring the PC Configurator handshake remains responsive.

---

## 3. Direct Porting Blueprint for ARIES v3.0 (THEJAS32 / VEGA ET1031)

1. **Deterministic Timebase via RISC-V Counters**:
   * The scheduler depends directly on two fundamental time functions:
     * `getCycleCounter()`: Used for cycle-accurate task execution timing. On RISC-V, this reads the `cycle` CSR.
     * `micros()`: Used for task period checking. On RISC-V, this reads the 100 MHz `mtime` timer.
2. **Behavior on Minimal Target Without Gyro**:
   * In our initial <128KB Configurator port, the gyro is disabled (`gyroEnabled = false`).
   * When `!gyroEnabled`, the scheduler bypasses the entire Gyro PLL synchronization block and operates purely as a lightweight round-robin dynamic scheduler.
   * `TASK_SYSTEM` (10 Hz) and `TASK_SERIAL` (100 Hz) run smoothly, servicing `UART0` and replying to Configurator handshake packets without delay!
