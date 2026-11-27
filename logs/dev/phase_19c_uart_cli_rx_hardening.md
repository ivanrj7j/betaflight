# Phase 19C (Post-Audit Follow-Up): UART0 CLI RX Tolerance, Floating PA17 Storm Suppression & Hardware-ISR IIR Split (Silicon Defect Still Open)

## 1. Header & Traceability
* **Date:** 2026-11-27
* **Driver Commit Hash:** `b236a26f2` (Active branch: `rebuild`)
* **Target Silicon:** C-DAC ARIES v3.0 (THEJAS32 / VEGA ET1031, RV32IM @ 100 MHz, `0x00202000` SRAM)
* **Physical Bench Outcome (`COM9`, `192,380 B` `.bin`):** **STILL REPRODUCED ON SILICON** — User flashed [`tempfiles/betaflight_ARIESV3.bin`](../../tempfiles/betaflight_ARIESV3.bin) (`7f9d10c93bec39e9cfd67f887b4cea177843758f3672611b9200327de980bfbb`) and confirmed the intermittent CLI input drop / `# ` freeze still occurs; investigation paused at user request to advance to Phase 20 (`MDP_SPI_3`) next.
* **Traceability & Consulted References:**
  * Micro-Plan & Gap Rules: [`PLAN/03_SERIAL_COMMUNICATION_MSP_AND_CLI.md`](../../PLAN/03_SERIAL_COMMUNICATION_MSP_AND_CLI.md), [`CRITICAL_KNOWLEDGE_GAPS_AND_ASSUMPTIONS.md`](../../knowledgebase/CRITICAL_KNOWLEDGE_GAPS_AND_ASSUMPTIONS.md)
  * Evaluation & Audit Nodes: [`06_EVAL_SERIAL_UART_ARIES_C.md`](../../knowledgebase/codebase_evaluations/06_EVAL_SERIAL_UART_ARIES_C.md), [`10_PHASE_19C_UART_CLI_RX_FREEZE.md`](../../knowledgebase/code_audit/10_PHASE_19C_UART_CLI_RX_FREEZE.md)
  * Triple Reference Consultation: [`lib/vega-sdk/vega-sdk/bsp/drivers/uart/uart.c`](../../lib/vega-sdk/vega-sdk/bsp/drivers/uart/uart.c), [`debug_uart.c`](../../lib/vega-sdk/vega-sdk/bsp/drivers/uart/debug_uart.c), [`HardwareSerial.cpp`](../../lib/vega-bsp/cores/arduino/HardwareSerial.cpp), [`06_STM32_SERIAL_UART_DRIVER.md`](../hardware_inspection/06_STM32_SERIAL_UART_DRIVER.md), [`18_ESP32_SERIAL_UART.md`](../hardware_inspection/18_ESP32_SERIAL_UART.md)
  * Companion Dev Logs: [`phase_13_cli_entry.md`](phase_13_cli_entry.md), [`phase_19b_post_audit_remediation.md`](phase_19b_post_audit_remediation.md)

---

## 2. High-Level Executive Overview & Silicon Context

Back-to-back physical hardware captures on the `191,828 B` Phase 19B build in [`tempfiles/audit19b.3.txt`](../../tempfiles/audit19b.3.txt) (matching earlier failures in [`tempfiles/audit19.2.txt`](../../tempfiles/audit19.2.txt), [`tempfiles/audit19.3.txt`](../../tempfiles/audit19.3.txt), [`tempfiles/audit19.4.txt`](../../tempfiles/audit19.4.txt), and [`tempfiles/audit19b.1.txt`](../../tempfiles/audit19b.1.txt)) exhibited reset-to-reset intermittent CLI character loss and permanent RX hangs:
* **Run 1 (`FLASHER -> Boot Complete -> Entering CLI Mode -> # `):** hung immediately on CLI entry.
* **Run 2 (`# version` OK -> `# h` -> `###ERROR IN input: "h"###` -> `# `):** dropped mid-command characters `'e','l','p'` and froze at `# `.
* **Run 3:** `status`, `tasks`, `exit`, `battery_profile 1`, `help`, and `save` all succeeded back-to-back with the identical flashed binary and `read_com9.py` session.

Comparing active `ARIESV3` runtime paths against working `phase19.4.txt` diagnostics ([`bootstrap_diag.c`](../../src/platform/ARIES/bootstrap_diag.c#L1355-L1363)), Phase 13.3 post-mortems ([`phase_13_cli_entry.md`](phase_13_cli_entry.md)), and C-DAC VEGA SDK/BSP sources isolated three software-side driver regressions:
1. [`serial_uart_aries.c`](../../src/platform/ARIES/serial_uart_aries.c) filtered `(lsr & 0x0CU) == 0U` (`PE | FE`) on **`UART0` (`ch == 0`)**, directly reintroducing the Phase 13.3 keystroke-drop defect (`help` -> `h`).
2. **`UART1` (`PA17`, `Header J2_15`)** had `UART_IE = 0x01` and `THEJAS32_IRQ_UART1` enabled unconditionally at `420,000` baud on an unpulled `IOCFG_IN_FLOATING` pad without storm suppression, whereas `bootstrap_diag.c` explicitly masked `UART1` when unattached to prevent spurious break/framing IRQ storms across resets.
3. **`thejas32_uart_set_baudrate_ex()`** set `DLAB = 1` without boundedly waiting for `TEMT` (`0x40U`) or flushing residual `FLASHER` bootloader `RBR`/`LSR` state, and hardware ISRs `thejas32_uart0_isr()` / `thejas32_uart1_isr()` omitted `HardwareSerial.cpp`'s hardware-ISR-only `UART_IIR_FCR` read while keeping `uartIrqHandler()` and polled routines completely free of `IIR` reads.

*(Honest uncertainty note: because THEJAS32 lacks JTAG and `PA22` does not blink during CLI idle under `GYRO: None`, whether an external board trigger outside UART0/UART1 also contributed to Run 1/Run 2's post-`# ` hang awaits physical `COM9` re-flash confirmation.)*

---

## 3. Unit Test Design Rationales (TDD Step 1 — Red to Green)

Four new AAA tests were authored before editing `.c` code and verified **Red** against the unfixed driver, then verified **Green** alongside all updated `UART0` vs. `UART1` assertions (`19/19` ARIES suites, `334/334 PASS`):

* **`Test_UART0_CliAndMspIngestAcceptsLsrErrorFlagsWithoutDroppingKeystrokes`** ([`aries_uart_rx_unittest.cc`](../../src/test/unit/aries_uart_rx_unittest.cc#L971-L1026)):
  * **Condition & AAA:** Streams `"help\r"` + high-bit binary MSP (`0x80`, `0xFF`) across `handle_trap()` (`IRQ 0`) and `thejas32_uart_poll_rx()` with `FE (0x08)`, `PE (0x04)`, and `OE (0x02)` set alongside `DR (0x01)`. Asserts all 7 bytes arrive intact (`serialTotalRxWaiting(sp0) == 7U`) without `PE|FE` drop or `& 0x7F` masking.
  * **Flight/Silicon Consequence:** Prevents CLI commands (`help`, `status`, `version`, `save`) from losing characters on noisy CP2102N/THEJAS32 clock edges per Phase 13.3.
* **`Test_UART1_FloatingPinNoiseStorm_AutoSuppressesIrq1WithoutStarvingUart0AndRecoversWhenClean`** ([`aries_uart_rx_unittest.cc`](../../src/test/unit/aries_uart_rx_unittest.cc#L1031-L1089)):
  * **Condition & AAA:** Opens both `UART0` (`115200`) and `UART1` (`420000`, `PA16`/`PA17`), drives `24` consecutive `DR|BI|FE(|PE)` events on `IRQ 1`, asserts `0` noise bytes reach CRSF callback and `UART1` masks `UART_IE == 0x00` + `THEJAS32_IRQ_UART1` (`INTR_EN` bit 1 = `0`) while `UART0` CLI stays active (`"ver\r"` ingested); then drives clean idle `UART1` `LSR = 0x60` across `100 Hz` `thejas32_uart_poll_rx()` ticks and asserts automatic re-arm and clean `0xC8` CRSF byte ingestion.
  * **Flight/Silicon Consequence:** Stops unattached floating `PA17` (`J2_15`) break storms from starving CLI/scheduler across resets without disabling hot-plug ELRS receiver recovery.
* **`Test_UART_ReconfigureFlushesStaleFlasherRxAndGuardsBusyTemt`** ([`aries_uart_rx_unittest.cc`](../../src/test/unit/aries_uart_rx_unittest.cc#L1094-L1131)):
  * **Condition & AAA:** Seeds `UART0` with stale `0x04` (`XMODEM EOT`) + `DR|FE` (`0x69`) before `uartOpen()`, asserting `RBR`/`LSR.DR` is drained cleanly (`(UART_LSR & 0x01U) == 0U`, RX ring `0`); then calls `uartReconfigure()` with `TEMT = 0` (`UART_LSR = 0x00U`) asserting bounded termination with `LCR = 0x03`, `UART_IE = 0x01`.
  * **Flight/Silicon Consequence:** Eliminates stale bootloader RX garbage and mid-TX divisor framing glitches on SRAM boot handoff (`0x00202000`).
* **`Test_UART_HwIsrAcknowledgesIirOnlyInIsrAndDlabWaitsTemt`** ([`aries_uart_tx_drain_unittest.cc`](../../src/test/unit/aries_uart_tx_drain_unittest.cc#L933-L993)):
  * **Condition & AAA:** Enforces structural presence of `TEMT` (`0x40`) check in `thejas32_uart_set_baudrate_ex()` and `UART_IIR_FCR` acknowledgment inside `thejas32_uart0_isr()` / `thejas32_uart1_isr()` while exercising live `IRQ 0`/`IRQ 1` clean RX delivery alongside `Test_UART_IrqHandler_OmitsPolledIirReadAndDrainsPendingTxSafely`.
  * **Flight/Silicon Consequence:** Matches C-DAC `HardwareSerial.cpp` hardware ISR `IIR` handling without reintroducing Phase 13.4 polled `IIR` `LSR & 0x01` desync.
* **Aligned Existing Tests (`UART0` vs `UART1`):** Updated `Test_RealUartRxAriesC_RingBufferAndErrorRecovery` ([`aries_uart_rx_unittest.cc`](../../src/test/unit/aries_uart_rx_unittest.cc#L919-L951)) and `Test_RealSerialUartAriesC_InitOrderNullGuardsAndPollRxErrors` ([`aries_uart_hw_unittest.cc`](../../src/test/unit/aries_uart_hw_unittest.cc#L636-L685)) so `PE|FE` rejection remains strictly verified on `UART1` CRSF while `UART0` CLI/MSP honors Phase 13.3 (`LSR & 0x01`).

---

## 4. Code Implementation Rationales

* **[`serial_uart_aries.c`](../../src/platform/ARIES/serial_uart_aries.c):**
  * **Bounded `TEMT` + RX Flush across `DLAB` Latch (`thejas32_uart_set_baudrate_ex`):** Waits up to `20000` fenced reads for `UART_LSR & 0x40U` (`TEMT`) before setting `UART_LCR = 0x80 | lcr` so early boot/bootloader characters finish shifting cleanly without blocking indefinitely on mocked/stuck lines; after clearing `DLAB`, drains up to `32` stale RX bytes via `thejas32_uart_read_lsr` / `thejas32_uart_read_rx` prior to `uartReconfigure()` arming `UART_IE`.
  * **Channel-Differentiated RX Ingestion (`uartIngestRxByte` + `uartIrqHandler`):** Extracted `uartIngestRxByte()` (zero duplication between `rxCallback` and circular buffer push). `ch == 0` (`UART0`) ingests every byte whenever `LSR.DR (0x01)` is set (Phase 13.3 & VEGA SDK `debug_uart.c` / `HardwareSerial.cpp`); `ch == 1` (`UART1`) enforces `(lsr & 0x1CU) == 0U` (`PE | FE | BI` rejection while tolerating recoverable `OE` for `420 kbaud` CRSF).
  * **Floating `PA17` Storm Suppression & `100 Hz` Auto-Recovery:** `UART1` counts consecutive `PE|FE|BI` noise bytes (`uartErrorStreak[1]`, reset on any clean byte); reaching `16` consecutive error bytes masks `THEJAS32_UART(1).UART_IE = 0x00U` and clears `THEJAS32_IRQ_UART1`. Inside `thejas32_uart_poll_rx()` (`100 Hz`), if `uartIrqSuppressed[1]` sees `0` noise events and `(UART_LSR & 0x1DU) == 0U` for `5` consecutive ticks (`50 ms`), `UART1` automatically re-arms `UART_IE = 0x01U` and `THEJAS32_IRQ_UART1`. `uartReconfigure()` clears all per-channel suppression counters cleanly.
  * **Hardware-ISR-Only `IIR` Read (`thejas32_uart0_isr`, `thejas32_uart1_isr`):** Performs `volatile uint32_t iir = THEJAS32_UART(x).UART_IIR_FCR; (void)iir;` strictly upon hardware IRQ entry (`HardwareSerial.cpp` parity) while keeping `uartIrqHandler()`, `thejas32_uart_poll_rx()`, and `uartPlatformPollRx()` completely free of `UART_IIR_FCR` reads.

---

## 5. Expected vs. Actual Verification Outputs

* **TDD Red Phase (unfixed driver + new specs):**
  * `make -k -C src/test CC=gcc CXX=g++ -j6 test_aries_uart_rx_unittest test_aries_uart_tx_drain_unittest`:
    * `AriesUartRxTest`: `19 PASS`, **`3 FAILED`** (`Test_UART0_CliAndMspIngestAcceptsLsrErrorFlagsWithoutDroppingKeystrokes`, `Test_UART1_FloatingPinNoiseStorm_AutoSuppressesIrq1WithoutStarvingUart0AndRecoversWhenClean`, `Test_UART_ReconfigureFlushesStaleFlasherRxAndGuardsBusyTemt`).
    * `AriesUartTxDrainTest`: `22 PASS`, **`1 FAILED`** (`Test_UART_HwIsrAcknowledgesIirOnlyInIsrAndDlabWaitsTemt`).
* **TDD Green Phase (all `19` ARIES suites):**
  * Command: `make -C src/test CC=gcc CXX=g++ -j6 test_aries_startup_unittest ... test_aries_rx_crsf_unittest`
  * Actual: **19 / 19 suites PASSED (`334 / 334 PASS`, `0 FAILED`)** — `AriesStartupTest` (`6`), `AriesLinkerTest` (`10`), `AriesAtomicsTest` (`8`), `AriesTargetConfigTest` (`11`), `AriesPgBoundsTest` (`13`), `AriesMcuExcludesTest` (`5`), `AriesSystemTimeTest` (`21`), `AriesMspHandshakeTest` (`17`), `AriesCliEntryTest` (`17`), `AriesIntegrationTest` (`19`), `AriesIoTest` (`18`), `AriesPwmMotorTest` (`24`), `AriesExtiTest` (`23`), `AriesIcm42688pTest` (`22`), `AriesSpiHwTest` (`28`), `AriesUartTxDrainTest` (`23`), `AriesRxCrsfTest` (`29`), `AriesUartRxTest` (`22`), `AriesUartHwTest` (`18`).
* **Bare-Metal RV32IM Binary Build & Tempfiles Sync:**
  * Build: `make TARGET=ARIESV3 -j6 binary` -> `text=191828`, `data=552`, `bss=31584`, `dec=223964` (`0x36adc`).
  * Disassembly check (`riscv32-esp-elf-objdump -d obj/main/betaflight_ARIESV3.elf`): verified pure 32-bit RV32IM instructions in `thejas32_uart_set_baudrate_ex`, `uartReconfigure`, `uartIrqHandler`, and `thejas32_uart1_isr` (`lw a5,8(a5)` hardware ISR `IIR` read intact).
  * Staged `.bin` (`192,380 B`, SHA-256 `7f9d10c93bec39e9cfd67f887b4cea177843758f3672611b9200327de980bfbb`): [`obj/betaflight_2026.12.0-alpha_ARIESV3.bin`](../../obj/betaflight_2026.12.0-alpha_ARIESV3.bin), [`obj/main/betaflight_ARIESV3.bin`](../../obj/main/betaflight_ARIESV3.bin), [`tempfiles/betaflight_2026.12.0-alpha_ARIESV3.bin`](../../tempfiles/betaflight_2026.12.0-alpha_ARIESV3.bin), [`tempfiles/betaflight_ARIESV3.bin`](../../tempfiles/betaflight_ARIESV3.bin).

---

## 6. 10-Point Silicon Invariant Audit Results (`build_verify.sh`)

| Audit | Check | Result | Measured Value |
| :---: | :--- | :---: | :--- |
| **1/10** | Git Branch (`rebuild`) | **PASS** | `rebuild` |
| **2/10** | Target ELF Existence | **PASS** | `obj/main/betaflight_ARIESV3.elf` |
| **3/10** | Binary Footprint (`<= 242 KB`) | **PASS** | `223,964 / 247,808 B` (`23,844 B` headroom) |
| **4/10** | SRAM Origin Entry Point | **PASS** | `0x00202000` |
| **5/10** | Compressed Opcodes (`grep c.`) | **PASS** | `0` RVC instructions |
| **6/10** | Double Soft-Float Helpers | **PASS** | `0` symbols (`__adddf3`/`__muldf3`/`__subdf3`/`__divdf3`) |
| **7/10** | Trap Vector Alignment | **PASS** | `trap_entry = 0x00202100` (`64 B` aligned) |
| **8/10** | Parameter Group Registry | **PASS** | `1,092 B` |
| **9/10** | Top-of-SRAM Bound (`<= 0x0023E7FF`) | **PASS** | `_end = 0x00238ae0` |
| **10/10** | Raw `.bin` Output Check | **PASS** | `obj/main/betaflight_ARIESV3.bin` (`192,380 B`) |

---

## 7. Retrospective & 0 to 10 Decision Scoring Rubric

| Decision / Action | Score (0–10) | Technical Justification |
| :--- | :---: | :--- |
| **UART0 `LSR & 0x01` Restoration (Phase 13.3)** | **9.8 / 10** | Exempting `ch == 0` from `(lsr & 0x0CU)` stops CP2102N/THEJAS32 `PE|FE` flags from dropping CLI keystrokes while preserving `PE\|FE\|BI` noise filtering on `UART1` CRSF and relying on MSP checksums for binary frame integrity. |
| **Self-Recovering Floating `PA17` Storm Guard** | **9.5 / 10** | Masking `UART1` after `16` consecutive `PE\|FE\|BI` errors and auto-rearming after `50 ms` clean idle stops floating-pin IRQ storms across resets without breaking hot-plug ELRS receivers. |
| **HW-ISR-Only `IIR` Split & `TEMT`/Flush Guard** | **9.5 / 10** | Acknowledging `UART_IIR_FCR` strictly inside `thejas32_uart0_isr()`/`thejas32_uart1_isr()` matches `HardwareSerial.cpp` without violating Phase 13.4 polled `LSR` sync, while bounded `TEMT` + 32B RBR flush sanitizes `FLASHER` handoffs (+`552 B` text/data delta). |

### Deep-Dive Critique
* **What Went Wrong Previously:** Re-introducing STM32-style `(lsr & 0x0CU)` filtering on `UART0` contradicted Phase 13.3 silicon evidence, floating `PA17` `IRQ 1` lacked storm protection, `DLAB` lacked `TEMT`/RBR flush, and ISR/polled `IIR` differed from `HardwareSerial.cpp`. **However**, flashing the `192,380 B` binary confirmed the intermittent CLI drop / `# ` freeze **still reproduces**, proving those 4 driver fixes alone are NOT the complete root cause.
* **What Could Have Been Done Better / Open Leads for Next Debug Pass:** Physical silicon lacks `LD1` Green feedback during `GYRO: None` CLI wait (`LD4` status during `# ` hang unconfirmed). Remaining suspects not yet tested: (1) testing `UART0` in pure polled `UART_IE = 0x00` mode matching working `bootstrap_diag.c` vs interrupt-driven `IRQ 0` + `uartPlatformPollRx()`, (2) multi-byte burst overrun on 1-byte `FCR = 0x00` RBR vs CP2102N/host TX cadence, or (3) silent trap / scheduler wedge outside `UART0`/`UART1`.
* **What Were the Good Parts:** Strict Red->Green TDD (`334/334 PASS`), zero duplication via `uartIngestRxByte()`, clean `10/10` silicon audit (`192,380 B` `.bin`, `0` RVC), and cleanly isolating what was ruled out before moving to Phase 20.
