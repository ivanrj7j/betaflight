# Code Audit Node 09: Phase 19B Silicon Bench Purple-LED & Silent-Boot Regression (`rebuild`)

## 1. Executive Summary & File Traceability
- **Incident Scope:** Physical bench flash of Pre-Phase 20 remediation builds on C-DAC ARIES v3.0 (`betaflight_ARIESV3.bin`, `191,364 B`, `Nov 27 13:26`) vs. working Phase 19 baseline ([`tempfiles/phase19.5.txt`](../../tempfiles/phase19.5.txt), `191,224 B`, `11:17:35`) and [`tempfiles/audit19.1.txt`](../../tempfiles/audit19.1.txt).
- **Authoritative Hardware & BSP Sources:**
  - Datasheet: [`oldfiles/logs/ARIESv3 0_Datasheet_14-10-24.pdf`](../../oldfiles/logs/ARIESv3%200_Datasheet_14-10-24.pdf) (pp. 5, 12, 15) & [`ARIES_V3_DATASHEET_ANALYSIS.md`](../ARIES_V3_DATASHEET_ANALYSIS.md#L142-L149)
  - BSP Polarity Examples: [`LED_Series.ino`](../../lib/vega-bsp/libraries/GPIO/examples/LED_Series/LED_Series.ino#L11-L58), [`DIP_Switch.ino`](../../lib/vega-bsp/libraries/GPIO/examples/DIP_Switch/DIP_Switch.ino#L12-L76)
  - Bootloaders Audited (`0` writes to `0x10180000`): [`flasher_min.bin`](../../lib/vega-bsp/bootloaders/arduflasher/flasher_min.bin), [`flasher_arduino.bin`](../../lib/vega-bsp/bootloaders/arduflasher/flasher_arduino.bin)
- **Firmware & Dev Log References:**
  - [`stub_drivers.c`](../../src/platform/ARIES/stub_drivers.c#L164-L173), [`init.c`](../../src/main/fc/init.c#L769-L793), [`init.c#L1080`](../../src/main/fc/init.c#L1080), [`light_led.c`](../../src/main/drivers/light_led.c#L61-L89)
  - [`ARIES.mk`](../../src/platform/ARIES/mk/ARIES.mk#L59), [`system.c`](../../src/platform/ARIES/system.c#L360-L382), [`target.h`](../../src/platform/ARIES/target/ARIESV3/target.h#L207-L231), [`serial_uart_aries.c`](../../src/platform/ARIES/serial_uart_aries.c#L91-L252)
  - [`logs/dev/phase_19b_post_audit_remediation.md`](../../logs/dev/phase_19b_post_audit_remediation.md#L52-L97), [`CRITICAL_KNOWLEDGE_GAPS_AND_ASSUMPTIONS.md`](../CRITICAL_KNOWLEDGE_GAPS_AND_ASSUMPTIONS.md) (`Erratum 11`)

---

## 2. Decision & Action Scoring Rubric (0 to 10)

| Evaluated Decision / Mechanism | Score (0–10) | Technical Rationale |
| :--- | :---: | :--- |
| **Push-Pull `IOLo` in `unusedPinsInit()`** | **1.5 / 10** | Ignored board-level active-LOW RGB LED `LD1` (`GPIO22..24`), forcing `PA23` (Blue) + `PA24` (Red) ON push-pull while `LED0_OFF` held `PA22` (Green) HIGH (`Purple`), unlike STM32's high-Z [`IOCFG_IPU`](../../src/platform/common/stm32/system.c#L331-L336). |
| **Missing `IOInit()` in `serial_uart_aries.c`** | **3.5 / 10** | Left `PA16`/`PA17`/`PA30`/`PA31` as `OWNER_FREE`, letting `unusedPinsInit()` drive `GPIO16..17` (`J10`) and `GPIO30..31` (`J9`) LOW despite dedicated SoC UART pins `107/108` and `45/50`. |
| **Stacking Silent Boot + `ETBEI` (`0x02`) in One Build** | **4.0 / 10** | Enabling `ARIES_DEV_LOGGING ?= 0`, removing `USE_IMMEDIATE_CLI`, and re-enabling `UART_IE |= 0x02U` (`H22` risk) together left zero UART observability when flashing stopped at host `"flashing..."`. |
| **Historical `LD4` vs `LD1` Doc Mislabeling** | **3.0 / 10** | Older target docs labeled `PA22` as `LD4` (`PROC_HB` Pin 16) or "Blue LED" instead of active-LOW `LD1_4` (Green), obscuring RGB color interactions until datasheet + BSP inspection. |
| **Composite Phase 19B Bench Readiness** | **3.0 / 10** | ELF proves execution reached `initPhase3()` (`0x203670`), **but** `betaflight_ARIESV3.bin` is unsafe/unreadable on bench until `unusedPinsInit()` & `UART_IE` / CLI checks are isolated and re-tested. |

---

## 3. Root-Cause & Action Register (`5` Findings)

| # | Finding & Evidence | Observed / Silicon Consequence | Minimal Remediation Direction (Pending Approval) |
| :-: | :--- | :--- | :--- |
| **1** | **Active-LOW `LD1` (`GPIO23` Blue + `GPIO24` Red) driven `LOW` by `unusedPinsInit()`**<br>[`stub_drivers.c#L164-L173`](../../src/platform/ARIES/stub_drivers.c#L164-L173), [`ARIESv3 0_Datasheet_14-10-24.pdf`](../../oldfiles/logs/ARIESv3%200_Datasheet_14-10-24.pdf) p. 15, [`LED_Series.ino#L11-L58`](../../lib/vega-bsp/libraries/GPIO/examples/LED_Series/LED_Series.ino#L11-L58) | `unusedPinsInit()` (`0x203670..0x203698`) drives `PA23=0`, `PA24=0`, while `LED0_OFF` keeps `PA22=1` $\rightarrow$ **Purple `LD1`** on boot. | Switch `unusedPinsInit()` to high-Z `IOCFG_IN_FLOATING` (or claim/park `PA23`/`PA24` `HIGH`), matching board hardware polarity. |
| **2** | **`ARIES_DEV_LOGGING ?= 0` + removed `USE_IMMEDIATE_CLI` hides boot progression**<br>[`ARIES.mk#L59`](../../src/platform/ARIES/mk/ARIES.mk#L59), [`system.c#L364-L381`](../../src/platform/ARIES/system.c#L364-L381), [`target.h#L217`](../../src/platform/ARIES/target/ARIESV3/target.h#L217) | Emits 0 UART0 bytes at reset; terminal waiting past `"flashing..."` sees silence even though [`handle_trap_panic()`](../../src/platform/ARIES/system.c#L257) and [`failureMode()`](../../src/platform/ARIES/system.c#L445) did not trigger. | Keep Configurator wire silent by default (`ARIES_DEV_LOGGING=0`), but test `#` (> 100 ms guard) / `$M<` MSP response explicitly during bench checks. |
| **3** | **Solid Purple vs. `PA22` 500 ms Flash (`warningLedFlash()`) & `UART_IE |= 0x02U` Risk**<br>[`core.c#L564-L570`](../../src/main/fc/core.c#L564-L570), [`serial_uart_aries.c#L246`](../../src/platform/ARIES/serial_uart_aries.c#L246), [`02_RETROSPECTIVE_HAL_PROGRESS.md`](../retrospective_logs/02_RETROSPECTIVE_HAL_PROGRESS.md#L76-L79) | With `NOGYRO`, `TASK_RX` should toggle `PA22` every 500 ms (Purple $\leftrightarrow$ White). Solid purple implies either flasher hold before scheduler cycling or post-`initPhase3` stall (Audit `H22` `ETBEI` storm risk if TX fires). | Isolate whether `#` / MSP responds under `LD1` Purple, and verify whether `UART_IE |= 0x02U` stalls `DW_apb_uart` TX under load. |
| **4** | **`serial_uart_aries.c` never claims `OWNER_SERIAL_TX`/`RX` via `IOInit()`**<br>[`serial_uart_aries.c#L91-L114`](../../src/platform/ARIES/serial_uart_aries.c#L91-L114) | Leaves `PA16`/`PA17`/`PA30`/`PA31` as `OWNER_FREE`, causing `unusedPinsInit()` to drive `GPIO16..17` (`J10`) and `GPIO30..31` (`J9`) LOW. | Call `IOInit()` on configured UART pins in `uartPinConfigure()` if `unusedPinsInit()` drives unowned pins. |
| **5** | **Stale Host `betaflight_bootstrap.bin` Captured in [`tempfiles/audit19.1.txt`](../../tempfiles/audit19.1.txt#L236)**<br>[`tempfiles/audit19.1.txt#L111-L236`](../../tempfiles/audit19.1.txt#L111-L236), [`tempfiles/betaflight_bootstrap.bin`](../../tempfiles/betaflight_bootstrap.bin) | `audit19.1.txt` reports `Nov 27 2026 / 11:18:41` and old Test 13/15/17 strings instead of the `13:28:10` `44,788 B` rebuild. | Confirm host flashing script targets freshly copied `tempfiles/*.bin` timestamps before re-running bench verification. |

---

## 4. Deep-Dive 3-Part Critique

### What Went Wrong
1. Implementing `unusedPinsInit()` with push-pull `IOLo` directly copied an assumption about "unused floating pins" without verifying the ARIES v3.0 schematic (`LD1` active-LOW LEDs on `GPIO22..24`) or STM32's safer `IOCFG_IPU` pattern.
2. Layering silent UART boot (`ARIES_DEV_LOGGING=0`), `unusedPinsInit()`, and `UART_IE |= 0x02U` (`H22` TX interrupt) in a single untested flash obscured whether `"flashing..."` stalled on the host or inside post-`initPhase3()` firmware.

### What Could Have Been Done Better
1. Verify hardware polarity of all `0..31` GPIOs (`ARIESv3 0_Datasheet_14-10-24.pdf` + `lib/vega-bsp` examples) before driving any `OWNER_FREE` pin as a push-pull output.
2. Stage `ARIESV3` bench verification with `make ... ARIES_DEV_LOGGING=1` first when debugging bring-up changes, then verify the `ARIES_DEV_LOGGING=0` release artifact separately.

### What Were the Good / Brilliant Parts
1. Cross-referencing `ARIESv3 0_Datasheet_14-10-24.pdf`, VEGA BSP `LED_Series.ino`, and bootloader disassembly (`0` `0x10180` stores) conclusively isolated `LD1` Purple (`Blue=0, Red=0, Green=1`) to `initPhase3()` line `1080` (`0x203670` in ELF), proving firmware booted cleanly past traps and `failureMode()`.
2. Comparing `audit19.1.txt` timestamp (`11:18:41`) against `tempfiles/betaflight_bootstrap.bin` (`13:28:10`) caught stale-binary test capture immediately before trusting old diagnostic strings.
