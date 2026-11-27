# Phase 19B (Pre-Phase 20): Post-Phase 19 Audit Remediation Log

* **Date:** 2026-11-27
* **Git Branch:** `rebuild`
* **Scope:** Remediation of pre-Phase 20 software, safety, build, persistence, and test findings from [07_POST_PHASE_19_DEEP_AUDIT_AND_DEBT.md](../../knowledgebase/code_audit/07_POST_PHASE_19_DEEP_AUDIT_AND_DEBT.md) and [08_WIDTH_AND_DEPTH_AUDIT_POST_PHASE_19.md](../../knowledgebase/code_audit/08_WIDTH_AND_DEPTH_AUDIT_POST_PHASE_19.md), leaving hardware-dependent verification (`PA0` IRQ 10 polarity, live ICM-42688P orientation/1 kHz WCET) for bench flash and Phase 20 (`MDP_SPI_3`) for Phase 20.

---

## 1. High-Level Executive Overview & Silicon Context

The post-Phase 19 audits identified critical software-level hazards hidden despite 285/285 mocked host tests passing:
1. Unstopped motor PWM outputs during `handle_trap()`, `systemReset()`, `systemResetToBootloader()`, and `failureMode()`, plus `1 kHz` continuous PWM clobbering (`useContinuousUpdate = true`) truncating `> 1000 us` ESC pulses.
2. `UART_TX_BUFFER_SIZE = 256` wrapping `>= 256 B` MSP packets (`MSP_ADJUSTMENT_RANGES` 300 B payload / 306 B framed), unguarded `txBufferTail` writes in `uartEnableTxInterrupt()`, `ARIES_DEV_LOGGING=1` boot wire chatter, and `USE_IMMEDIATE_CLI` bypassing Betaflight's 100 ms CLI quiet guard.
3. Missing `PERSISTENT` / `USE_PERSISTENT_OBJECTS` placing `eepromData[8192]` into `.bss` (`0 B` in `.noinit`), `BOOTLOADER_REQUEST_ROM` masquerading as warm reboot to `0x00202000`, `248K` SRAM linker ceiling overlapping the `0x0023E800..0x0023FFFF` ROM bootloader workspace, and missing `-mstrict-align` / empty `DMA_DATA`.
4. `bus_spi_aries.c` initializing SPI0 in Mode 0 instead of Mode 3 (`leadingEdge == false` for ICM-42688P), empty `unusedPinsInit()`, and stale diagnostic text / software-invoked `thejas32_gpio0_isr()` in `bootstrap_diag.c`.

---

## 2. Unit Test Design Rationales (TDD Step 1 — 43 Remediated Cases)

All 43 test cases were written in AAA (`Arrange` -> `Act` -> `Assert`) form across 12 host suites ([src/test/Makefile](../../src/test/Makefile)) before implementation:
* **Group A ([aries_pwm_motor_unittest.cc](../../src/test/unit/aries_pwm_motor_unittest.cc), 7 tests):** Forced synchronous CPU trap (`mcause=2`, `mcause=7`), soft reset, bootloader entry, and fork-guarded 50 ms `failureMode()` mid-pulse; verified `vTable->shutdown()` idempotency and pin isolation (`PA0`/`PA5`/`PA22`); enforced active-pulse skip at `1000 us`, `2500 us` (`400 Hz`) rate gate with 32-bit `micros()` wrap (`0xFFFFFC18`), and custom `50 Hz` / `OneShot125` rate periods.
* **Group B ([aries_uart_tx_drain_unittest.cc](../../src/test/unit/aries_uart_tx_drain_unittest.cc), [aries_msp_handshake_unittest.cc](../../src/test/unit/aries_msp_handshake_unittest.cc), [aries_rx_crsf_unittest.cc](../../src/test/unit/aries_rx_crsf_unittest.cc), 8 tests):** Enforced `UART_TX_BUFFER_SIZE >= 512`, `306 B` / `309 B` `MSP_ADJUSTMENT_RANGES` burst framing without wrap (caught actual `306 -> 50` modulo wrap in Red phase), Erratum 1 `FCR=0x00` sustained UART0 & `420 kbaud` UART1 CRSF ingest + `OE` recovery, `uartEnableTxInterrupt()` atomic `MIE` locking + `ETBEI` disarm on empty, spurious `THRE` safety, and `700 B` concurrent TX/RX wraparound.
* **Group C ([aries_system_time_unittest.cc](../../src/test/unit/aries_system_time_unittest.cc), [aries_cli_entry_unittest.cc](../../src/test/unit/aries_cli_entry_unittest.cc), 4 tests + updated Phase 13 immediate-CLI tests):** Enforced default `ARIES_DEV_LOGGING=0`, silent `targetPreInit()`/`systemInit()` (`0U` before `systemInit`, BRG `0x36` after), disabled `USE_IMMEDIATE_CLI`, and Betaflight's `100 ms` quiet guard around `'#'`.
* **Group D ([aries_pg_bounds_unittest.cc](../../src/test/unit/aries_pg_bounds_unittest.cc), [aries_system_time_unittest.cc](../../src/test/unit/aries_system_time_unittest.cc), [aries_linker_unittest.cc](../../src/test/unit/aries_linker_unittest.cc), 8 tests):** Enforced `PERSISTENT` (`.noinit`) + `USE_PERSISTENT_OBJECTS`, chunked `config_streamer` `eepromData[8192]` round-trips, `persistentObjects` `'Bef1'` survival, distinct `BOOTLOADER_REQUEST_ROM` safe halt vs `BOOTLOADER_REQUEST_FLASH` reboot, actual `aries_v3.ld` `<= 242K` (`0x0023E800`) + `_stack_size` `ASSERT`, and `build_verify.sh` `247808` check.
* **Group E ([aries_spi_hw_unittest.cc](../../src/test/unit/aries_spi_hw_unittest.cc), [aries_exti_unittest.cc](../../src/test/unit/aries_exti_unittest.cc), [aries_io_unittest.cc](../../src/test/unit/aries_io_unittest.cc), 8 tests):** Enforced `bus_spi_aries.c` Mode 3 (`0x30`) init for `leadingEdge == false`, dynamic Mode 0 <-> Mode 3 switching via `spiSequenceStart()`, CS-wrapped Mode 3 `WHO_AM_I` transfer, `EXTIConfig` input dir preservation + `EXTIRelease` IRQ 10 cleanup + `handle_trap` dispatch + dormancy, and real `unusedPinsInit()` clamping `OWNER_FREE` Bank 0/1 pins LOW while sparing claimed pins.
* **Group F ([aries_system_time_unittest.cc](../../src/test/unit/aries_system_time_unittest.cc), [aries_target_config_unittest.cc](../../src/test/unit/aries_target_config_unittest.cc), [aries_rx_crsf_unittest.cc](../../src/test/unit/aries_rx_crsf_unittest.cc), 8 tests):** Verified 64-bit `millis()` at `71.58 min`/`31.73 d`/`49.71 d`, removed `system.c` orphan `shortcut:`, enforced `-mstrict-align` + `DMA_DATA`, verified `timer_def.h` accuracy, enforced `#undef USE_ADC` / `VOLTAGE_METER_NONE` safety across `15` stability ticks, and removed stale `bootstrap_diag.c` text & synthetic software `thejas32_gpio0_isr()` invocation.

---

## 3. Code Implementation Rationales

* **[system.c](../../src/platform/ARIES/system.c) & [pwm_motor_aries.c](../../src/platform/ARIES/pwm_motor_aries.c):** Added `__attribute__((weak)) ariesPwmEmergencyStop()` in `system.c` (strong implementation in `pwm_motor_aries.c`), invoked from `handle_trap_panic()`, `systemReset()`, `systemResetToBootloader()`, `failureMode()`, and `ariesPwmMotorShutdown()`. Added `#ifdef UNIT_TEST` return in `failureMode()`, safe `WFI` halt on `BOOTLOADER_REQUEST_ROM` (avoiding unverified `0x00010000` jump and `0x00202000` loops), gated boot banners on `ARIES_DEV_LOGGING > 0`, and added active-pulse (`Control & 0x01`) + rollover-safe `motorPwmRate` gating (`2500 us` @ 400 Hz) in `ariesPwmMotorUpdateComplete()`.
* **[serial_uart_aries.c](../../src/platform/ARIES/serial_uart_aries.c):** Wrapped `uartEnableTxInterrupt()` in `thejas32_atomic_enter()` / `thejas32_atomic_exit()`, arming `UART_IE |= 0x02U` (`ETBEI`) only when TX bytes stay pending and clearing `ETBEI` once empty in both `uartEnableTxInterrupt()` and `uartIrqHandler()`.
* **[bus_spi_aries.c](../../src/platform/ARIES/bus_spi_aries.c) & [exti_aries.c](../../src/platform/ARIES/exti_aries.c):** Initialized `hw->Control` to SPI Mode 3 (`THEJAS32_SPI_CTRL_MODE_3 << THEJAS32_SPI_CTRL_MODE_SHIFT`) matching `leadingEdge = false`; documented THEJAS32 lack of SW pull/trigger regs in `exti_aries.c`.
* **[platform.h](../../src/platform/ARIES/include/platform/platform.h) & [stub_drivers.c](../../src/platform/ARIES/stub_drivers.c):** Defined `PERSISTENT __attribute__((section(".noinit")))` and `USE_PERSISTENT_OBJECTS`, annotated `persistentObjects`, and implemented `unusedPinsInit()` over `0..DEFIO_IO_USED_COUNT - 1`.
* **[target.h](../../src/platform/ARIES/target/ARIESV3/target.h), [ARIES.mk](../../src/platform/ARIES/mk/ARIES.mk), [aries_v3.ld](../../src/platform/ARIES/link/aries_v3.ld), [build_verify.sh](../../src/test/unit/aries/build_verify.sh), [timer_def.h](../../src/platform/ARIES/timer_def.h), [bootstrap_diag.c](../../src/platform/ARIES/bootstrap_diag.c):** Enabled `-mstrict-align`, defaulted `ARIES_DEV_LOGGING ?= 0`, set `DMA_DATA` to 4-byte aligned, raised `UART_TX_BUFFER_SIZE` to `512`, disabled `USE_IMMEDIATE_CLI`, clamped RAM to `242K` (`0x0023E7FF`) with `_stack_size >= 0x2000` assertion, synced `build_verify.sh` to `247808` / `0x0023E7FF`, cleaned `timer_def.h` comment, and updated `bootstrap_diag.c` diagnostics.

---

## 4. Expected vs. Actual Verification Outputs

* **TDD Red Phase (pre-fix across 19 ARIES suites):** `326` total (`293 PASSED`, `33 FAILED` on unfixed bugs, `0` compile/link errors).
* **TDD Green Phase (post-fix across all 19 ARIES suites):** **19 / 19 suites PASSED — 326 / 326 PASS, 0 FAILED**:
  * `AriesAtomicsTest`: 8/8 | `AriesCliEntryTest`: 17/17 | `AriesExtiTest`: 23/23 | `AriesIcm42688pTest`: 22/22 | `AriesIntegrationTest`: 19/19 | `AriesIoTest`: 18/18 | `AriesLinkerTest`: 10/10 | `AriesMcuExcludesTest`: 5/5 | `AriesMspHandshakeTest`: 17/17 | `AriesPgBoundsTest`: 13/13 | `AriesPwmMotorTest`: 24/24 | `AriesRxCrsfTest`: 29/29 | `AriesSpiHwTest`: 28/28 | `AriesStartupTest`: 6/6 | `AriesSystemTimeTest`: 20/20 | `AriesTargetConfigTest`: 10/10 | `AriesUartHwTest`: 17/17 | `AriesUartRxTest`: 19/19 | `AriesUartTxDrainTest`: 21/21.
* **Bare-Metal RV32IM Firmware (`make -j4 TARGET=ARIESV3`):**
  * ELF: [obj/main/betaflight_ARIESV3.elf](../../obj/main/betaflight_ARIESV3.elf) (`text=190,812`, `data=552`, `bss=31,500`, `dec=222,864 B` — **24,944 B** headroom under `247,808 B` / `242K`; `.noinit = 0x2000` (`8,192 B` `eepromData`); `_end = 0x00238690 <= 0x0023E7FF`)
  * BIN mirror: [tempfiles/betaflight_ARIESV3.bin](../../tempfiles/betaflight_ARIESV3.bin) (`191,364 B`, SHA-256 `a8a23a2ce8a65fade72d1290a2aae8716a36fb3476ca09aaf1dd657867bf2a00`)
* **Bare-Metal Bootstrap Diagnostic (`bootstrap_diag.c`):**
  * ELF: [obj/main/betaflight_bootstrap.elf](../../obj/main/betaflight_bootstrap.elf) (`text=44,784`, `data=4`, `bss=19,484`, `dec=64,272 B`; `_end = 0x00211b10 <= 0x0023E7FF`)
  * BIN mirror: [tempfiles/betaflight_bootstrap.bin](../../tempfiles/betaflight_bootstrap.bin) (`44,788 B`, SHA-256 `af22da4f3c5842158496c8205b780afcf326e18884c7a040f09ab4455a6fcfb3`)
* **Physical Silicon Benchmark Status:** **SILICON REGRESSION OBSERVED** — `betaflight_ARIESV3.bin` (`191,364 B`) booted into solid purple `LD1` with 0 UART bytes; [`tempfiles/audit19.1.txt`](../../tempfiles/audit19.1.txt) captured an older `11:18:41` bootstrap binary (see Section 7).

---

## 5. 10-Point Silicon Invariant Audit Results (`build_verify.sh`)

* `obj/main/betaflight_ARIESV3.elf`: **10 / 10 PASS** (`rebuild` branch, `222,864 / 247,808 B`, `0x00202000` entry, **0** `c.` RVC opcodes, **0** double-float helpers, `trap_entry = 0x00202100`, `PG = 1092 B`, `_end = 0x00238690 <= 0x0023E7FF`, `191,364 B` `.bin`).
* `obj/main/betaflight_bootstrap.elf`: **PASS** (Audit 8 PG skipped in standalone bootstrap; `64,272 / 247,808 B`, `0x00202000` entry, **0** `c.` RVC opcodes, `_end = 0x00211b10 <= 0x0023E7FF`, `44,788 B` `.bin`).

---

## 6. Retrospective & Decision Scoring Rubric (0–10)

| Decision Area | Score | Technical Justification |
| :--- | :---: | :--- |
| **Motor Safety & PWM Gating** | **9.8 / 10** | Weak/strong `ariesPwmEmergencyStop()` decouples system panics cleanly without linking PWM into non-motor unit tests; Timer 1 active-check + unsigned `motorPwmRate` gating prevents `1 kHz` pulse clobbering cleanly in minimal lines. |
| **MSP TX Ring & Quiet Boot** | **9.6 / 10** | `512 B` TX ring + atomic `uartEnableTxInterrupt()` + `ARIES_DEV_LOGGING ?= 0` + disabled `USE_IMMEDIATE_CLI` removes all COM handshake hazards while strictly honoring Erratum 1 (`FCR=0x00`). |
| **Persistence, Alignment & Linker Bounds** | **9.7 / 10** | Defining `PERSISTENT` / `USE_PERSISTENT_OBJECTS` moved all `8,192 B` of `eepromData` into `.noinit` cleanly; `242K` clamp + `_stack_size` `ASSERT` + `-mstrict-align` + `DMA_DATA` close both bootloader and alignment trap risks. |
| **Driver Honesty & Hardware Readiness** | **4.0 / 10** | Fixed SPI0 Mode 3 & synthetic `bootstrap_diag.c` text, **but** real `unusedPinsInit()` drove active-LOW RGB channels `PA23` (Blue) + `PA24` (Red) `LOW` (`LD1` purple), left UART pins unowned (`IOInit()` missing), and silent boot masked startup status on flash. |

---

## 7. Physical Silicon Bench Regression & Root-Cause Analysis (2026-11-27)

### Observed Bench Evidence
1. **[tempfiles/audit19.1.txt](../../tempfiles/audit19.1.txt) (`Nov 27 13:34`):** Line 236 prints `# Betaflight / ARIESV3 (ARV3) 4.5.0 Nov 27 2026 / 11:18:41 (rebuild)` alongside pre-remediation Test 13/15/17 strings (`Clamped to 16 Bytes`, `Immediate CLI Entry Keys`, `thejas32_gpio0_isr dispatched`), whereas [`tempfiles/betaflight_bootstrap.bin`](../../tempfiles/betaflight_bootstrap.bin) (`44,788 B`) contains `Nov 27 2026 / 13:28:10`. Thus `audit19.1.txt` exercised the earlier `11:18` bootstrap image rather than the rebuilt `13:28` artifact.
2. **Full [`tempfiles/betaflight_ARIESV3.bin`](../../tempfiles/betaflight_ARIESV3.bin) (`191,364 B`, `Nov 27 13:26`):** Flashing stopped at host `"flashing..."` with an onboard **purple LED (`LD1`)** and **0 UART bytes**, whereas the prior [`tempfiles/phase19.5.txt`](../../tempfiles/phase19.5.txt) build (`191,224 B`, `11:17:35`) booted cleanly to CLI.

### Datasheet, BSP & ELF Root-Cause Trace
* **Bootloader Isolation:** Disassembly of [`flasher_min.bin`](../../lib/vega-bsp/bootloaders/arduflasher/flasher_min.bin) and [`flasher_arduino.bin`](../../lib/vega-bsp/bootloaders/arduflasher/flasher_arduino.bin) shows `0` instructions accessing Bank 1 GPIO (`0x10180000`); the bootloader cannot illuminate `LD1`.
* **Purple Light Hardware Mechanism:** Per [`ARIESv3 0_Datasheet_14-10-24.pdf`](../../oldfiles/logs/ARIESv3%200_Datasheet_14-10-24.pdf) (pp. 5, 15) and VEGA BSP [`LED_Series.ino`](../../lib/vega-bsp/libraries/GPIO/examples/LED_Series/LED_Series.ino#L11-L58), `LD1` is **active-LOW (`0` = ON, `1` = OFF)** across `GPIO22` (`LD1_4` Green), `GPIO23` (`LD1_3` Blue), and `GPIO24` (`LD1_2` Red), while `LD4` is the separate `PROC_HB` (Pin 16) LED. Unlike STM32's `IOCFG_IPU` [`unusedPinInit()`](../../src/platform/common/stm32/system.c#L331-L336), [`unusedPinsInit()`](../../src/platform/ARIES/stub_drivers.c#L164-L173) (inlined at `0x203670..0x203698` in `obj/main/betaflight_ARIESV3.elf`, called at [`init.c:1080`](../../src/main/fc/init.c#L1080)) drives all `OWNER_FREE` pins push-pull `IOLo` (`0`):
  * `PA22` (`LED0_PIN`, Green `LD1_4`): claimed `OWNER_LED`, set `IOHi` (`1` = OFF) by [`LED0_OFF`](../../src/main/fc/init.c#L792) / [`ledSet()`](../../src/main/drivers/light_led.c#L82-L89).
  * `PA23` (Blue `LD1_3`) & `PA24` (Red `LD1_2`): unowned (`OWNER_FREE`) $\rightarrow$ driven **`LOW` (`0` = ON)** $\rightarrow$ **`Blue + Red = Purple`**.
* **Silent Boot & Remaining Open Items:** Setting `ARIES_DEV_LOGGING ?= 0` in [`ARIES.mk`](../../src/platform/ARIES/mk/ARIES.mk#L59) muted all [`system.c:364-381`](../../src/platform/ARIES/system.c#L364-L381) boot prints, and removing `USE_IMMEDIATE_CLI` in [`target.h`](../../src/platform/ARIES/target/ARIESV3/target.h#L217) kept UART0 silent pending `'#'` (> 100 ms quiet guard) or MSP `$M<` *(trap/failure paths [`system.c:257`](../../src/platform/ARIES/system.c#L257), [`L445`](../../src/platform/ARIES/system.c#L445) are un-gated, confirming no pre-`initPhase3` panic fired)*. Two runtime risks remain open for inspection before coding a fix:
  1. Solid purple (vs `PA22` 500 ms [`warningLedFlash()`](../../src/main/fc/core.c#L564-L570) Purple/White blink) suggests either host flasher hold after `"flashing..."` or a post-init stall (notably `UART_IE |= 0x02U` added at [`serial_uart_aries.c:246`](../../src/platform/ARIES/serial_uart_aries.c#L246), which [`02_RETROSPECTIVE_HAL_PROGRESS.md`](../../knowledgebase/retrospective_logs/02_RETROSPECTIVE_HAL_PROGRESS.md#L76-L79) H22 flagged for DW_apb_uart IRQ storms).
  2. [`serial_uart_aries.c`](../../src/platform/ARIES/serial_uart_aries.c) never calls `IOInit()` on `PA16`/`PA17`/`PA30`/`PA31`, letting `unusedPinsInit()` drive `GPIO16..17` (`J10`) and `GPIO30..31` (`J9`) LOW.

### 3-Part Bench Regression Critique
* **What Went Wrong:** Implementing `unusedPinsInit()` with `IOLo` + `IOCFG_OUT_PP` without checking active-LOW board peripherals caused `LD1` Blue/Red turn-on (`Purple`), while simultaneous `ARIES_DEV_LOGGING=0` left no UART signal separating silent boot from hang.
* **What Could Have Been Done Better:** Consulted `LD1` schematics/BSP examples before driving `OWNER_FREE` Bank 1 pins LOW push-pull, matched STM32's high-Z `IOCFG_IPU` / `IOCFG_IN_FLOATING` pattern, and tested a standalone silent-boot step before stacking `unusedPinsInit()` + `UART_IE |= 0x02U` in one flash.
* **Good / Brilliant Diagnostic Clue:** Hardware color decoding (`PA22=HIGH`, `PA23=LOW`, `PA24=LOW` = Purple) proved `betaflight_ARIESV3.bin` executed all the way to `initPhase3()` line 1080 without an earlier trap.
