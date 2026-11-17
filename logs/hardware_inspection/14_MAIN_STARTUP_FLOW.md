# Hardware Inspection Log: `src/main/main.c`

## 1. File Purpose & Role in Architecture
* **File Path**: `src/main/main.c`
* **Subsystem**: Universal Application Lifecycle Entry Point and Main Execution Loop.
* **Key Components**:
  * `main()`: Master C entry point following assembly startup.
  * Phased Initialization: `systemInit()` $\rightarrow$ `initPhase1()` $\rightarrow$ `initPhase2()` $\rightarrow$ `initPhase3()`.
  * `run()`: Cooperative scheduling loop executing `scheduler()`.

---

## 2. Hardware Challenges STM32 Developers Faced & How They Overcame Them

### Challenge 1: Peripheral Initialization Order Hazards
* **The Problem**: Initializing complex peripherals in the wrong order causes bus hangs. For example:
  * Initializing USB before memory structures are valid causes USB enumeration packets to write into unallocated memory.
  * Initializing sensor drivers before SPI/I2C bus pins are clocked causes peripheral bus timeout hangs.
  * Initializing timers before NVIC grouping is set causes priority inversion and deadlocks.
* **How It Was Overcome**:
  Betaflight strictly decoupled initialization into discrete architectural phases:
  * **`systemInit()`**: Raw silicon bring-up: PLL clocks, vector table base (`VTOR` / `mtvec`), microsecond timebase (`micros()`), and basic peripheral bus clocks.
  * **`initPhase1()`**: Static data structures: Global pin records (`IOInitGlobal()`), parameter group registration (`pg.c`), and hardware UID.
  * **`initPhase2()`**: Physical bus drivers: SPI, I2C, Timers, ADC, and hardware sensors.
  * **`initPhase3()`**: Communication interfaces: Opening serial ports (UART/VCP), attaching MSP protocol engines, and populating the scheduler task table.

---

### Challenge 2: USB Bring-Up Timing
* **The Problem**: On STM32, USB VCP initialization must occur only after the main clock tree is locked at 48 MHz and core structures are in place, but before application loops start.
* **How It Was Overcome**:
  `usbVcpInit()` is guarded behind `#ifdef USE_VCP` between `initPhase2()` and `initPhase3()`. If `USE_VCP` is disabled, the system boots cleanly without USB initialization delays.

---

## 3. Direct Porting Blueprint for ARIES v3.0 (THEJAS32 / VEGA ET1031)

1. **Clean Single-Core Execution**:
   * Multicore macros (`ENABLE_MULTICORE_INIT`, `USE_MULTICORE`) are omitted.
   * `main()` cleanly calls:
     ```c
     systemInit(); // THEJAS32 PLL, PLIC, Timer 0
     initPhase1(); // IO, parameter groups
     initPhase2(); // SPI0, I2C0, UART0
     initPhase3(); // Open UART0 for MSP @ 115200 baud
     run();        // Enter cooperative scheduler
     ```
2. **`USE_VCP` Disabled by Default**:
   * By disabling `USE_VCP` in `target.h`, `usbVcpInit()` is compiled out.
   * `UART0` is registered as the default console in `initPhase3()`, connecting directly through the CP2102N USB bridge to the host PC.
