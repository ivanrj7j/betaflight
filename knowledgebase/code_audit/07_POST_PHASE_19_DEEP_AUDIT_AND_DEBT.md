# Code Audit Node 07: Post-Phase 19 Deep Audit & Debt Review (`rebuild`)

## 1. Executive Summary & File Traceability
- **Audit Target**: Whole-repository senior review on branch `rebuild` (`3f93a8fee Phase 19`), focused on **Betaflight Configurator <-> Firmware MSP Handshake**, **Booting**, **Memory Management**, **Motor Safety**, **SPI/IMU**, **Timebase**, and **Host Test Realism**.
- **Primary Audited Source Files**:
  - Serial / MSP / CLI: [`serial_uart_aries.c`](../../src/platform/ARIES/serial_uart_aries.c), [`serial_uart.c`](../../src/main/drivers/serial_uart.c), [`serial_uart_impl.h`](../../src/main/drivers/serial_uart_impl.h), [`msp_serial.h`](../../src/main/msp/msp_serial.h), [`msp_serial.c`](../../src/main/msp/msp_serial.c), [`tasks.c`](../../src/main/fc/tasks.c)
  - Booting & Memory: [`startup.S`](../../src/platform/ARIES/startup.S), [`aries_v3.ld`](../../src/platform/ARIES/link/aries_v3.ld), [`system.c`](../../src/platform/ARIES/system.c), [`config_streamer.c`](../../src/main/config/config_streamer.c), [`platform.h`](../../src/platform/ARIES/include/platform/platform.h), [`init.c`](../../src/main/fc/init.c), [`betaflight_ARIESV3.map`](../../obj/main/betaflight_ARIESV3.map)
  - Actuators / SPI / Config / Build: [`pwm_motor_aries.c`](../../src/platform/ARIES/pwm_motor_aries.c), [`motor.c`](../../src/main/pg/motor.c), [`core.c`](../../src/main/fc/core.c), [`bus_spi_aries.c`](../../src/platform/ARIES/bus_spi_aries.c), [`target.h`](../../src/platform/ARIES/target/ARIESV3/target.h), [`ARIES.mk`](../../src/platform/ARIES/mk/ARIES.mk), [`stub_drivers.c`](../../src/platform/ARIES/stub_drivers.c)
  - Host Verification: [`src/test/Makefile`](../../src/test/Makefile), [`aries_msp_handshake_unittest.cc`](../../src/test/unit/aries_msp_handshake_unittest.cc), [`aries_system_time_unittest.cc`](../../src/test/unit/aries_system_time_unittest.cc), [`aries_cli_entry_unittest.cc`](../../src/test/unit/aries_cli_entry_unittest.cc)
- **Prior Baseline & Hardware Evidence**: [`06_PHASE_19_AUDIT_AND_DEBT.md`](06_PHASE_19_AUDIT_AND_DEBT.md), [`phase19.5.txt`](../../tempfiles/phase19.5.txt), [`THEJAS32_PROGRAMMER_MANUAL_ANALYSIS.md`](../THEJAS32_PROGRAMMER_MANUAL_ANALYSIS.md)
- **Assumed Operating Load**: Single ARIES v3.0 quadcopter running a `1.0 kHz` soft-float gyro/PID loop on `100 MHz` RV32IM inside SRAM (`0x00202000`), simultaneously servicing `115,200 baud` Configurator MSP/CLI on UART0, `420,000 baud` CRSF RX on UART1, `400 Hz` PWM ESCs on Timer 1 (`PA1`–`PA4`), and `12.5 MHz` polled SPI0 ICM-42688P reads.
- **Verification Snapshot**:
  - All `19` `aries_*` host unit test suites (`285` tests) built and passed under `CC=gcc CXX=g++` (`0` failed).
  - Upstream diff scope vs `885f12404`: `9` files changed under `Makefile`, `mk/`, and `src/main/` (`65 insertions(+), 10 deletions(-)`).
  - Artifact footprint inspected via linker map: `obj/main/betaflight_ARIESV3.bin` (`191,224 B`), total mapped image + `8 KB` stack ending at `_end = 0x00238400`.
  - Cross-toolchain (`riscv32-esp-elf-*`) was unavailable in this execution environment, so ELF rebuild/disassembly (`build_verify.sh` past `[Audit 2/10]`) and physical flashing could not be re-run directly.

---

## 2. Whole-Repo Senior Audit Summary (13 Open Findings)

### Must Fix (8 Blockers for Configurator, Persistence, & Flight Safety)

| # | Area & File | Concrete Problem | Minimal Fix |
| :---: | :--- | :--- | :--- |
| **1** | **UART0 TX Choke & >=256 B Wrap**<br>[`serial_uart_aries.c`](../../src/platform/ARIES/serial_uart_aries.c#L181-L255)<br>[`serial_uart_impl.h`](../../src/main/drivers/serial_uart_impl.h#L47)<br>[`msp_serial.c`](../../src/main/msp/msp_serial.c#L369-L379) | `FCR = 0x00` limits UART0 to 1 byte (`~87 us`), while `TASK_SERIAL` (`100 Hz`) drains only ~1 byte per 10 ms (`~100 B/s`). Worse, `UART_TX_BUFFER_SIZE` is `256` (`255` usable) vs `MSP_PORT_OUTBUF_SIZE` `512`; when `txBuffer` starts empty, `mspSerialSendFrame()` skips checking `serialTxBytesFree()`, so `uartWriteBuf()` ([`serial_uart.c`](../../src/main/drivers/serial_uart.c#L533-L546)) wraps and drops/zeros frames `>= 256` bytes (e.g., `MSP_MULTIPLE_MSP`). | Set `#define UART_TX_BUFFER_SIZE 512` in [`target.h`](../../src/platform/ARIES/target/ARIESV3/target.h), enforce `serialTxBytesFree() < totalFrameLength` unconditionally, and poll `uartPollTx()` in `TASK_MAIN` (`1 kHz`) or use a bounded THRE wait. |
| **2** | **UART0 TX Thread/ISR Race**<br>[`serial_uart_aries.c`](../../src/platform/ARIES/serial_uart_aries.c#L235-L255)<br>[`serial_uart_aries.c`](../../src/platform/ARIES/serial_uart_aries.c#L319-L321) | Both main-loop `uartPollTx()`/`uartEndWrite()` and RX-ISR `uartIrqHandler()` invoke `uartEnableTxInterrupt()` without `thejas32_atomic_enter()`. An RX IRQ arriving between writing `UART_DR` (line 239) and updating `txBufferTail` (line 240) duplicates a byte and corrupts `txBufferTail`. | Wrap `uartEnableTxInterrupt()` in `thejas32_atomic_enter()` / `thejas32_atomic_exit()`. |
| **3** | **Boot Banners & 0 ms CLI Hijack**<br>[`system.c`](../../src/platform/ARIES/system.c#L348-L364)<br>[`ARIES.mk`](../../src/platform/ARIES/mk/ARIES.mk#L66)<br>[`msp_serial.c`](../../src/main/msp/msp_serial.c#L510-L643) | `systemInit()` and `targetPreInit()` unconditionally print `[ARIES v3]` banners on UART0, and `ARIES.mk` still compiles `-DARIES_DEV_LOGGING=1` (violating [`aries_cli_entry_unittest.cc`](../../src/test/unit/aries_cli_entry_unittest.cc#L351-L352)). Meanwhile `USE_IMMEDIATE_CLI` removes the 100 ms guard on `\r`/`\n`/`#`, letting boot line noise flip UART0 into `PORT_CLI` before Configurator's `$M<`. | Remove `-DARIES_DEV_LOGGING=1` from `ARIES.mk`, gate `systemInit()`/`targetPreInit()` UART banners behind `#if defined(ARIES_DEV_LOGGING) && (ARIES_DEV_LOGGING == 1)`, and keep the 100 ms guard for Configurator builds. |
| **4** | **`CONFIG_IN_RAM` Wiped on Warm Reboot**<br>[`config_streamer.c`](../../src/main/config/config_streamer.c#L32-L38)<br>[`startup.S`](../../src/platform/ARIES/startup.S#L96-L104)<br>[`phase19.5.txt`](../../tempfiles/phase19.5.txt#L16) | `ARIES/include/platform/platform.h` never defines `PERSISTENT`, so `eepromData[8192]` lands in `.bss` (`betaflight_ARIESV3.map` shows `.noinit` = `0 B`), `startup.S` zeroes it on every `systemReset()`, and `initEEPROM()` ([`init.c`](../../src/main/fc/init.c#L418-L420)) resets config (`CONFIG: UNCONFIGURED`) on Configurator Save & Reboot. | Define `#define PERSISTENT __attribute__((section(".noinit")))` and `#define USE_PERSISTENT_OBJECTS` in `platform.h` for warm reboots, and implement `MDP_SPI_3` flash (`U5`) storage for power cycles. |
| **5** | **1,000 Hz PWM Clobbers >1,000 us Pulses**<br>[`target.h`](../../src/platform/ARIES/target/ARIESV3/target.h#L236-L237)<br>[`motor.c`](../../src/main/pg/motor.c#L82)<br>[`pwm_motor_aries.c`](../../src/platform/ARIES/pwm_motor_aries.c#L180-L245) | `pg/motor.c:82` sets `useContinuousUpdate = true` for `MOTOR_PROTOCOL_PWM`, so the 1.0 kHz PID loop ([`core.c`](../../src/main/fc/core.c#L1525)) triggers `ariesPwmMotorUpdateComplete()` every `1,000 us` instead of `2,500 us` (`400 Hz`). Commands `> 1,000 us` get reset and pulled `IOHi` again *before* Timer 1's falling-edge ISR fires, latching ESC inputs continuously HIGH. | Gate `ariesPwmMotorUpdateComplete()` on `micros()` period (`>= 2,500 us` at `400 Hz`) and skip starting a cycle while `aries_current_deadline_idx < aries_motor_count` is still running. |
| **6** | **Crash/Reset Leaves Motors Latching HIGH**<br>[`system.c`](../../src/platform/ARIES/system.c#L243-L413)<br>[`pwm_motor_aries.c`](../../src/platform/ARIES/pwm_motor_aries.c#L157-L173) | `handle_trap_panic()`, `failureMode()`, and `systemReset()` do not stop Timer 1 or clear `PA1`–`PA4` LOW (`IOLo`). Disabling `INTR_EN` or halting with `__WFI()` mid-pulse cancels Timer 1 compare IRQs and freezes active motor pins HIGH. | Stop Timer 1 and pull `PA1`–`PA4` LOW via `motorShutdown()` at entry to `handle_trap_panic()`, `failureMode()`, and `systemReset()`. |
| **7** | **SPI0 Locked in Mode 0 for Mode 3 Requests**<br>[`bus_spi_aries.c`](../../src/platform/ARIES/bus_spi_aries.c#L122-L273) | `spiPinConfigure()` sets `pDev->leadingEdge = false` (Mode 3) and `.bss` starts `bus->leadingEdge == false`, while `spiInitDevice()` programs `hw->Control = 0` (Mode 0). Thus `spiSequenceStart()` evaluates `false != false` as false and never sets Mode 3 (`THEJAS32_SPI_CTRL_MODE_3`). | Set `hw->Control` in `spiInitDevice()` to Mode 3, or initialize `bus->busType_u.spi.leadingEdge = true` alongside `hw->Control = 0`. |
| **8** | **`.noinit` Stripped & `MSP_REBOOT` Bootloader No-Op**<br>[`platform.h`](../../src/platform/ARIES/include/platform/platform.h)<br>[`stub_drivers.c`](../../src/platform/ARIES/stub_drivers.c#L136)<br>[`system.c`](../../src/platform/ARIES/system.c#L395-L399) | `USE_PERSISTENT_OBJECTS` is unset in `platform.h`, so `.noinit` in [`betaflight_ARIESV3.map`](../../obj/main/betaflight_ARIESV3.map#L1148-L1154) is `0` bytes (`_snoinit == _enoinit == 0x002363fc`). Also `systemResetToBootloader()` ignores `requestType` and just jumps to firmware `_start`. | Enable `USE_PERSISTENT_OBJECTS` + `PERSISTENT` in `platform.h`, and wire verified ROM bootloader entry (`0x00010000`) or reject `MSP_REBOOT_BOOTLOADER_ROM`. |

### Should Fix (Linker Safety, CI Test Blind Spots, Alignment)

| # | Area & File | Concrete Problem | Minimal Fix |
| :---: | :--- | :--- | :--- |
| **9** | **248 KB Linker Ceiling vs 250 KB Bootloader Map**<br>[`aries_v3.ld`](../../src/platform/ARIES/link/aries_v3.ld#L28)<br>[`THEJAS32_PROGRAMMER_MANUAL_ANALYSIS.md`](../THEJAS32_PROGRAMMER_MANUAL_ANALYSIS.md#L43-L47) | Bootloader terminal log reports `IRAM : [0x200000 - 0x23E7FF] [250 KB]` (ceiling `0x0023E800`), whereas `aries_v3.ld` sets `ORIGIN = 0x00202000, LENGTH = 248K` (ceiling `0x00240000`, `6 KB` higher). Current `_end = 0x00238400` fits (`25,600 B` margin), but future growth can cross `0x0023E800` unnoticed. | Clamp `LENGTH` to `242K` (`0x3C800`, ending at `0x0023E800`) unless silicon testing verifies `0x0023E800`–`0x0023FFFF`. |
| **10** | **Mocked Host Tests Miss Configurator & Time Bugs**<br>[`src/test/Makefile`](../../src/test/Makefile#L69-L178)<br>[`aries_msp_handshake_unittest.cc`](../../src/test/unit/aries_msp_handshake_unittest.cc#L43-L95)<br>[`aries_system_time_unittest.cc`](../../src/test/unit/aries_system_time_unittest.cc#L58-L66) | `aries_msp_handshake_unittest` fakes a `2048`-byte TX buffer and stubs `msp.c`; `aries_system_time_unittest` tests a copied `compute_millis_fast()` helper instead of `system.c`; and `src/test/Makefile` omits `-DARIES_DEV_LOGGING=1` while `ARIES.mk` defines it (`285/285` tests pass despite Findings 1–8). | Link `serial_uart_aries.c` + `msp_serial.c` + `msp.c` with real 256/512-byte ring limits, test `millis()` directly on `system.c`, and align test flags with `ARIES.mk`. |
| **11** | **Missing `-mstrict-align` with Empty `DMA_DATA`**<br>[`ARIES.mk`](../../src/platform/ARIES/mk/ARIES.mk#L59)<br>[`gyro_init.c`](../../src/main/sensors/gyro_init.c#L731)<br>[`accgyro_mpu.c`](../../src/main/drivers/accgyro/accgyro_mpu.c#L274) | `target.h:30` empties `DMA_DATA`, giving `gyroBuf` 1-byte alignment while `mpuGyroReadSPI()` casts `rxBuf` to `int16_t *`, and packed PG/MSP structs omit `-mstrict-align` in `ARIES.mk`. Odd placement risks `mcause = 4/6` alignment trap panics. | Add `-mstrict-align` to `ARCH_FLAGS` in `ARIES.mk` and set `DMA_DATA __attribute__((aligned(4)))`. |

### Nice to Have (Latent Overflow & Lean Cleanups, `-~35 lines`)

| # | Area & File | Concrete Problem | Minimal Fix |
| :---: | :--- | :--- | :--- |
| **12** | **`millis()` 31.7-Day 32-bit Overflow**<br>[`system.c`](../../src/platform/ARIES/system.c#L139-L151)<br>[`betaflight_ARIESV3.map`](../../obj/main/betaflight_ARIESV3.map) | `(carry * 67296U + lo_sum) / 100000U` wraps 32 bits when `carry == 1` and `lo_sum >= 4,294,900,000` (at `hi = 63822, lo = 4294960000`, `~31.73 days` uptime, jumping backwards `-42,950 ms`). `libgcc.a(_udivdi3.o)` is already linked in `betaflight_ARIESV3.map` (`0x00226eb4`). | Replace with `return (uint32_t)(getCycleCounter64() / 100000ULL);` and delete `compute_millis_fast` in `aries_system_time_unittest.cc`. |
| **13** | **Dead `timer_def.h` & `ARIES_DEV_LOGGING` Stub**<br>[`timer_def.h`](../../src/platform/ARIES/timer_def.h#L1-L12)<br>[`pwm_motor_aries.c`](../../src/platform/ARIES/pwm_motor_aries.c#L126-L131) | `timer_def.h` (`12` lines) is unreferenced in `src/platform/ARIES/`, and `ariesPwmMotorPostInit()` + `THEJAS_LOG` exist solely for the `ARIES_DEV_LOGGING` boot log forbidden by test. | Delete `timer_def.h`, remove `THEJAS_LOG` from `platform.h`, and leave `ariesPwmMotorPostInit()` empty without UART output. |

---

## 3. Shortcut Debt Scan Snapshot (`/ponytail:ponytail-debt`)

Scan command: `git grep --no-index --exclude-standard -nE '(#|//|/[*]) ?(shortcut|ponytail):'`  
Result: **`11` code markers, `1` with `no-trigger`** (`20` non-deferral doc/log table hits excluded):

| File & Line | Simplified Mechanism | Named Ceiling | Named Upgrade Trigger | Rot Risk |
| :--- | :--- | :--- | :--- | :---: |
| [`bus_spi_aries.c#L48`](../../src/platform/ARIES/bus_spi_aries.c#L48) | Fixed 1,000-cycle polled SPI0 timeout | `< 140 us` / 14B burst | If controller supports DMA | `managed` |
| [`exti_aries.c#L158`](../../src/platform/ARIES/exti_aries.c#L158) | Bank 0 EXTI single-pin active cap | 1 active Bank 0 pin | If per-pin HW pending reg added | `managed` |
| [`io_aries.c#L75`](../../src/platform/ARIES/io_aries.c#L75) | Open-drain via DIR RMW under MIE guard | Push-pull pad + MIE lock | If HW open-drain pad ctrl added | `managed` |
| [`serial_uart_aries.c#L136`](../../src/platform/ARIES/serial_uart_aries.c#L136) | Clamp baud `> 460800` to `420000` | `420,000` baud (`-0.79%`) | If fractional baud HW exists | `managed` |
| [`serial_uart_aries.c#L181`](../../src/platform/ARIES/serial_uart_aries.c#L181) | 1-byte UART mode (`FCR = 0x00`) | 0-byte HW FIFO depth | When `DW_apb_uart` FIFO erratum fixed | `managed` |
| [`serial_uart_aries.c#L234`](../../src/platform/ARIES/serial_uart_aries.c#L234) | Non-FIFO polled TX drain (`uartPollTx`) | `~100–200 B/s` @ 100 Hz | 1 kHz TX poll or DMA if saturated *(hit now)* | `ACTIVE BREACH` |
| [`stub_drivers.c#L135`](../../src/platform/ARIES/stub_drivers.c#L135) | `persistentObjects` in `.noinit` SRAM | Warm reset only | Until `MDP_SPI_3` flash driver lands | `managed` |
| [`stub_drivers.c#L306`](../../src/platform/ARIES/stub_drivers.c#L306) | `adcGetValue()` returns `0` | Zero ADC readings | Implement `U9` I2C2 before `USE_ADC` | `managed` |
| [`stub_drivers.c#L319`](../../src/platform/ARIES/stub_drivers.c#L319) | Empty/constant nav & altitude stubs | No GPS/baro/pos/alt hold | While disabled on `ARIESV3` | `managed` |
| [`system.c#L416`](../../src/platform/ARIES/system.c#L416) | Custom non-RVC mem ops `#ifndef UNIT_TEST` | *None named* | *None named* | **`no-trigger`** |
| [`target.h#L152`](../../src/platform/ARIES/target/ARIESV3/target.h#L152) | `#undef USE_ADC` | No battery ADC telemetry | Until external I2C2 `U9` implemented | `managed` |

---

## 4. Decision & Action Scoring Rubric (0 to 10) & 3-Part Critique

| Evaluated Area / Decision | Audit Score (0–10) | Technical Justification |
| :--- | :---: | :--- |
| **Configurator <-> Firmware MSP & UART0 Pipeline** | **2.5 / 10** | Single-byte `FCR=0x00` + 100 Hz `uartPollTx()` throttles Configurator TX to `~100 B/s`; `UART_TX_BUFFER_SIZE=256` wraps `>=256 B` MSP frames silently; thread/ISR race in `uartEnableTxInterrupt()`; boot banners + `USE_IMMEDIATE_CLI` corrupt initial COM handshake. |
| **Booting & Trap Recovery Path** | **5.5 / 10** | `_start` at `0x00202000`, GPR wipe, `trap_entry` `x2` save, and auto-masking of unhandled IRQs are solid, **but** `systemResetToBootloader()` ignores ROM DFU, and crash/reset paths never kill Timer 1 or pull motor pins LOW. |
| **Memory Management & Config Persistence** | **4.0 / 10** | Full image fits cleanly (`191,224 B` `.bin`, `_end = 0x00238400`, `31,744 B` under `248 KB` / `25,600 B` under `250 KB` bootloader limit), **but** undefined `PERSISTENT`/`USE_PERSISTENT_OBJECTS` drops `.noinit` to `0 B`, wipes `eepromData` on every warm reboot, `aries_v3.ld` overshoots `0x0023E800` by `6 KB`, and `-mstrict-align` is missing. |
| **Timer 1 PWM Motor & SPI0 Drivers** | **4.0 / 10** | Ascending-deadline Timer 1 design and 12.5 MHz SPI0 clamp are sound concepts, **but** `useContinuousUpdate = true` fires 400 Hz PWM at 1,000 Hz (latching `>1,000 us` pulses continuously HIGH) and `spiSequenceStart()` never switches SPI0 out of Mode 0. |
| **Host Test Realism & Debt Hygiene** | **5.0 / 10** | `19/19` ARIES suites (`285/285`) compile and run fast with MMIO register mocks, **but** MSP handshake, `millis()`, and logging-flag mismatches hide Findings 1–8; `1` `// shortcut:` (`system.c:416`) lacks a trigger and `serial_uart_aries.c:234` already exceeds its stated ceiling. |
| **Composite Runtime Readiness Score** | **4.2 / 10** | **Not ready for Configurator persistence or powered props/ESCs** until Findings 1–8 are fixed and verified on silicon. |

### 3-Part Deep-Dive Critique

#### What Went Wrong
1. **Bench Diagnostic Checks Masked Real Full-Firmware Interactions**: Previous verification relied on `phase19.5.txt` manual CLI spot checks (`status`/`version`/`exit`) and isolated C++ mocks, leaving full-firmware `msp.c` multi-packet frames, `Save & Reboot` persistence across `.bss` clearing, and 1.0 kHz `core.c` continuous PWM updates untested end-to-end.
2. **Incomplete `.noinit` & Persistence Wiring**: Although `aries_v3.ld` carved out `.noinit` and `stub_drivers.c` added `PERSISTENT_OBJECT_MAGIC_VALUE`, `platform.h` omitted `PERSISTENT` and `USE_PERSISTENT_OBJECTS`, silently reducing `.noinit` to `0` bytes and trapping `eepromData[8192]` inside `.bss`.
3. **Asymmetric Test Flags & Mocked Buffers**: Setting `FAKE_BUFFER_CAPACITY = 2048` in `aries_msp_handshake_unittest.cc` and omitting `-DARIES_DEV_LOGGING=1` in `src/test/Makefile` let host tests pass directly over the real `256`-byte TX overflow, `~100 B/s` drain choke, and release boot-log contamination.

#### What Could Have Been Done Better
1. **Treat `uartPollTx()` Ceiling as Active Configurator Load**: Instead of deferring `~100–200 B/s` with a `// shortcut:` note, sizing `UART_TX_BUFFER_SIZE >= 512`, locking `txBufferTail` atomically, and draining at `1 kHz` / bounded `THRE` avoids Configurator timeouts from day one.
2. **Validate Warm-Reboot Persistence & Disarmed-vs-Armed PWM Periods Early**: A single reboot test saving a non-default parameter (`feature` or `pid`) and checking an oscilloscope/logic trace at `1500 us` throttle catches both the `.bss` wipe and `1000 Hz` PWM clobber immediately.

#### What Were the Good / Brilliant Parts
1. **Disciplined Upstream Isolation & Binary Footprint**: Despite adding a brand-new RISC-V target, upstream `src/main/` + `mk/` diffs remain tiny (`9` files, `+65/-10` lines) and full `TARGET=ARIESV3` fits in `191,224 B` `.bin` (`222,208 B` total SRAM incl. `8 KB` stack).
2. **Clean MMIO Separation for Fast Host Execution**: `aries_host_mmio.c` + `285` deterministic host tests run in milliseconds, providing an ideal foundation once `msp.c` and real `system.c`/`ARIES.mk` invariants are wired directly into the suite.
