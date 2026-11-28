# Phase 19D (Full-Stack TDD Expansion & Flight Safety Hardening): PWM Non-Finite Guard & 32-Bit Failsafe Wraparound Remediation

## 1. Header & Traceability
* **Date:** 2026-11-28
* **Base Commit / Branch:** `d4fd1b18e` + Phase 19D Changes (Active branch: `rebuild`)
* **Target Silicon:** C-DAC ARIES v3.0 (THEJAS32 / VEGA ET1031, RV32IM @ 100 MHz, `0x00202000` SRAM, `<= 242 KB` bootloader ceiling)
* **Consulted Micro-Plans & Knowledgebase Nodes:**
  * [`PLAN/04_GPIO_EXTI_AND_HARDWARE_TIMERS.md`](../../PLAN/04_GPIO_EXTI_AND_HARDWARE_TIMERS.md), [`PLAN/06_SENSORS_RC_TELEMETRY_AND_SCHEDULER.md`](../../PLAN/06_SENSORS_RC_TELEMETRY_AND_SCHEDULER.md)
  * [`CRITICAL_KNOWLEDGE_GAPS_AND_ASSUMPTIONS.md`](../../knowledgebase/CRITICAL_KNOWLEDGE_GAPS_AND_ASSUMPTIONS.md)
  * [`11_EVAL_PWM_MOTOR_ARIES_C.md`](../../knowledgebase/codebase_evaluations/11_EVAL_PWM_MOTOR_ARIES_C.md)
* **Triple-Reference Implementation Consultation (Prior to Production Edits):**
  * **C-DAC VEGA SDK:** [`lib/vega-sdk/vega-sdk/bsp/include/timer.h`](../../lib/vega-sdk/vega-sdk/bsp/include/timer.h), [`lib/vega-sdk/vega-sdk/bsp/drivers/timer/timer.c`](../../lib/vega-sdk/vega-sdk/bsp/drivers/timer/timer.c)
  * **STM32 Implementation:** [`src/platform/STM32/pwm_output_hw.c`](../../src/platform/STM32/pwm_output_hw.c), [`09_STM32_TIMER_PWM_HW.md`](../hardware_inspection/09_STM32_TIMER_PWM_HW.md)
  * **ESP32 Implementation:** [`src/platform/ESP32/pwm_motor_esp32.c`](../../src/platform/ESP32/pwm_motor_esp32.c), [`22_ESP32_PWM_DSHOT.md`](../hardware_inspection/22_ESP32_PWM_DSHOT.md)

---

## 2. High-Level Executive Overview & Silicon Context

Before proceeding into Phase 20 (`MDP_SPI_3` non-volatile flash & physical flight readiness), all 14 unit, real-C integration, edge-case, and ELF structural suites (**Suites 1–14**) were authored and executed under strict Test-Driven Development (TDD Step 1 -> Step 3). Linking **real unmodified `src/platform/ARIES/*.c`** alongside real Betaflight core modules (`scheduler.c`, `imu.c`, `pid.c`, `mixer.c`, `voltage.c`, `battery.c`, `msp.c`, `msp_serial.c`, `cli.c`, `pg.c`, `config_eeprom.c`, `crsf.c`, `failsafe.c`) against host MMIO hardware models exposed two live silicon-flight defects that superficial mocked tests missed:

1. **Non-Finite Float Bypass in `ariesPwmMotorWrite()` ([`pwm_motor_aries.c`](../../src/platform/ARIES/pwm_motor_aries.c#L208-L224)):** Because IEEE-754 `NAN` returns `false` for both `< 1000.0f` and `> 2000.0f`, converting `NAN` through `(uint32_t)(value * CYCLES_PER_US)` wrote `0` cycles to `THEJAS32_TIMER(1).LoadCount` (illegal underflow/zero-width pulse on ESCs). Furthermore, [`ARIES.mk`](../../src/platform/ARIES/mk/ARIES.mk#L65-L66) builds with `-ffast-math` (`-ffinite-math-only`) and `-fno-builtin-memcpy`, which would strip a naive `isnan(value)` call or emit external `memcpy()` calls at 4 kHz.
2. **Unsigned 32-bit `millis()` Rollover Comparisons in [`failsafe.c`](../../src/main/flight/failsafe.c#L162-L556):** Three raw unsigned `millis() > ...Period` comparisons (`failsafeShouldHaveCausedLandingByNow()` line 164, low-throttle Stage 2 check line 364, and RX recovery period line 548) inverted polarity across the `0xFFFFFFFF -> 0` wraparound boundary, causing premature mid-flight motor disarm (`FAILSAFE_LANDED`) after 120 ms instead of `FAILSAFE_LANDING` when `throttleLowPeriod` wrapped into `0x00001288`.

Both defects were reproduced as **TDD-RED**, fixed with minimal, `-ffast-math`-immune production code, verified **481/481 PASS** across all **32 ARIES test suites**, compiled cleanly into [`obj/main/betaflight_ARIESV3.bin`](../../obj/main/betaflight_ARIESV3.bin) (`192,408 B`, `0` RVC instructions), and staged to [`tempfiles/`](../../tempfiles).

---

## 3. Unit Test Design Rationales (Living Documentation, Waves 1–3)

* **Wave 1 (Pre-Flight Hardening & Real-C Migrations — Suites 1–7, 129 tests):**
  * [`aries_config_flash_unittest.cc`](../../src/test/unit/aries_config_flash_unittest.cc) (24 tests): Verifies `CONFIG_IN_RAM` 8 KB allocation, zero XIP flash dereference, and flash-store panic boundaries.
  * [`aries_scheduler_integration_unittest.cc`](../../src/test/unit/aries_scheduler_integration_unittest.cc) (16 tests): Links real `scheduler.c` under 1 kHz gyro/PID clamping and checks > 65% CPU idle headroom.
  * [`aries_preflight_uart_msp_unittest.cc`](../../src/test/unit/aries_preflight_uart_msp_unittest.cc) (20 tests): Exercises `startup.S` `_sbss`/`_ebss` word clearing, UART0 DLAB divisor 54, and pre-flight MSP readiness.
  * [`aries_system_time_unittest.cc`](../../src/test/unit/aries_system_time_unittest.cc) (22 tests): Tests real `system.c` `micros()`/`millis()`, Timer 0 overflow rollover, and 32-bit reciprocal timing accuracy.
  * [`aries_msp_handshake_unittest.cc`](../../src/test/unit/aries_msp_handshake_unittest.cc) (18 tests), [`aries_cli_entry_unittest.cc`](../../src/test/unit/aries_cli_entry_unittest.cc) (18 tests), and [`aries_linker_unittest.cc`](../../src/test/unit/aries_linker_unittest.cc) (11 tests): Verify MSP error (`!`) responses, immediate `#`/CR CLI activation, `0x00202000` SRAM origin, and `.noinit`/`.stack` section ordering.
* **Wave 2 (Full Flight Stack, Battery/ADC & ELF Binary Audit — Suites 8–10, 28 tests):**
  * [`aries_imu_pipeline_integration_unittest.cc`](../../src/test/unit/aries_imu_pipeline_integration_unittest.cc) (6 tests): Real `bus_spi_aries.c` + `accgyro_spi_icm426xx.c` + `imu.c` 1 kHz Mahony AHRS pipeline under 12.5 MHz SPI0 clamp.
  * [`aries_fc_flight_safety_unittest.cc`](../../src/test/unit/aries_fc_flight_safety_unittest.cc) (6 tests): Real IMU tilt -> `pid.c` -> `mixer.c` -> `pwm_motor_aries.c` pipeline. **`Test_PwmWrite_NaNAndInfinity_ClampedSafelyWithin1000To2000`** arranges M0=`NAN` (plus M1..M3=`1500.0f`), acts via `updateComplete()`, and asserts `THEJAS32_TIMER(1).LoadCount >= 100000UL && <= 200000UL`, plus `-INFINITY`/`INFINITY` clamping. *Flight consequence:* prevents zero-width ESC desync or divide-by-zero full-throttle flyaway.
  * [`aries_adc_i2c_battery_unittest.cc`](../../src/test/unit/aries_adc_i2c_battery_unittest.cc) (6 tests): Verifies VEGA SDK `I2CReg(2)` `0x10000A00` Timer 0 address collision guard, `#undef USE_ADC`, and real `voltage.c` / `battery.c` cell/warning/critical state transitions.
  * [`aries_msp_cli_full_protocol_unittest.cc`](../../src/test/unit/aries_msp_cli_full_protocol_unittest.cc) (6 tests): Links real `msp.c` (`--gc-sections`), `msp_serial.c`, and `serial_uart_aries.c` over UART0 MMIO rings for Configurator commands, armed `MSP_EEPROM_WRITE` lockout, `#` CLI entry, and 100 ms reboot guard.
  * [`aries_elf_invariants_unittest.py`](../../src/test/unit/aries_elf_invariants_unittest.py) (4 tests): Validates `obj/main/betaflight_ARIESV3.{elf,bin,map}` binary size, `0x00202000` entry, `.riscv.attributes` RV32IM + `STT_FUNC` pure 32-bit opcode scan (`0` RVC), `.noinit`/`.bss` ordering, and zero XIP references.
* **Wave 3 (Deep Protocol, Migration, Failsafe, Bus/EXTI & Silicon Runtime Edge Cases — Suites 11–14, 55 tests):**
  * [`aries_pg_eeprom_migration_unittest.cc`](../../src/test/unit/aries_pg_eeprom_migration_unittest.cc) (12 tests): Real `pg.c` + `config_eeprom.c` + `config_streamer.c` serialization, version mismatch fallback, FNV-1a bit-flip detection, `8192 B` RAM bounds, corrupted size header abort, and `MDP_SPI_3` A/B torn-write rollback model.
  * [`aries_rx_link_failsafe_edge_unittest.cc`](../../src/test/unit/aries_rx_link_failsafe_edge_unittest.cc) (13 tests): Real `crsf.c` + `failsafe.c` 11-bit boundary packing, `1750 us` inter-byte gap resync, kill-switch/landing/recovery lifecycles, and **`RxEdge12_TDD_RED_FailsafeMillisWraparoundAcross0xFFFFFFFF`** placing high throttle at `0xFFFFFF00UL` (`throttleLowPeriod = 0x00001288`), cutting throttle & RX at `0xFFFFFF10UL`, and asserting `failsafePhase() == FAILSAFE_LANDING` at `0xFFFFFF88UL`. *Flight consequence:* prevents mid-air instant motor cut on 49.7-day uptime wrap or wrapped time offsets.
  * [`aries_msp_cli_protocol_edge_unittest.cc`](../../src/test/unit/aries_msp_cli_protocol_edge_unittest.cc) (12 tests): Real `msp.c` + `msp_serial.c` + `cli.c` + `serial.c` + `buf_writer.c` + `serial_uart_aries.c` covering MSPv1 `0/192/193 B`, MSPv2-over-V1 (`255`) & `$X<` CRC8, garbage resync, `MSP_SKIP_NON_MSP_DATA`, peripheral (`FUNCTION_VTX_MSP`/`LIDAR`) isolation, `PORT_CLI_CMD` STX/ETX & 2s idle exit, interactive comment stripping & 256B line clamp + `CTRL-D`, multi-port UART0/UART1 isolation, and THRE backpressure `cliHelp` flush across the 256 B TX ring.
  * [`aries_bus_gpio_trap_edge_unittest.cc`](../../src/test/unit/aries_bus_gpio_trap_edge_unittest.cc) (12 tests, Edge 01–14): `spiSequence`/`spiWait`/`spiRelease`, UART `size-1` ring full & 8E2 DCR framing, `460800` vs `460801 -> 420000` clamp, nested `ATOMIC_BLOCK` & CSR `mstatus.MIE`, Bank 0 single-pin EXTI contention/release vs Bank 1, SVF near-Nyquist `490 Hz @ 1 kHz` stability, `icm426xxSpiDetect` missing MISO `failureMode()` emergency stop (`--wrap=delay`), and synchronous `mcause = {2, 4, 5, 6, 7}` trap cuts.
  * [`aries_silicon_runtime_edge_unittest.cc`](../../src/test/unit/aries_silicon_runtime_edge_unittest.cc) (6 tests): `startup.S` word loops, dirty `.noinit` cold-SRAM magic recovery, multi-IRQ simultaneous CTZ dispatch, `stub_drivers.c` safe ADC returns, clean `systemReset()`, and `-mstrict-align` packed unaligned protocol decoding.

---

## 4. Code Implementation Rationales

1. **Bit-Level IEEE-754 Non-Finite Guard ([`src/platform/ARIES/pwm_motor_aries.c`](../../src/platform/ARIES/pwm_motor_aries.c#L208-L224)):**
   * **Decision:** Added `union { float f; uint32_t u; } conv = { .f = value }; if ((conv.u & 0x7F800000UL) == 0x7F800000UL) { value = 1000.0f; }` prior to the `[1000.0f, 2000.0f]` clamp.
   * **Silicon Rationale:** [`ARIES.mk`](../../src/platform/ARIES/mk/ARIES.mk#L65-L66) passes `-ffast-math` (which implies `-ffinite-math-only` and can eliminate `isnan(value)`) and `-fno-builtin-memcpy`. Union exponent masking checks all non-finite IEEE-754 patterns (`NaN`, `±Inf`) with zero `libm`/`memcpy` calls, clamping them conservatively to the disarmed idle pulse (`1000.0f` -> `100000` cycles on Timer 1) so no arithmetic exception can command full throttle. Verified directly in `riscv32-esp-elf-objdump -d obj/main/betaflight_ARIESV3.elf` (`lui a5,0x7f800; and a4,a5,a1; beq a4,a5,...`) with 0 compressed opcodes.
2. **Signed Wraparound-Safe `cmp32()` Deadline Checks ([`src/main/flight/failsafe.c`](../../src/main/flight/failsafe.c#L162-L556)):**
   * **Decision:** Replaced raw `millis() > ...Period` unsigned comparisons at lines `164` (`landingShouldBeFinishedAt`), `364` (`throttleLowPeriod`), and `548` (`receivingRxDataPeriod`) with `cmp32(millis(), ...) > 0`, matching existing `cmp32()` usage at lines `203`, `222`, `232`, and `499` in the same translation unit.
   * **Silicon Rationale:** `cmp32(a, b)` casts `(int32_t)((uint32_t)a - (uint32_t)b)`, remaining exact across the `0xFFFFFFFF -> 0` modular wraparound boundary for any timer span `< 2^31 - 1 ms`.

---

## 5. Expected vs. Actual Outputs

| Command / Check | Expected Metric | Observed Actual Output | Variance / Status |
| :--- | :--- | :--- | :--- |
| `make -C src/test -k -j4 ...` (All 31 `test_aries_*_unittest` suites) | All 477 C++ tests pass after final edit (TDD-RED resolved) | **477 / 477 PASSED across 31/31 C++ suites** (`6+8+12+18+24+23+6+22+6+19+18+11+5+6+12+18+13+12+20+24+29+13+16+6+28+6+22+11+18+22+23`) | **0 failures, 0 skipped** |
| `python3 src/test/unit/aries_elf_invariants_unittest.py` | 4 / 4 structural ELF/BIN checks pass (`<= 248 KB`, `0` RVC) | **4 / 4 PASSED (`OK` in 0.194s)** | Exact match |
| `make TARGET=ARIESV3 -j4` | Clean link `<= 242 KB` SRAM ceiling (`0x0023E7FF`) | `text=191856`, `data=552`, `bss=31576`, `dec=223984 (0x36af0)`, `.bin = 192,408 B`, `_end = _estack = 0x00238af0` | `23,824 B` below `242K` cap (+28 B vs `192,380 B` 19C `.bin`) |
| `riscv32-esp-elf-objdump -d obj/main/betaflight_ARIESV3.elf \| grep -cE "[[:space:]]c\."` | Strictly `0` RVC compressed opcodes | **`0`** (`RVC_COUNT=0`; pure 32-bit `lui`/`and`/`beq` verified in `<ariesPwmMotorWrite>`) | **Exact `0`** |

---

## 6. 10-Point Silicon Invariant Audit Results

| # | Silicon Invariant | Verification Method | Result |
| :---: | :--- | :--- | :---: |
| 1 | **Active Branch `rebuild`** | `git branch --show-current` == `rebuild`; 0 edits to `master` | **PASS** |
| 2 | **Strictly `0` RVC Compressed Instructions** | `riscv32-esp-elf-objdump -d` `grep -cE "[[:space:]]c\." == 0` & Python `STT_FUNC` scan | **PASS** |
| 3 | **In-SRAM Execution Origin `0x00202000`** | `readelf -h -S` (`Entry point 0x202000`, `_start = 0x00202000`) | **PASS** |
| 4 | **Binary Size `<= 248 KB` (`<= 242 KB` Boot Cap)** | `.bin = 192,408 B`; `.elf` SRAM `_end = 0x00238af0` (`223,984 B` <= `247,808 B`) | **PASS** |
| 5 | **Stack Bounds (`>= 8192 B` before `0x0023E7FF`)** | `.stack` anchored in [`aries_v3.ld`](../../src/platform/ARIES/link/aries_v3.ld) with `ASSERT` | **PASS** |
| 6 | **`.noinit` (`PERSISTENT`) Preserved** | `readelf -S`: `.noinit = 0x00234ab0` (`0x2040` / `8,256 B` after `_ebss`) | **PASS** |
| 7 | **`CONFIG_IN_RAM` & `EEPROM_SIZE 8192`** | [`aries_pg_eeprom_migration_unittest.cc`](../../src/test/unit/aries_pg_eeprom_migration_unittest.cc) 12/12 PASS | **PASS** |
| 8 | **Soft-Float `-ffast-math` Safety** | `<ariesPwmMotorWrite>` disassembly retains bit-level `0x7F800000` guard | **PASS** |
| 9 | **MMIO & Timing Wraparound Determinism** | `aries_system_time` (22/22) & `aries_rx_link_failsafe_edge` (13/13) PASS | **PASS** |
| 10 | **Binary Artifact Staged & Relative Links Only** | `tempfiles/betaflight_ARIESV3.bin` (`192,408 B`, SHA-256 `a36404f0...`); 0 `/home/` links in docs | **PASS** |

---

## 7. Retrospective & 0 to 10 Decision Scoring Rubric

| Decision / Action | Score (0–10) | Technical Justification |
| :--- | :---: | :--- |
| **Union IEEE-754 Exponent Mask Over `isnan()` under `-ffast-math`** | **10.0 / 10** | Immune to `-ffast-math` (`-ffinite-math-only`) constant folding, avoids `libm`/`memcpy` calls under `-fno-builtin-memcpy`, proven in RV32IM disassembly. |
| **Clamping Non-Finite `+Inf` to Safe Idle (`1000.0f`)** | **9.8 / 10** | Prevents divide-by-zero arithmetic blowups from commanding 100% full throttle (`2000 us`), favoring fail-safe motor stop while passing range checks. |
| **Uniform `cmp32()` Conversion Across All `failsafe.c` Deadlines** | **10.0 / 10** | Eliminates wraparound inversion on `landingShouldBeFinishedAt`, `throttleLowPeriod`, and `receivingRxDataPeriod` consistently with lines 203/222/232/499. |
| **Linking Real Production `.c` Drivers in Waves 1–3 Test Matrix** | **10.0 / 10** | Exposed both genuine production defects (`pwm_motor_aries.c` NaN bypass & `failsafe.c` wraparound) plus `-fno-builtin-memcpy`/`-ffast-math` interaction before hardware flight. |

**Three-Part Critique:**
* **What went wrong earlier:** Host tests mocked math/failsafe paths too lightly, leaving `ariesPwmMotorWrite()` without non-finite protection and `failsafe.c` with 3 legacy raw unsigned timer comparisons.
* **What could have been done better:** Upstream [`flight_failsafe_unittest.cc`](../../src/test/unit/flight_failsafe_unittest.cc) still uses `_Static_assert` C11 syntax blocked under GNU `g++` (requires clang); [`aries_rx_link_failsafe_edge_unittest.cc`](../../src/test/unit/aries_rx_link_failsafe_edge_unittest.cc) bridges that gap under `gcc`/`g++` until clang CI is used.
* **Good / Brilliant Parts:** Inspecting `ARIES.mk` (`-ffast-math`, `-fno-builtin-memcpy`) during mandatory reference consultation prevented writing an `isnan()` check that the target cross-compiler would silently delete at compile time.
