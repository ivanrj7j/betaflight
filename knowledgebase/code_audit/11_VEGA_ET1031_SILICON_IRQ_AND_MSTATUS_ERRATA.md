# Code Audit Node 11: VEGA ET1031 Silicon IRQ Errata — M-Extension Trap Corruption, `csrrci`/`csrc mstatus` `MPIE` Wipe & Configurator 5s Disconnect (`rebuild`)

## 1. Executive Summary & File Traceability
- **Evaluated Files:**
  - [`src/platform/ARIES/mk/ARIES.mk`](../../src/platform/ARIES/mk/ARIES.mk#L61-L63) (`-march=rv32i_zicsr` software mul/div switch)
  - [`src/platform/ARIES/startup.S`](../../src/platform/ARIES/startup.S#L123-L211) (`x5`/`t0` save before `sp` compute + VEGA SDK `INTERRUPT_Handler_0` IRQ epilogue)
  - [`src/platform/ARIES/include/platform/platform.h`](../../src/platform/ARIES/include/platform/platform.h#L105-L181) (ET1031-guarded `thejas32_atomic_enter()` / `thejas32_atomic_exit()` / `__set_BASEPRI*` / `__disable_irq()`)
  - [`src/platform/ARIES/serial_uart_aries.c`](../../src/platform/ARIES/serial_uart_aries.c#L274-L287) & [`#L454-L457`](../../src/platform/ARIES/serial_uart_aries.c#L454-L457) (`uartIrqHandler()` -> `uartEnableTxInterrupt()` inside trap context)
  - [`src/platform/ARIES/aries_probe.c`](../../src/platform/ARIES/aries_probe.c#L488-L517) & [`L779-L1275`](../../src/platform/ARIES/aries_probe.c#L779-L1275) (`Boot Silicon Lab` & `ARIES_PROBE_DEBUG` flight recorder)
- **Associated Unit & ELF Tests:**
  - [`src/test/unit/aries_atomics_unittest.cc`](../../src/test/unit/aries_atomics_unittest.cc#L412-L448) (`Test_AtomicEnter_PreservesMpieWhenMieAlreadyZero`)
  - [`src/test/unit/aries_uart_tx_drain_unittest.cc`](../../src/test/unit/aries_uart_tx_drain_unittest.cc#L684-L782)
  - [`src/test/unit/aries_silicon_runtime_edge_unittest.cc`](../../src/test/unit/aries_silicon_runtime_edge_unittest.cc) (`Runtime07_TrapEntryPreservesX5AndBuildUsesPureRv32i`)
  - [`src/test/unit/aries_bus_gpio_trap_edge_unittest.cc`](../../src/test/unit/aries_bus_gpio_trap_edge_unittest.cc)
  - [`src/test/unit/aries_elf_invariants_unittest.py`](../../src/test/unit/aries_elf_invariants_unittest.py)
  - [`src/test/unit/platform/test_toolchain_flags.py`](../../src/test/unit/platform/test_toolchain_flags.py)
- **Source Hardware Captures:**
  - [`tempfiles/com9_proxy_20261128_155027.txt`](../../tempfiles/com9_proxy_20261128_155027.txt) (**Verified Pass Capture:** `8m43s` total runtime, `13,328` TX / `9,118` RX MSP frames across Run 1 CLI + `exit` reboot + Run 2 `5m49s` live Configurator session with zero disconnects)
  - [`tempfiles/betaflight logs.txt`](../../tempfiles/betaflight%20logs.txt) (User bench report confirming `5m` manual hold & configuration edits with zero drops)
  - `tempfiles/com9_proxy_20261128_152217.txt` (Try 2 failure capture: `Boot Silicon Lab` + 2 Configurator runs `MSP_STALL` + `cmd=3` `MIE_LOST`)
  - `tempfiles/com9_proxy_20261128_152631.txt` (Try 2 failure capture: `Boot Silicon Lab` + 2 Configurator runs `MSP_STALL`)
- **Predecessor Audit Node:** [`knowledgebase/code_audit/10_PHASE_19C_UART_CLI_RX_FREEZE.md`](10_PHASE_19C_UART_CLI_RX_FREEZE.md) (`A10`)
- **Upstream / Reference Drivers:** [`lib/vega-sdk/vega-sdk/bsp/common/crt.S`](../../lib/vega-sdk/vega-sdk/bsp/common/crt.S), [`lib/vega-sdk/vega-sdk/bsp/drivers/interrupt/interrupt.c`](../../lib/vega-sdk/vega-sdk/bsp/drivers/interrupt/interrupt.c#L30-L80), [`lib/vega-sdk/vega-sdk/bsp/include/encoding.h`](../../lib/vega-sdk/vega-sdk/bsp/include/encoding.h#L210-L245)
- **Domain & Status (`e4096eb07` working tree):** **VERIFIED ON PHYSICAL SILICON (`5m49s` continuous hold).** All three root causes of `A10` & 5s disconnects (`M`-extension IRQ trap corruption, `csrrci`/`csrc mstatus, 8` `MPIE` wipe on `txIsr=1`, and `cmd=3` probe false alarm) were isolated and fixed; physical ET1031 capture [`com9_proxy_20261128_155027.txt`](../../tempfiles/com9_proxy_20261128_155027.txt) (`ARIES_PROBE_DEBUG=0` production build, `194,932 B` `.bin`) sustained `2,585` TX / `1,987` RX MSP packets + CLI `help`/`get`/`exit` reboot (Run 1) followed by `10,743` TX / `7,115` RX MSP packets over `5m49s` (Run 2) with zero disconnects until manually closed. Note: `H1_DIV_ATOM`/`H5_DIV_ATOM` `4x nop` re-test with `ARIES_PROBE_DEBUG=1` was not run (`155027.txt` booted the default `ARIES_PROBE_DEBUG=0` image).

---

## 2. Decision & Action Scoring Rubric (0 to 10)

| Decision / Action | Score (0–10) | Evaluation Rationale |
| :--- | :---: | :--- |
| **Root-Cause Isolation of VEGA ET1031 `M`-Extension Trap Erratum (`-march=rv32i_zicsr`)** | **10.0 / 10** | Switching [`ARIES.mk`](../../src/platform/ARIES/mk/ARIES.mk#L63) from `-march=rv32im_zicsr` to `-march=rv32i_zicsr` eliminated hardware `mul`/`div`/`rem` pipeline state corruption when asynchronous interrupts hit active M-extension instructions. Verified directly on ET1031 silicon in `152217.txt` & `152631.txt`: all `30` unguarded `Boot Silicon Lab` stress cases (`T2_GPR_*`, `A*`, `B*`, `B2..B6`, `C*`, `E*`, `F*`, `G*`, `I*`, `J*`) dropped to `0` drops and `0` arithmetic errors, unlocking full Configurator attachment (`ok=16..22`). |
| **Fixing `x5` (`t0`) Pre-Save Order & `INTERRUPT_Handler_0` Epilogue in `startup.S`** | **9.8 / 10** | Saving `x5` before `addi t0, sp, 128` in [`startup.S`](../../src/platform/ARIES/startup.S#L131-L136) stopped trap entry from clobbering interrupted `t0`, and skipping redundant `csrw mepc`/`csrs mstatus, 0x1800` on interrupts (`mcause < 0`) matched VEGA SDK `INTERRUPT_Handler_0` semantics while staying cleanly testable on host. |
| **Isolating 5–6s Configurator `MSP_STALL` (`txIsr=1`, `0x1880 -> 0x1800`) via `TR_H` Flight Recorder** | **9.9 / 10** | Across all `4` Configurator captures in `152217.txt` (`L609-L644`, `L1209-L1242`) and `152631.txt` (`L620-L653`, `L1191-L1227`), every freeze occurred on the **exact single ISR where `txIsr` went `0 -> 1`** (`[20014]`, `[14471]`, `[15100]`, `[14889]`), exiting `handle_trap()` with `mstatusIn=0x00001880 -> mstatusOut=0x00001800`. Combined with `csr=P@26469 0x00001880->0x00001800`, this proved ET1031 `csrrci`/`csrc mstatus, 8` clears `MPIE` (bit 7) whenever `MIE == 0`. |
| **Guarding `csrc mstatus, 8` on `(prev_mstatus & 8U)` & Unified `platform.h` Primitives** | **9.5 / 10** | Reading `csrr` first in [`thejas32_atomic_enter()`](../../src/platform/ARIES/include/platform/platform.h#L112-L120) and skipping `csrc mstatus, 8` when `(prev_mstatus & 8U) == 0U` — shared across `__disable_irq()` and `__set_BASEPRI*()` — eliminated every `csrrc rd, mstatus, 8` from the ELF (`0` remaining) and ensured `csrc` never executes inside trap context (`0x00001880`) or nested atomics. |
| **Suppressing `millis()` False-Positive `MIE_LOST` in `ariesProbeRecordCsr()`** | **9.4 / 10** | Checking `CSR_MIE_DROP` before `millis()` and temporarily clearing `gProbeFastArmed` around `millis()` when `(prevMstatus & 8U) == 0U` in [`ariesProbeRecordCsr()`](../../src/platform/ARIES/aries_probe.c#L488-L517) stopped nested `taskHandleSerial -> uartIrqHandler -> uartEnableTxInterrupt` from dumping `[PROBE DUMP] reason=MIE_LOST` mid-reply on `MSP_FC_VERSION` (`cmd=3`). |
| **Pipeline `nop` Spacer (`H1_DIV_ATOM`/`H5_DIV_ATOM` Mitigation at `csrs + 8`)** | **8.0 / 10 (Pending Silicon)** | `H1_DIV_ATOM` and `H5_DIV_ATOM` in `152217.txt` (`L302-L312`) and `152631.txt` (`L307-L315`) both failed once (`drops=1`, `err=1(k=0x20)`, `m=0x00001800`) at `hwMepc=0x00224298` (`2` instructions after `csrs mstatus, 8`). Adding `4x nop` after `csrs mstatus, 8` keeps any pending IRQ in a side-effect-free shadow, but remains **unverified on silicon** until the next flash. |

---

## 3. Deep-Dive Critique

### What Went Wrong
1. **ET1031 Hardware Erratum 12 — `M`-Extension Trap State Corruption (`-march=rv32im_zicsr`):**
   - Although ET1031 boots advertising `RV32IM`, asynchronous machine interrupts (`mcause = 0x8000000B`) preempting active hardware `mul`/`div`/`rem` instructions corrupt `mstatus.MIE` and/or destination register `rd` across trap return. Under `-march=rv32i_zicsr` software helpers (`__mulsi3`, `__divsi3`, `__udivsi3`), all 30 unguarded `Boot Silicon Lab` stress loops (`T2`, `A*`, `B*`, `B2..B6`, `C*`, `E*`, `F*`, `G*`, `I*`, `J*`) ran with `drops=0` and `err=0`.
2. **ET1031 Hardware Erratum 13 — `csrrci`/`csrc mstatus, 8` Wipes `MPIE` (`bit 7`) When `MIE == 0`:**
   - Inside `thejas32_uart0_isr()` (where hardware holds `mstatus == 0x00001880`, i.e., `MPP=3, MPIE=1, MIE=0`), [`uartIrqHandler()`](../../src/platform/ARIES/serial_uart_aries.c#L455-L457) opportunistically called [`uartEnableTxInterrupt()`](../../src/platform/ARIES/serial_uart_aries.c#L275-L287) whenever an RX IRQ arrived while `txBufferTail != txBufferHead` (`txIsr == 1`).
   - `thejas32_atomic_enter()` executed `csrrci rd, mstatus, 8`, which on ET1031 cleared `MPIE` (`0x00001880 -> 0x00001800`). `thejas32_atomic_exit()` skipped `csrs` because `(prev & 8U) == 0U`, `handle_trap()` returned `mstatusOut = 0x00001800`, and `mret` copied `MPIE (0) -> MIE (0)`. Afterward, only 100 Hz `pollT` caught 1 of 6 bytes per `$M<\x00nn` command (`RX_H: P:6E(61)`), causing Configurator's timeout disconnect ~5s after connection.
3. **Probe Re-Entrancy False Positive on `cmd=3` (`0x00207FD0`):**
   - `taskHandleSerial` enters `thejas32_atomic_enter()` (`MIE=0`) before calling `uartIrqHandler()`, which could enter `uartEnableTxInterrupt()` while `cmd=3`'s reply was still draining (`txT=31 < txH=44`). `ariesProbeRecordCsr('T', 0x1800)` then invoked `millis()`, whose inline `PROBE_CHECK_MIE_INLINE('l')` saw `MIE=0` outside `gProbeInTrap` and emitted `[PROBE DUMP] reason=MIE_LOST` right inside `"2026.12.0-alpha"`.
4. **`csrs mstatus, 8` Pending-IRQ Shadow (`H1_DIV_ATOM` / `H5_DIV_ATOM`):**
   - When an interrupt becomes pending inside an atomic block and fires `2` instructions after `csrs mstatus, 8` (`hwMepc = 0x00224298`), both `tm=1` (`it0=77`) and `tm=5` (`it0=584`) observed `drops=1(noE=0,pend=0)`, `err=1(k=0x20)`, `m=0x00001800`.

### What Could Have Been Done Better
1. **Detecting `0x1880 -> 0x1800` in Static/Host Review Earlier:** Checking `csrc`/`csrrci mstatus, 8` behavior across ISR nesting against ET1031 silicon assumptions earlier would have prevented putting `uartEnableTxInterrupt()` inside `uartIrqHandler()` without an `MIE == 0` skip guard.
2. **De-duplicating `BASEPRI` and Atomic Macros from Day 1:** Prior [`platform.h`](../../src/platform/ARIES/include/platform/platform.h#L105-L177) duplicated raw `csrc mstatus, 8` and `csrs mstatus, 8` inline assembly across 7 separate functions (`__set_BASEPRI`, `__set_BASEPRI_nb`, `__set_BASEPRI_MAX`, `__set_BASEPRI_MAX_nb`, `__disable_irq`, `thejas32_atomic_enter`, `thejas32_atomic_exit`), so any CSR erratum fix would have missed `ATOMIC_BLOCK` (`spiSequence`) if not unified.

### What Were the Good / Brilliant Parts
1. **Hypothesis-Driven `Boot Silicon Lab` + `TR_H` Telemetry:** Instrumenting `mstatusIn -> mstatusOut` per trap and keeping `ARIES_PROBE_DEBUG=1` active turned an intermittent "5-second disconnect" into two exact instruction addresses (`0x00207FD0` and `0x00224298`) and four 100%-reproducible `txIsr=1` `0x1880->0x1800` trap records.
2. **Tiny Pure-RV32I Build Still Well Under Budget:** Even with full `-march=rv32i_zicsr` software mul/div libraries, `4x nop` after `csrs mstatus, 8`, and `ARIES_PROBE_DEBUG=1` enabled, [`betaflight_ARIESV3.elf`](../../obj/main/betaflight_ARIESV3.elf) uses `226,508 / 247,808 B` (`21,300 B` headroom; `194,932 B` `.bin`) with `0` RVC and `0` M-extension opcodes and `59/59` host/ELF verification checks passing.
