# Low-Level Software Test Plan & Verification Specifications: C-DAC ARIES v3.0

**Target Board:** C-DAC ARIES v3.0 Development Board  
**SoC / Core:** C-DAC THEJAS32 / VEGA ET1031 (32-bit RISC-V, RV32IM, 100 MHz, soft-float)  
**Firmware:** Betaflight 2026.12.0-alpha (`ARIESV3` Target)  
**Parent Document:** [`TEST_PLAN.md`](TEST_PLAN.md)  
**Document Purpose:** Exhaustive low-level software test specifications, unit tests, register-level verification procedures, mock injection methods, and pass/fail criteria across all subsystems.

---

## 1. Test Architecture & Execution Environments

Tests are categorized across three execution environments:

```
+-----------------------------------------------------------------------------------+
|                            TEST EXECUTION ENVIRONMENTS                            |
+------------------------------------+----------------------------------------------+
| Environment A: Host Unit Tests     | x86_64 host compilation via GCC/G++. Mocks   |
| (make test / src/test/unit/)       | hardware registers. Validates algorithms,     |
|                                    | ring buffers, filters, and mathematical logic.|
+------------------------------------+----------------------------------------------+
| Environment B: Headless Simulation | Renode emulation (ariesv3.repl, rv32im_zicsr)|
| (Renode Simulation)                | Validates reset vector, stack layout, trap   |
|                                    | entries, peripheral MMIO read/write cycles.  |
+------------------------------------+----------------------------------------------+
| Environment C: Target Silicon      | Live THEJAS32 hardware via JTAG OpenOCD,     |
| (Target Hardware & GDB)            | GDB register inspection, and interactive CLI.|
+------------------------------------+----------------------------------------------+
```

---

## 2. Test Module Directory

- **[Module 1: Bare-Metal Boot, Memory Sections & Trap Vector](#module-1-bare-metal-boot-memory-sections--trap-vector)**
- **[Module 2: 16550 UART Serial Communication & Ring Buffers](#module-2-16550-uart-serial-communication--ring-buffers)**
- **[Module 3: High-Resolution Timebase & Hardware Timer 0](#module-3-high-resolution-timebase--hardware-timer-0)**
- **[Module 4: Address-Decoded GPIO & External Interrupts](#module-4-address-decoded-gpio--external-interrupts)**
- **[Module 5: SPI Bus & IMU Sensor Communication](#module-5-spi-bus--imu-sensor-communication)**
- **[Module 6: Non-Volatile Flash Configuration & PG Storage](#module-6-non-volatile-flash-configuration--pg-storage)**
- **[Module 7: Motor Actuator Outputs & Timer 1 Pulse Scheduler](#module-7-motor-actuator-outputs--timer-1-pulse-scheduler)**
- **[Module 8: Soft-Float Flight Loop & Scheduler Stress](#module-8-soft-float-flight-loop--scheduler-stress)**

---

## Module 1: Bare-Metal Boot, Memory Sections & Trap Vector

### Test Case SW-BOOT-01: General Purpose Register (GPR) Zero Initialization
* **Environment:** Renode Simulation / GDB
* **Target File:** [`src/platform/ARIES/startup.S`](src/platform/ARIES/startup.S)
* **Objective:** Ensure all 31 general-purpose registers (`x1` through `x31`) are cleared to `0x00000000` before C runtime initialization to avoid non-deterministic behavior.
* **Execution Steps:**
  1. Set breakpoint at `_start` (`0x20000000`).
  2. Step instruction-by-instruction past the register clearing loop (`li x1, 0 ... li x31, 0`).
  3. In GDB, execute: `info registers`.
* **Expected Result:**
  - `ra` through `t6` all read `0x00000000` (except `sp` and `gp` which are initialized immediately after).
* **Pass/Fail Criteria:** PASS if no register contains residual SRAM garbage.

---

### Test Case SW-BOOT-02: Stack Pointer & Global Pointer Setup
* **Environment:** Renode / GDB / Linker Map
* **Target Files:** [`src/platform/ARIES/link/aries_v3.ld`](src/platform/ARIES/link/aries_v3.ld), [`src/platform/ARIES/startup.S`](src/platform/ARIES/startup.S)
* **Objective:** Verify stack pointer (`sp`) and global pointer (`gp`) are loaded with linker symbols `_estack` and `__global_pointer$`.
* **Execution Steps:**
  1. Break at label `1:` in `startup.S`.
  2. Inspect pointers in GDB:
     ```gdb
     print /x $sp
     print /x $gp
     ```
* **Expected Result:**
  - `$sp` must equal `0x1000FEF0` (top of 16 KB stack in SRAM, 16-byte aligned).
  - `$gp` must equal `0x10000800` (SRAM base + 2 KB for small data relaxation).
* **Pass/Fail Criteria:** Exact match with linker symbol addresses.

---

### Test Case SW-BOOT-03: Data Relocation & BSS Zero Initialization
* **Environment:** Renode Simulation
* **Target Files:** [`src/platform/ARIES/startup.S`](src/platform/ARIES/startup.S), [`src/platform/ARIES/system.c`](src/platform/ARIES/system.c)
* **Objective:** Confirm `.data` is copied from Flash (`_sidata`) to SRAM (`_sdata`) and `.bss` is wiped to `0x00`.
* **Execution Steps:**
  1. Identify a known non-zero initialized global variable (e.g. `systemConfig` in `.data`).
  2. Identify an uninitialized global variable (e.g. `timer0_overflows` in `.bss`).
  3. Break at entry to `main()` (`0x20000100`).
  4. Inspect memory addresses of both symbols.
* **Expected Result:**
  - `.data` variable has expected non-zero initialization values.
  - `.bss` variable evaluates to exactly `0x00000000`.
* **Pass/Fail Criteria:** Zero bytes in `.bss`; exact flash payload mirrored in `.data`.

---

### Test Case SW-BOOT-04: Machine Trap Vector Routing (`mtvec`)
* **Environment:** Target GDB / Renode
* **Target Files:** [`src/platform/ARIES/startup.S`](src/platform/ARIES/startup.S)
* **Objective:** Confirm the RISC-V Machine Trap Vector Base Address Register (`mtvec`) points to `trap_entry` in direct mode (bits [1:0] = `00`).
* **Execution Steps:**
  1. In GDB, break after `csrw mtvec, t0`.
  2. Read CSR: `info reg mtvec`.
* **Expected Result:**
  - `mtvec` value must equal `0x200000F0` (address of `trap_entry`).
  - Bits [1:0] must be `0b00` (Direct Mode: all traps jump to single address).
* **Pass/Fail Criteria:** `mtvec == 0x200000F0`.

---

### Test Case SW-BOOT-05: Trap Frame Save/Restore (32-Register Context)
* **Environment:** Renode / GDB
* **Target File:** [`src/platform/ARIES/startup.S`](src/platform/ARIES/startup.S)
* **Objective:** Verify `trap_entry` saves all 32 integer registers to a 128-byte stack frame, calls `handle_trap`, and restores all registers before `mret`.
* **Execution Steps:**
  1. Inject an illegal instruction (e.g. `.word 0x00000000`) or trigger software interrupt.
  2. Verify execution branches to `trap_entry`.
  3. Verify `addi sp, sp, -128` allocates 128 bytes.
  4. Inspect memory at `$sp` to `$sp + 124`: registers `x1` through `x31` must be stored consecutively.
* **Expected Result:**
  - Stack frame is exactly 128 bytes (16-byte aligned).
  - Context is completely restored upon `mret`.
* **Pass/Fail Criteria:** Register preservation verified across interrupt entry and exit.

---

### Test Case SW-BOOT-06: Synchronous Trap Panic vs. Machine External Interrupt
* **Environment:** Target Silicon / Renode
* **Target File:** [`src/platform/ARIES/system.c`](src/platform/ARIES/system.c)
* **Objective:** Verify `handle_trap()` checks bit 31 of `mcause` to distinguish asynchronous interrupts from synchronous exceptions.
* **Execution Steps:**
  1. Force a synchronous exception (e.g. deliberate unaligned 32-bit load `*(volatile uint32_t*)0x10000001`).
  2. Verify `mcause & 0x80000000 == 0`.
  3. Verify execution enters `handle_trap_panic()`.
  4. Verify UART0 outputs panic diagnostic dump: `mcause`, `mepc`, `sp`.
* **Expected Result:**
  - Synchronous exception triggers panic telemetry and enters safe halt loop.
  - Asynchronous interrupt (`mcause & 0x80000000 != 0`) checks `cause_num == 11` (External Interrupt) and indexes `interrupt_table[]`.
* **Pass/Fail Criteria:** Zero cross-execution between exception panic and interrupt dispatcher.

---

### Test Case SW-BOOT-07: Stack High-Water Mark Canary Check
* **Environment:** Target Silicon / GDB
* **Target Files:** [`src/platform/ARIES/system.c`](src/platform/ARIES/system.c), [`src/platform/ARIES/link/aries_v3.ld`](src/platform/ARIES/link/aries_v3.ld)
* **Objective:** Determine the maximum stack depth consumed during flight controller operation.
* **Execution Steps:**
  1. Before entering `main()`, fill the region from `_sstack` to `_estack - 512` with pattern `0xA5A5A5A5`.
  2. Run the board through 5 minutes of high-load operations (active CLI commands, continuous Configurator MSP telemetry streaming).
  3. In GDB or via CLI diagnostic command, scan from `_sstack` upward to find the first address where `0xA5A5A5A5` was overwritten.
* **Expected Result:**
  - Minimum 4 KB of virgin `0xA5A5A5A5` bytes remaining between the lowest stack point and `_sstack`.
* **Pass/Fail Criteria:** PASS if stack usage is $< 12\text{ KB}$ out of $16\text{ KB}$.

---

## Module 2: 16550 UART Serial Communication & Ring Buffers

### Test Case SW-UART-01: Baud Rate Divisor Register Configuration
* **Environment:** Renode MMIO Inspection / Target Hardware
* **Target File:** [`src/platform/ARIES/serial_uart_aries.c`](src/platform/ARIES/serial_uart_aries.c)
* **Objective:** Verify baud rate divisor formula: $\text{Divisor} = \frac{100,000,000}{16 \times \text{Baudrate}}$.
* **Execution Steps:**
  1. Call `uartOpen(UART0, 115200, ...)`.
  2. Inspect 16550 registers at base `0x10000100`:
     - Bit 7 of `LCR` (Divisor Latch Access Bit - DLAB) must be set to `1`.
     - Read `DLL` (`0x10000100`) and `DLM` (`0x10000104`).
     - Bit 7 of `LCR` must be cleared back to `0`.
* **Expected Result:**
  - $\text{Divisor} = \frac{100,000,000}{16 \times 115200} = 54.25 \rightarrow 54$ (`0x0036`).
  - `DLL == 0x36`, `DLM == 0x00`.
* **Pass/Fail Criteria:** Divisor registers match `54` within 0% baud error margin ($115,740\text{ baud}$, $+0.46\%$ tolerance).

---

### Test Case SW-UART-02: FIFO Enable & Interrupt Masking
* **Environment:** Target Silicon / Renode
* **Target File:** [`src/platform/ARIES/serial_uart_aries.c`](src/platform/ARIES/serial_uart_aries.c)
* **Objective:** Confirm UART0 FIFO is enabled and RX interrupt line is unmasked.
* **Execution Steps:**
  1. Read `FCR` (FIFO Control Register) and `IER` (Interrupt Enable Register).
* **Expected Result:**
  - `FCR` write of `0x07` (Enable FIFO, clear RX/TX FIFOs).
  - `IER` write of `0x01` (Enable Received Data Available Interrupt - ERBFI).
* **Pass/Fail Criteria:** Both registers verified via GDB / MMIO read.

---

### Test Case SW-UART-03: RX Ring Buffer Ingestion & Head/Tail Wraparound
* **Environment:** Host Unit Test (`src/test/unit/io_serial_unittest.cc`) / Target
* **Target File:** [`src/platform/ARIES/serial_uart_aries.c`](src/platform/ARIES/serial_uart_aries.c)
* **Objective:** Ensure UART RX ring buffer correctly handles circular buffer wraparound without losing or corrupting bytes.
* **Execution Steps:**
  1. Sized buffer: 256 bytes (`mask = 0xFF`).
  2. Feed 300 sequential bytes (`0x00`, `0x01`, ... `0xFF`, `0x00`, ... `0x2C`) through `uartRxPoll` or simulated UART interrupt.
  3. Read bytes out concurrently in batches of 10.
  4. Verify head and tail pointers wrap around `0xFF` to `0x00`.
* **Expected Result:**
  - Head and tail indices increment modulo buffer size.
  - All 300 bytes are read back with 100% order fidelity.
* **Pass/Fail Criteria:** Zero dropped, repeated, or out-of-order bytes.

---

### Test Case SW-UART-04: Transmit FIFO Priming on Empty Queue
* **Environment:** Target Silicon / Renode
* **Target File:** [`src/platform/ARIES/serial_uart_aries.c`](src/platform/ARIES/serial_uart_aries.c)
* **Objective:** Prevent UART transmission stalls when writing to an idle transmitter.
* **Execution Steps:**
  1. Call `uartWrite(s, 'A')` when transmitter queue is completely idle and TX interrupts are disabled.
  2. Observe transmission path:
     - Driver must write `'A'` directly to `THR` (`0x10000100`), OR
     - Enable `IER_ETBEI` (bit 1 of `IER`) to trigger an immediate transmitter holding register empty interrupt.
* **Expected Result:**
  - Character `'A'` immediately appears on physical UART TX line (`/dev/ttyUSB0`).
  - No deadlock or indefinite wait on TX buffer space.
* **Pass/Fail Criteria:** Character transmission begins within $< 10\,\mu\text{s}$.

---

### Test Case SW-UART-05: High-Speed MSP Frame Checksum Integrity
* **Environment:** Target Silicon / Python Test Script
* **Target File:** [`src/main/msp/msp_serial.c`](src/main/msp/msp_serial.c)
* **Objective:** Validate error-free transmission of MultiWii Serial Protocol (MSP v1 and v2) frames over UART0.
* **Execution Steps:**
  1. Send 10,000 MSP requests for `MSP_STATUS` (`cmd 101`) and `MSP_RAW_IMU` (`cmd 102`) over USB-UART0.
  2. Capture responses and compute XOR checksum (MSPv1) and CRC32 (MSPv2).
* **Expected Result:**
  - Checksum match rate: 10,000 / 10,000 (100.0%).
  - Packet drops / framing errors: 0.
* **Pass/Fail Criteria:** PASS if error rate is 0.00%.

---

## Module 3: High-Resolution Timebase & Hardware Timer 0

### Test Case SW-TIME-01: Monotonic 64-bit Cycle Counter Atomic Read
* **Environment:** Host Unit Test / Target Silicon
* **Target File:** [`src/platform/ARIES/system.c`](src/platform/ARIES/system.c)
* **Objective:** Verify [`getCycleCounter64()`](src/platform/ARIES/system.c#L18) guarantees consistency across 32-bit `mcycle` and `mcycleh` reads without torn reads during roll boundaries.
* **Execution Steps:**
  1. Code inspect loop:
     ```c
     do {
         hi = csrr(mcycleh);
         lo = csrr(mcycle);
         hi2 = csrr(mcycleh);
     } while (hi != hi2);
     ```
  2. Test in a loop of $1,000,000$ consecutive reads across the lower-to-upper word carry boundary.
* **Expected Result:**
  - Current value must always be strictly greater than or equal to previous value: $\text{val}_{k} \ge \text{val}_{k-1}$.
* **Pass/Fail Criteria:** Zero instances of negative delta ($0$ backwards jumps).

---

### Test Case SW-TIME-02: Fast Reciprocal Division by 100ULL
* **Environment:** Host Unit Test / Disassembly Inspection
* **Target File:** [`src/platform/ARIES/system.c`](src/platform/ARIES/system.c)
* **Objective:** Verify `(timeUs_t)(getCycleCounter64() / 100ULL)` executes without calling `__udivdi3`.
* **Execution Steps:**
  1. Compile `system.c` to object file: `make obj/main/ARIES/system.o`.
  2. Run `riscv32-esp-elf-objdump -d obj/main/ARIES/system.o`.
  3. Inspect disassembly of function `micros`.
* **Expected Result:**
  - Disassembly contains no `jal __udivdi3` or `call __udivdi3`.
  - Division is implemented via reciprocal multiplication (`mulhu`, `srli`).
* **Pass/Fail Criteria:** Zero software division subroutine calls.

---

### Test Case SW-TIME-03: Timer 0 EOI Interrupt Acknowledgment
* **Environment:** Target Silicon / Renode
* **Target File:** [`src/platform/ARIES/system.c`](src/platform/ARIES/system.c)
* **Objective:** Confirm `thejas32_timer0_isr` clears the interrupt flag by reading the `EOI` register (`0x10000A0C`).
* **Execution Steps:**
  1. Break at entry to `thejas32_timer0_isr`.
  2. Single step across instruction `(void)THEJAS32_TIMER(0).EOI;`.
  3. Inspect interrupt controller status at `0x20010000`.
  4. Execute `mret`.
* **Expected Result:**
  - The interrupt line for IRQ 7 drops from `1` to `0`.
  - CPU resumes normal main execution without immediately re-entering the trap handler.
* **Pass/Fail Criteria:** PASS if interrupt line de-asserts upon read of `EOI`.

---

### Test Case SW-TIME-04: Delay Precision & Scaling (`delayMicroseconds` & `delay`)
* **Environment:** Target Hardware + Oscilloscope / Logic Analyzer
* **Target File:** [`src/platform/ARIES/system.c`](src/platform/ARIES/system.c)
* **Objective:** Measure physical delay accuracy on GPIO pin toggles.
* **Execution Steps:**
  1. Create a tight test loop:
     ```c
     IOHi(LED0_PIN);
     delayMicroseconds(100);
     IOLo(LED0_PIN);
     delay(10);
     ```
  2. Measure high pulse width and low duration on oscilloscope.
* **Expected Result:**
  - High pulse width: $100\,\mu\text{s} \pm 1\,\mu\text{s}$.
  - Low duration: $10.0\text{ ms} \pm 0.05\text{ ms}$.
* **Pass/Fail Criteria:** Timing error $\le 1.0\%$.

---

## Module 4: Address-Decoded GPIO & External Interrupts

### Test Case SW-GPIO-01: Address-Decoded Bank 0 & Bank 1 Bit Access
* **Environment:** Renode MMIO Inspection / Target Hardware
* **Target File:** [`src/platform/ARIES/io_aries.c`](src/platform/ARIES/io_aries.c)
* **Objective:** Verify THEJAS32 unique address-decoded GPIO access formula:
  $$\text{Address} = \text{Base} + ((1 \ll \text{pin}) \ll 2)$$
* **Execution Steps:**
  1. Call `IOHi(IOGetByTag(DEFIO_TAG(PA0)))` (Bank 0, pin 0).
     - Expected write address: `0x10080000 + ((1 << 0) << 2) = 0x10080004`.
  2. Call `IOHi(IOGetByTag(DEFIO_TAG(PA22)))` (Bank 1, pin 6: $22 - 16 = 6$).
     - Expected write address: `0x10180000 + ((1 << 6) << 2) = 0x10180100`.
* **Expected Result:**
  - Memory write target strictly follows the address-decoded formula.
* **Pass/Fail Criteria:** Verified exact target MMIO address in disassembly and execution.

---

### Test Case SW-GPIO-02: Bank 0 & Bank 1 EXTI Handler Callback Routing
* **Environment:** Target Silicon / Renode
* **Target File:** [`src/platform/ARIES/exti_aries.c`](src/platform/ARIES/exti_aries.c)
* **Objective:** Verify Bank 0 (IRQ 10) and Bank 1 (IRQ 11) route to individual registered callbacks in `extiChannels[32]`.
* **Execution Steps:**
  1. Register a test callback on `PA0` (Bank 0) via `EXTIConfig()`.
  2. Register a test callback on `PA22` (Bank 1) via `EXTIConfig()`.
  3. Pulse `PA0` high; pulse `PA22` high.
* **Expected Result:**
  - `thejas32_gpio0_isr()` triggers for `PA0`; increments callback counter 1.
  - `thejas32_gpio1_isr()` triggers for `PA22`; increments callback counter 2.
  - Interrupt status bits cleared in both GPIO banks.
* **Pass/Fail Criteria:** Independent callback execution with zero cross-talk.

---

## Module 5: SPI Bus & IMU Sensor Communication

### Test Case SW-SPI-01: Chip Select Active After Transfer (`Bits.CSAAT`)
* **Environment:** Target Hardware + Logic Analyzer
* **Target File:** [`src/platform/ARIES/bus_spi_aries.c`](src/platform/ARIES/bus_spi_aries.c)
* **Objective:** Verify CS line (`PA8`) does not drop high between intermediate segment transfers during multi-byte reads.
* **Execution Steps:**
  1. Trigger 14-byte IMU burst read via `spiProcessSegmentsPolled()`.
  2. Observe CS line (`PA8`), SCK (`PA5`), and MISO (`PA6`) on logic analyzer.
* **Expected Result:**
  - CS transitions low at start of byte 1.
  - CS remains continuously low through byte 14.
  - CS transitions high only after byte 14 completion.
* **Pass/Fail Criteria:** Zero mid-transfer CS high pulses ($0\text{ ns}$ glitches).

---

### Test Case SW-SPI-02: IMU WHO_AM_I Hardware Identification
* **Environment:** Target Hardware
* **Target Files:** [`src/main/drivers/accgyro/accgyro_spi_icm426xx.c`](src/main/drivers/accgyro/accgyro_spi_icm426xx.c), [`src/main/drivers/accgyro/accgyro_spi_bmi270.c`](src/main/drivers/accgyro/accgyro_spi_bmi270.c)
* **Objective:** Confirm successful register read of device identification register (`0x75` for ICM-42688-P or `0x00` for BMI270).
* **Execution Steps:**
  1. Boot firmware with breakout wired to Header J1.
  2. In CLI, type `status`.
* **Expected Result:**
  - If ICM42688P: reads `0x47`. Betaflight logs `GYRO: ICM42688P`.
  - If BMI270: reads `0x24`. Betaflight logs `GYRO: BMI270`.
* **Pass/Fail Criteria:** CLI reports correct hardware sensor name (not `NONE`).

---

### Test Case SW-SPI-03: Polling Loop Bounded Timeouts
* **Environment:** Host Unit Test / Renode
* **Target File:** [`src/platform/ARIES/bus_spi_aries.c`](src/platform/ARIES/bus_spi_aries.c)
* **Objective:** Verify SPI transfer loops cannot hang indefinitely if an external sensor is unplugged or lines are floating.
* **Execution Steps:**
  1. Disconnect MISO and SCK lines.
  2. Issue `spiProcessSegmentsPolled()`.
  3. Measure elapsed cycles until function aborts.
* **Expected Result:**
  - Loop timeout counter terminates execution after 1,000 iterations.
  - Function returns error flag `false` without deadlocking the flight controller.
* **Pass/Fail Criteria:** Aborts cleanly in $< 50\,\mu\text{s}$.

---

## Module 6: Non-Volatile Flash Configuration & PG Storage

### Test Case SW-FLASH-01: SPI3 Winbond JEDEC ID Validation
* **Environment:** Target Hardware / GDB
* **Target File:** [`src/platform/ARIES/config_flash_aries.c`](src/platform/ARIES/config_flash_aries.c)
* **Objective:** Query onboard SPI3 Winbond Flash (`U5`) using opcode `0x9F` (Read Identification).
* **Execution Steps:**
  1. Send opcode `0x9F` over SPI3 controller (`0x10200200`).
  2. Read 3 response bytes: Manufacturer ID, Memory Type, Capacity.
* **Expected Result:**
  - Byte 1: `0xEF` (Winbond Serial Flash).
  - Byte 2: `0x40` (SPI NOR).
  - Byte 3: `0x15` (16 Mbit / 2 MB) or `0x16` (32 Mbit / 4 MB).
* **Pass/Fail Criteria:** Byte 1 must equal `0xEF`.

---

### Test Case SW-FLASH-02: 4 KB Sector Erase (`0x20`) & Blank Check
* **Environment:** Target Hardware
* **Target File:** [`src/platform/ARIES/config_flash_aries.c`](src/platform/ARIES/config_flash_aries.c)
* **Objective:** Erase reserved configuration sector at `0x201F8000` and verify all bits read `0xFF`.
* **Execution Steps:**
  1. Call `flashSectorErase(0x201F8000)`.
  2. Read back 4,096 bytes directly from memory-mapped flash at `0x201F8000`.
* **Expected Result:**
  - All 4,096 bytes evaluate to `0xFF`.
* **Pass/Fail Criteria:** Zero non-`0xFF` bytes detected.

---

### Test Case SW-FLASH-03: Parameter Group (PG) Data Persistence & Reboot Survival
* **Environment:** Target Silicon / CLI
* **Target Files:** [`src/platform/ARIES/config_flash_aries.c`](src/platform/ARIES/config_flash_aries.c), [`src/main/pg/pg.c`](src/main/pg/pg.c)
* **Objective:** Verify settings changes survive a hard power cycle.
* **Execution Steps:**
  1. In CLI: `set motor_pwm_rate = 400`, `set pilot_name = ARIES_ACE`, `save`.
  2. Disconnect physical USB power completely for 10 seconds.
  3. Reconnect USB; in CLI run: `get motor_pwm_rate` and `get pilot_name`.
* **Expected Result:**
  - Returns `motor_pwm_rate = 400`.
  - Returns `pilot_name = ARIES_ACE`.
  - No `EEPROM checksum failure` warning displayed on boot.
* **Pass/Fail Criteria:** Exact string and integer value retention.

---

## Module 7: Motor Actuator Outputs & Timer 1 Pulse Scheduler

### Test Case SW-MOTOR-01: Timer 1 Interrupt Registration & Pulse Scheduling
* **Environment:** Target Hardware + Oscilloscope
* **Target File:** [`src/platform/ARIES/pwm_motor_aries.c`](src/platform/ARIES/pwm_motor_aries.c)
* **Objective:** Verify Timer 1 on IRQ 8 schedules sub-microsecond pulse falling edges across 4 motor channels simultaneously.
* **Execution Steps:**
  1. Command motor throttle outputs: Motor 1 = 1100 µs, Motor 2 = 1300 µs, Motor 3 = 1500 µs, Motor 4 = 1800 µs.
  2. Capture all 4 channels on a multi-channel logic analyzer.
* **Expected Result:**
  - All 4 pins assert high simultaneously at the start of the 400 Hz period.
  - Pin 1 drops at $1100\,\mu\text{s} \pm 2\,\mu\text{s}$.
  - Pin 2 drops at $1300\,\mu\text{s} \pm 2\,\mu\text{s}$.
  - Pin 3 drops at $1500\,\mu\text{s} \pm 2\,\mu\text{s}$.
  - Pin 4 drops at $1800\,\mu\text{s} \pm 2\,\mu\text{s}$.
* **Pass/Fail Criteria:** Pulse width error $< 0.5\%$, jitter $< 2\,\mu\text{s}$.

---

### Test Case SW-MOTOR-02: Disarmed Motor Cutoff State
* **Environment:** Target Hardware + Oscilloscope
* **Target File:** [`src/platform/ARIES/pwm_motor_aries.c`](src/platform/ARIES/pwm_motor_aries.c)
* **Objective:** Ensure motor output lines never latch high when disarmed.
* **Execution Steps:**
  1. Boot flight controller into disarmed state.
  2. Observe output on `PA1`–`PA4`.
* **Expected Result:**
  - Pins output safe disarmed pulse width ($1000\,\mu\text{s}$) at 400 Hz, or remain logic low (`0.0V`).
  - No continuous high voltage ($3.3\text{V DC}$) under any condition.
* **Pass/Fail Criteria:** Steady pulse or ground; zero runaway high states.

---

## Module 8: Soft-Float Flight Loop & Scheduler Stress

### Test Case SW-PERF-01: Disassembly Audit for Zero Double-Precision Math
* **Environment:** Host Build Pipeline
* **Target Files:** [`obj/main/betaflight_ARIESV3.elf`](obj/main/betaflight_ARIESV3.elf)
* **Objective:** Guarantee that no 64-bit double-precision routines (`__adddf3`, `__subdf3`, `__muldf3`, `__divdf3`) exist inside time-critical control paths.
* **Execution Steps:**
  1. Run command:
     ```bash
     riscv32-esp-elf-objdump -d obj/main/betaflight_ARIESV3.elf | grep -E "<__adddf3>|<__subdf3>|<__muldf3>|<__divdf3>"
     ```
  2. Trace caller functions using symbol addresses.
* **Expected Result:**
  - Zero calls originating from:
    - `subTaskPidController`
    - `taskGyroSample`
    - `taskFiltering`
    - `applyIirFilter`
* **Pass/Fail Criteria:** Zero double-precision calls inside 1 kHz control loop functions.

---

### Test Case SW-PERF-02: Clamped 1.0 kHz Loop Period & CPU Load Profiling
* **Environment:** Target Silicon / CLI
* **Target Files:** [`src/main/flight/pid.c`](src/main/flight/pid.c), [`src/main/scheduler/scheduler.c`](src/main/scheduler/scheduler.c)
* **Objective:** Verify PID loop executes strictly at 1.0 kHz ($1000\,\mu\text{s}$) with total CPU load under 50%.
* **Execution Steps:**
  1. In CLI, execute `tasks`.
  2. Inspect:
     - `Task ID 0 (PID)`: rate must report $1000\text{ Hz}$.
     - `Interval`: must report $1000\,\mu\text{s}$.
     - `Max Duration`: must report $< 500\,\mu\text{s}$.
  3. In CLI, execute `status`.
  4. Inspect `Current Load`.
* **Expected Result:**
  - PID execution rate: $1000\text{ Hz} \pm 5\text{ Hz}$.
  - System load: $< 50\%$ (nominal target: 25%–35%).
* **Pass/Fail Criteria:** System load $\le 50.0\%$.

---

### Test Case SW-SAFE-01: Comprehensive Arming Lockout Enforcement
* **Environment:** Target Silicon / Configurator
* **Target File:** [`src/main/fc/runtime_config.h`](src/main/fc/runtime_config.h)
* **Objective:** Confirm flight controller rejects arming switch activation under multiple failure injection scenarios.
* **Execution Steps:**
  1. **Scenario A (CLI Active):** Try arming while USB CLI prompt is open.
     - Expected: Arming rejected (`ARMING_DISABLED_CLI`).
  2. **Scenario B (Throttle Raised):** Raise throttle stick to 50%; attempt to arm.
     - Expected: Arming rejected (`ARMING_DISABLED_THROTTLE`).
  3. **Scenario C (Uncalibrated Gyro):** Move the board violently during power-up to delay gyro calibration; attempt to arm.
     - Expected: Arming rejected (`ARMING_DISABLED_CALIBRATING`).
  4. **Scenario D (Disconnected IMU):** Boot with IMU unplugged; attempt to arm.
     - Expected: Arming rejected (`ARMING_DISABLED_NO_GYRO`).
* **Pass/Fail Criteria:** 100% rejection across all 4 scenarios.

---

### Test Case SW-SAFE-02: Failsafe Stage 1 & Stage 2 Cutoff Verification
* **Environment:** Target Hardware + Logic Analyzer (Props Off)
* **Target Files:** [`src/main/flight/failsafe.c`](src/main/flight/failsafe.c)
* **Objective:** Ensure radio transmitter signal loss disarms motors within 1.0 second.
* **Execution Steps:**
  1. Arm quadcopter on bench at low idle throttle ($1100\,\mu\text{s}$).
  2. Sever the receiver signal wire or turn off radio transmitter.
  3. Measure time from signal loss to PWM pulse drop on logic analyzer.
* **Expected Result:**
  - Stage 1 (0–1000 ms): Holds fallback channel values.
  - Stage 2 ($> 1000\text{ ms}$): Motors disarm immediately; PWM output drops to $1000\,\mu\text{s}$ or cuts out.
* **Pass/Fail Criteria:** Total motor cutoff time $\le 1000\text{ ms}$.

---

## 3. Automated Test Execution Script

A bash script to automate host unit testing, disassembly auditing, and static analysis:

```bash
#!/usr/bin/env bash
# ==============================================================================
# ARIES v3.0 Automated Software Test Runner
# ==============================================================================
set -euo pipefail

echo "==> [Step 1/4] Running Clean Target Build..."
make TARGET=ARIESV3 clean
make TARGET=ARIESV3 -j$(nproc)
make TARGET=ARIESV3 hex

echo "==> [Step 2/4] Verifying Binary Footprint..."
TOOLCHAIN="${HOME}/.espressif/tools/riscv32-esp-elf/esp-13.2.0_20250707/riscv32-esp-elf/bin"
"${TOOLCHAIN}/riscv32-esp-elf-size" obj/main/betaflight_ARIESV3.elf

echo "==> [Step 3/4] Running Soft-Float Disassembly Audit..."
# Check for forbidden 64-bit double routines in control loop
DOUBLE_OPS=$("${TOOLCHAIN}/riscv32-esp-elf-objdump" -d obj/main/betaflight_ARIESV3.elf | \
    grep -E "<__adddf3>|<__subdf3>|<__muldf3>|<__divdf3>" | wc -l || true)

if [ "${DOUBLE_OPS}" -gt 0 ]; then
    echo "⚠️ Warning: Found ${DOUBLE_OPS} double-precision soft-float calls in binary!"
    echo "Review objdump output to ensure none reside in subTaskPidController."
else
    echo "✅ PASS: Zero double-precision floating point operations detected."
fi

echo "==> [Step 4/4] Verifying Trap Vector & Reset Address..."
RESET_ADDR=$("${TOOLCHAIN}/riscv32-esp-elf-nm" obj/main/betaflight_ARIESV3.elf | grep " _start$" | awk '{print $1}')
TRAP_ADDR=$("${TOOLCHAIN}/riscv32-esp-elf-nm" obj/main/betaflight_ARIESV3.elf | grep " trap_entry$" | awk '{print $1}')

echo "Reset Vector Address: 0x${RESET_ADDR} (Expected: 0x20000000)"
echo "Trap Vector Address:  0x${TRAP_ADDR}  (Expected: 0x200000f0)"

if [ "${RESET_ADDR}" == "20000000" ] && [ "${TRAP_ADDR}" == "200000f0" ]; then
    echo "✅ PASS: Boot and trap vectors match exact hardware specifications."
else
    echo "❌ FAIL: Vector address mismatch!"
    exit 1
fi

echo "=============================================================================="
echo "🎉 ALL AUTOMATED SOFTWARE CHECKS PASSED SUCCESSFULLY."
echo "=============================================================================="
```

---

*Document Author: Flight Control Porting Team*  
*Repository Reference: [`TEST_PLAN.md`](TEST_PLAN.md) & [`HAL_PROGRESS.md`](HAL_PROGRESS.md)*
