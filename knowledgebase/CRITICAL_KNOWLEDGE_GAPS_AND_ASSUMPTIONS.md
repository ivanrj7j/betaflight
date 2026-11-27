# Critical Knowledge Gaps, Hardware Boundaries & Assumption Guardrails

> **MANDATORY POLICY FOR ALL AGENTS AND DEVELOPERS:**  
> When operating on this repository, **NEVER ASSUME OR GUESS** hardware parameters, pinout behaviors, or protocol requirements.  
> If a task touches any subsystem documented below that is marked with **[PENDING TEAM CONFIRMATION]**, you **MUST NOT** hallucinate an implementation. You must present the verified boundaries, state the open questions, and request user/team confirmation.

---

## 1. Summary of Identified Knowledge Gaps

```mermaid
graph TD
    subgraph SENSORS ["1. IMU & Sensor Subsystem"]
        G1["IMU Breakout Model & Axis Alignment"]
        G1_DESC["Exact sensor (ICM42688P vs BMI270 vs MPU6000), 3.3V LDO, DRDY on PA0, Board Orientation"]
    end

    subgraph RC_RX ["2. Radio Control & Inversion"]
        G2["RC Protocol & Hardware Inversion Reality"]
        G2_DESC["16550 UART lacks hardware pin inverter; CRSF/ELRS (420k, Divisor 15) vs SBUS inverted logic"]
    end

    subgraph ACTUATORS ["3. Motor ESC Signaling"]
        G3["ESC Protocol & PWM Constraints"]
        G3_DESC["4-in-1 ESC type, OneShot125 vs DShot; Timer 1 IRQ 8 deadline scheduler limits"]
    end

    subgraph ADC_VBAT ["4. Analog & Battery Telemetry"]
        G4["I2C ADC (U9) vs Internal 0x10001000"]
        G4_DESC["External U9 chip on I2C2 (Header J10) vs VEGA SDK internal ADC register block 0x10001000"]
    end

    subgraph FLASH_PERSIST ["5. Non-Volatile Parameter Persistence"]
        G5["SPI3 Flash Persistence vs CONFIG_IN_RAM"]
        G5_DESC["No-XIP reality; 0x20000000 causes mcause=7; true persistent storage requires SPI3 (0x10200100)"]
    end

    subgraph BOOT_FLASH ["6. Board Flashing & Boot Jumpers"]
        G6["Flashing Runbook & Jumper Configuration"]
        G6_DESC["BOOT_SEL on J12, XMODEM transfer at 0x00200000, vega_flasher.py / OpenOCD binary offsets"]
    end
```

---

## 2. Granular Gap Breakdown & Strict Assumption Guardrails

### Gap 1: Physical IMU Model, Wiring & Axis Alignment
* **Current Knowledge Base Reality:**
  - Header J1 exposes `SPI0` pins: `PA5` (SCK), `PA6` (MISO), `PA7` (MOSI), `PA8` (CS), and `PA0` (`EXTI` DRDY).
  - Driver `bus_spi_aries.c` clamps SCK to **12.5 MHz** (Divisor 1) to respect the 24.0 MHz maximum specification of high-grade sensors like ICM-42688P.
* **The Gap [PENDING TEAM CONFIRMATION]:**
  - **Specific Sensor Model:** Which sensor breakout is physically mounted to Header J1 on the drone/bench setup? (e.g., ICM-42688P, BMI270, MPU6000, or an indigenous Indian MEMS IMU?)
  - **Voltage Supply:** Does the breakout accept 3.3V directly from Header J1, or does it require 5V from an external BEC?
  - **Sensor Orientation:** Betaflight calculates roll/pitch/yaw using `CW0`, `CW90`, `CW180`, `CW270`, or `CUSTOM` matrix alignment. An incorrect orientation causes instant flyaways upon takeoff!
* **Guardrail Rule for Agents:**
  > **DO NOT** hardcode sensor orientation or assume a specific IMU model without verifying `target.h` against the user's physical sensor board. If sensor data reads inverted or axes swap, immediately prompt the user for physical module orientation.

---

### Gap 2: RC Receiver Protocol & Serial Inversion Realities
* **Current Knowledge Base Reality:**
  - `target.h` configures `UART1` on pins `PA16` (TX) and `PA17` (RX) for `USE_SERIALRX_CRSF`.
  - For a 100 MHz APB clock, standard 420,000 baud generates:
    $$\text{Divisor} = \frac{100{,}000{,}000}{16 \times 420{,}000} \approx 14.88 \implies \text{Divisor } 15 \ (416{,}666.7\text{ baud}, -0.79\%\text{ error})$$
* **The Gap [CRITICAL SILICON LIMITATION]:**
  - **No Hardware Inverter on 16550 UART:** Unlike STM32F3/F7/H7, the THEJAS32 16550 UART has **no internal hardware signal inverters**.
  - **SBUS / SmartPort Limitation:** Standard SBUS uses inverted UART logic ($V_{\text{idle}} = \text{LOW}$, $3.3\text{V} = \text{SPACE}$). Connecting an SBUS receiver directly to `PA17` will yield 100% frame errors and corrupted bytes unless an external transistor inverter circuit is added.
  - **CRSF / ExpressLRS Compatibility:** CRSF/ELRS uses non-inverted UART logic ($V_{\text{idle}} = \text{HIGH}$), making it natively compatible with `PA17`.
* **Guardrail Rule for Agents:**
  > **DO NOT** configure SBUS or SmartPort telemetry without explicitly alerting the user that an external hardware inverter is electrically required. Recommend **ExpressLRS (CRSF)** at 420,000 baud as the native non-inverted protocol for ARIES v3.

---

### Gap 3: Physical ESC Hardware & Protocol Selection
* **Current Knowledge Base Reality:**
  - Motor outputs 1..4 (`PA1`–`PA4`) use Hardware Timer 1 (IRQ 8) ascending deadline event scheduling.
  - Generates 400 Hz Standard PWM ($1000\text{--}2000\,\mu\text{s}$) or OneShot125 ($125\text{--}250\,\mu\text{s}$) consuming $< 3\,\mu\text{s}$ CPU total.
* **The Gap [PENDING TEAM CONFIRMATION]:**
  - **ESC Protocol Support:** What 4-in-1 ESC is installed? Does it support OneShot125 or does it require DShot (digital pulse-width encoding)?
  - **DShot Feasibility:** THEJAS32 lacks STM32-style timer DMA burst engines. Bit-banged DShot300 requires tight timing loops that conflict with soft-float processing.
* **Guardrail Rule for Agents:**
  > **DO NOT** attempt to write a bit-banged DShot driver on THEJAS32 unless Timer 1 OneShot125 is proven insufficient. OneShot125 provides sub-millisecond response latency with zero CPU blocking overhead.

---

### Gap 4: Analog ADC Telemetry (`U9` I2C Chip vs. `0x10001000` Internal Registers)
* **Current Knowledge Base Reality:**
  - `oldfiles/logs/ARIESv3 0_Datasheet_14-10-24.pdf` confirms Header `J10` analog inputs (`ADC_CH0`–`ADC_CH3`) are wired to **`U9`**, an external 4-channel I2C ADC chip connected to `I2C2` (`PA32` SCL, `PA31` SDA).
  - In `lib/vega-sdk/vega-sdk/bsp/include/adc.h`, C-DAC provides an internal register map:
    ```c
    #define ADC_BASE_ADDRESS 0x10001000
    typedef struct {
        UI address_reg; // 0x14 (A0), 0x15 (A1), 0x16 (A2), 0x17 (A3)
        UI status_reg;  // Poll bit 0 for done
        UI data_reg;    // 32-bit conversion
    } ADCReg_type;
    ```
* **The Gap [PENDING TEST CONFIRMATION]:**
  - Is `0x10001000` a functional on-chip peripheral on physical THEJAS32 silicon, or does the board strictly depend on `U9`?
  - Currently, `target.h` defines `#undef USE_ADC` to avoid `stub_drivers.c` returning `0V`, which permanently locks arming under `ARMING_DISABLED_BATTERY`.
* **Guardrail Rule for Agents:**
  > **DO NOT** enable `#define USE_ADC` until either:
  > 1. Register reads on `0x10001000` are validated on physical hardware, or
  > 2. An asynchronous driver for `U9` over `I2C2` is written and tested.

---

### Gap 5: Non-Volatile Flash Persistence across Power Cycles
* **Current Knowledge Base Reality:**
  - The external SPI flash is **NOT memory-mapped** for CPU read/write caching.
  - Attempting to dereference `0x20000000` triggers a fatal Store Access Fault (`mcause = 7`).
  - Active configuration uses `#define CONFIG_IN_RAM` (`EEPROM_SIZE 8192`). Because `PERSISTENT` is currently undefined in `src/platform/ARIES/include/platform/platform.h`, [`config_streamer.c`](../src/main/config/config_streamer.c#L32-L38) allocates `eepromData[8192]` in `.bss`.
  - **Current Reality (Audit 07 Verified):** [`startup.S`](../src/platform/ARIES/startup.S#L96-L104) zeroes `.bss` on every reboot (`systemReset()`), so saved configuration is **erased on warm soft-reboots as well as cold power-off** (`CONFIG: UNCONFIGURED` in [`tempfiles/phase19.5.txt`](../tempfiles/phase19.5.txt#L16)) until `PERSISTENT` (`.noinit`) and `MDP_SPI_3` storage are wired.
* **The Proven Solution from C-DAC SDK:**
  - C-DAC's reference example [`lib/vega-sdk/vega-sdk/examples/spi/spi_write_read_pgm/main.c`](../lib/vega-sdk/vega-sdk/examples/spi/spi_write_read_pgm/main.c) proves how to access persistent storage safely:
    - Target controller: `MDP_SPI_3` (`0x10200100`).
    - Use `SPI_set_CSAAT_pin(MDP_SPI_3, 1)` to assert Chip Select.
    - Read via command `0x03` (`RD_DATA_BYTES_SPI_CMD`) and write via `0x02` (`BYTE_PAGE_PGM_SPI_CMD`) with `0x20` (Sector Erase).
* **Guardrail Rule for Agents:**
  > **NEVER** write pointer dereferences (`*(uint32_t *)0x20000000 = val`) for flash persistence. If cold-reboot persistence is requested, implement the `MDP_SPI_3` command-based driver following the C-DAC example.

---

### Gap 6: Board Flashing Runbook & Boot Jumpers
* **Current Knowledge Base Reality:**
  - Firmware executes from SRAM starting at `0x00202000` with a strict $248\text{ KB}$ ceiling.
  - Intel HEX files (`.hex`) must be converted to raw binary (`.bin`) before uploading to internal SRAM.
* **The Gap [PENDING LAB RUNBOOK]:**
  - Exact boot jumper state: Header `J12` (`BOOT_SEL`) must be documented for SPI boot vs UART XMODEM mode.
  - Flashing commands:
    ```bash
    # OpenOCD SRAM Upload
    openocd -f aries_openocd.cfg -c "init; halt; load_image betaflight_ARIESV3.bin 0x00202000; resume 0x00202000; exit"

    # Serial Bootloader Upload
    python3 vega_flasher.py -p /dev/ttyUSB0 -b 115200 -a 0x00202000 -w betaflight_ARIESV3.bin
    ```
* **Guardrail Rule for Agents:**
  > **DO NOT** instruct users to flash `.hex` files directly to `0x20000000` without specifying the target memory medium (SRAM vs SPI flash) and conversion steps.

---

## 3. Verified Silicon Errata & Architecture Guardrails (Code Audit Node 02)

The following architectural guardrails and silicon quirks were confirmed through physical hardware testing, technical debt analysis ([`code_audit/02_WORKING_TREE_REVIEW_AND_TECHNICAL_DEBT.md`](code_audit/02_WORKING_TREE_REVIEW_AND_TECHNICAL_DEBT.md)), and consultation of official C-DAC VEGA SDK/BSP sources:

### Erratum 1: Synopsys DW_apb_uart 16550 Hardware FIFO Errata (`FCR = 0x00` Mandatory)
* **The Hardware Reality**: The DW_apb_uart 16550 IP on THEJAS32 (VEGA ET1031) silicon has hardware FIFO pointer and clock-crossing errata. Writing `UART_IIR_FCR = 0x07` or `0x01` causes incoming keystrokes to arrive scrambled/out-of-order, duplicates characters, and eventually freezes the hardware FIFO pointer.
* **The Official Precedent**: Every official C-DAC driver ([`lib/vega-sdk/vega-sdk/bsp/drivers/uart/uart.c:104`](../lib/vega-sdk/vega-sdk/bsp/drivers/uart/uart.c), [`lib/vega-bsp/cores/arduino/HardwareSerial.cpp:114`](../lib/vega-bsp/cores/arduino/HardwareSerial.cpp), [`lib/vega-bsp/cores/arduino/UARTClass.cpp:101`](../lib/vega-bsp/cores/arduino/UARTClass.cpp)) strictly disables hardware FIFOs by writing `UART_IIR_FCR = 0x00`.
* **Register Readback Quirk (Verified on Silicon, Phase 19)**: Offset `0x08` (`UART_IIR_FCR`) is write-only `FCR` and read-only `IIR`. On idle UART1 silicon ([`tempfiles/phase19.1.txt`](../tempfiles/phase19.1.txt)), reading `UART_IIR_FCR` returns `0x00000001` (`Bit 0 = 1` = no IRQ pending; `Bits [7:6] = 00` = FIFOs disabled). Diagnostics must verify `(iir & 0xC0U) == 0U`, not `Bit 0 == 0`.
* **Guardrail Rule**: Always operate in 1-byte register mode (`UART_IIR_FCR = 0x00`). Never enable hardware FIFOs in runtime code.

### Erratum 2: 921,600 Baud Timing Error Exceeds Asynchronous Tolerance (-3.12%)
* **The Math**: On a 100 MHz APB clock, 921,600 baud yields integer divisor 7:
  $$\text{Actual Baud} = \frac{100{,}000{,}000}{16 \times 7} \approx 892{,}857.14\text{ bps} \implies \text{Error} = -3.12\%$$
* **The Consequence**: Exceeds the standard $\pm 2.5\%$ UART asynchronous margin, causing framing errors (`FE`) on high-speed blackbox or logging ports.
* **Guardrail Rule**: Clamp or reject 921,600 baud in `thejas32_uart_set_baudrate_ex()`. Safe high-speed baud rates are 230,400 (Divisor 27, $+0.47\%$) and 420,000 (Divisor 15, $-0.79\%$).

### Erratum 3: Concurrency Hazard in Generic Polled Ingestion Hooks
* **The Mechanism**: In `src/main/drivers/serial_uart.c`, `#ifdef THEJAS32` calls `uartIrqHandler((uartPort_t *)uartPort)` inside `uartTotalRxBytesWaiting()` and `uartRead()`.
* **The Concurrency Risk**: In thread mode, these calls execute without atomic masking. If the hardware external interrupt (`thejas32_uart0_isr`) fires concurrently, both thread and ISR mutate `rxBufferHead` and read `UART_DR` simultaneously, corrupting ring buffers.
* **Guardrail Rule**: Any thread-mode invocation of `uartIrqHandler` must be enclosed in an `ATOMIC_BLOCK(NVIC_PRIO_SERIALUART)`.

### Erratum 4: `.noinit` Section (`PERSISTENT` & `USE_PERSISTENT_OBJECTS` — Resolved in Pre-Phase 20)
* **Resolved Behavior**: [`platform.h`](../src/platform/ARIES/include/platform/platform.h) now defines `#define PERSISTENT __attribute__((section(".noinit")))` and `#define USE_PERSISTENT_OBJECTS`, and [`stub_drivers.c`](../src/platform/ARIES/stub_drivers.c) annotates `persistentObjects[16]` with `PERSISTENT`. [`betaflight_ARIESV3.map`](../obj/main/betaflight_ARIESV3.map#L1149-L1155) confirms `.noinit = 0x2000` (`8,192 B` `eepromData`), isolated from `startup.S` `.bss` zeroing; warm-reset Configurator retention remains to be exercised on physical silicon.

### Erratum 5: Missing Memory Barrier Fences Across Timer 1 MMIO Writes (RVWMO)
* **Guardrail Rule**: Always insert `__asm__ volatile ("fence" : : : "memory");` after writing `LoadCount` before writing `Control = 0x03` on Synopsys DW APB timers so APB write buffering cannot reorder timer enable ahead of reload.

### Erratum 6: Bank-Wide EXTI Interrupt Fan-Out
* **Guardrail Rule**: IRQ 10 (`THEJAS32_IRQ_GPIO0`) and IRQ 11 (`THEJAS32_IRQ_GPIO1`) have no per-pin HW pending status register; strictly enforce at most 1 active interrupt pin on Bank 0 (`PA0` ICM-42688P Gyro DRDY).

### Erratum 7–9 Summary (Pre-Phase 20 Software Fixes Applied; Hardware Pending)
* **UART0 TX & Quiet Boot (`Erratum 7`)**: Fixed via `UART_TX_BUFFER_SIZE = 512`, atomic `uartEnableTxInterrupt()`, `ARIES_DEV_LOGGING ?= 0`, and disabling `USE_IMMEDIATE_CLI` (restoring the 100 ms CLI guard).
* **Motor Safety & PWM Gating (`Erratum 8`)**: Fixed via `ariesPwmEmergencyStop()` in trap/reset/failure/shutdown paths plus active-pulse and `motorPwmRate` gating in `ariesPwmMotorUpdateComplete()`.
* **242 KB Linker Ceiling, SPI0 Mode 3 & Alignment (`Erratum 9`)**: Clamped `aries_v3.ld` & `build_verify.sh` to `242K` (`0x0023E7FF`) with `_stack_size >= 0x2000` assertion, initialized SPI0 to Mode 3 (`0x30`), added `-mstrict-align` + `DMA_DATA`, implemented `unusedPinsInit()`, and set `BOOTLOADER_REQUEST_ROM` to safe halt pending verified ROM vector (`0x00010000`).

### Erratum 10: CRSF `23.8 us` UART1 Deadline, Untested `PA0` IRQ 10 & Phase 20 SPI3 Erase Stall (Audit 08 — Physical Verification Pending)
* **The Mechanisms**:
  1. Because `Erratum 1` keeps `UART1` in 1-byte mode (`FCR = 0x00`), `420,000` baud CRSF allows at most **`23.8 us`** per byte before `ORE` drops RC frames—any `thejas32_atomic_enter()` lock or ISR longer than `~20 us` threatens RC link integrity.
  2. `mpuIntExtiInit()` expects floating rising-edge `PA0`, while THEJAS32 lacks SW pull/trigger regs and `thejas32_gpio0_isr()` clears no edge latch—real silicon `IRQ 10` edge vs. level sensitivity and live `1 kHz` soft-float `pidController`/`imuMahonyAHRSupdate` WCET (`GYRO: None` so far) remain unverified until bench testing with live ICM-42688P + CRSF hardware.
  3. VEGA SDK's reference SPI write/erase (`spi_write_read_pgm/main.c:119-121`) spins on `while (bStatus & 0x01)` during multi-ms `4 KB` sector erases (`0x20`)—deferred to Phase 20 (`MDP_SPI_3`).

### Erratum 11: Active-LOW `LD1` RGB LED (`PA22`–`PA24`), `unusedPinsInit()` Purple Trap & Silent Boot Masking (`2026-11-27`)
* **Hardware Reality ([`ARIESv3 0_Datasheet_14-10-24.pdf`](../oldfiles/logs/ARIESv3%200_Datasheet_14-10-24.pdf) pp. 5, 15 & [`LED_Series.ino`](../lib/vega-bsp/libraries/GPIO/examples/LED_Series/LED_Series.ino#L11-L58)):**
  * Onboard RGB LED `LD1` is **active-LOW** (`0` = ON, `1` = OFF): `GPIO22` (`LD1_4` Green), `GPIO23` (`LD1_3` Blue), `GPIO24` (`LD1_2` Red). Dedicated `LD4` (`LD4_1`) is wired to SoC pin `PROC_HB` (Pin 16), not `GPIO22`.
  * Bootloaders (`flasher_min.bin`, `flasher_arduino.bin`) contain zero writes to `0x10180000` (Bank 1 GPIO).
* **Silicon Regression Mechanism ([`phase_19b_post_audit_remediation.md`](../logs/dev/phase_19b_post_audit_remediation.md), [`code_audit/09_PHASE_19B_SILICON_PURPLE_LED_REGRESSION.md`](code_audit/09_PHASE_19B_SILICON_PURPLE_LED_REGRESSION.md)):**
  1. Implementing [`unusedPinsInit()`](../src/platform/ARIES/stub_drivers.c#L164-L173) as `IOLo(io); IOConfigGPIO(io, IOCFG_OUT_PP);` drives all unowned Bank 1 pins `LOW`, turning `PA23` (Blue) + `PA24` (Red) **ON** while [`LED0_OFF`](../src/main/fc/init.c#L792) / [`light_led.c`](../src/main/drivers/light_led.c#L88) holds `PA22` (Green) `HIGH` (`OFF`) $\rightarrow$ **Purple `LD1`** upon reaching `initPhase3()` line `1080`.
  2. [`serial_uart_aries.c`](../src/platform/ARIES/serial_uart_aries.c) omits `IOInit()` on `PA16`/`PA17`/`PA30`/`PA31`, leaving them `OWNER_FREE` so `unusedPinsInit()` drives header pins `GPIO16..17` (`J10`) and `GPIO30..31` (`J9`) LOW.
  3. Switching [`ARIES_DEV_LOGGING ?= 0`](../src/platform/ARIES/mk/ARIES.mk#L59) + removing `USE_IMMEDIATE_CLI` suppresses all boot UART output; solid purple without `PA22` 500 ms [`warningLedFlash()`](../src/main/fc/core.c#L564-L570) toggles also requires re-checking post-`initPhase3()` scheduler/TX IRQ flow (including `UART_IE |= 0x02U` added at [`serial_uart_aries.c:246`](../src/platform/ARIES/serial_uart_aries.c#L246) vs `H22`).
* **Guardrail Rule:**
  > **NEVER** force `OWNER_FREE` GPIO lines push-pull `LOW` in `unusedPinsInit()` on ARIES v3.0 (use `IOCFG_IN_FLOATING` like high-Z reset state, or claim/park `PA23`/`PA24` `HIGH` / `OFF`). Never test `UART_IE |= 0x02` (`ETBEI`) or silent `ARIES_DEV_LOGGING=0` simultaneously with unverified GPIO shutdowns without checking serial `#`/MSP response.

---

## 4. Mandatory Agent Operational Rules Summary

1. **Before writing code touching these areas:** Check this document first.
2. **If an item is marked [PENDING TEAM CONFIRMATION]:** Ask the user for clarification rather than assuming default STM32/ESP32 behavior.
3. **If a team member provides physical hardware details or audits:** Immediately invoke `knowledge-update` to document findings and track technical debt!
