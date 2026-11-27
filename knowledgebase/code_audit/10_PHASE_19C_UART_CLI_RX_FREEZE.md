# Code Audit Node 10: Phase 19C Intermittent UART0 CLI Drop & `# ` RX Freeze (`rebuild`)

## 1. Executive Summary & File Traceability
- **Evaluated File:** [`src/platform/ARIES/serial_uart_aries.c`](../../src/platform/ARIES/serial_uart_aries.c)
- **Associated Unit Tests:** [`aries_uart_rx_unittest.cc`](../../src/test/unit/aries_uart_rx_unittest.cc), [`aries_uart_tx_drain_unittest.cc`](../../src/test/unit/aries_uart_tx_drain_unittest.cc), [`aries_uart_hw_unittest.cc`](../../src/test/unit/aries_uart_hw_unittest.cc)
- **Source Hardware & Dev Logs:** [`tempfiles/audit19.2.txt`](../../tempfiles/audit19.2.txt), [`tempfiles/audit19.3.txt`](../../tempfiles/audit19.3.txt), [`tempfiles/audit19.4.txt`](../../tempfiles/audit19.4.txt), [`tempfiles/audit19b.1.txt`](../../tempfiles/audit19b.1.txt), [`tempfiles/audit19b.3.txt`](../../tempfiles/audit19b.3.txt), [`tempfiles/com9_output.txt`](../../tempfiles/com9_output.txt), [`logs/dev/phase_19c_uart_cli_rx_hardening.md`](../../logs/dev/phase_19c_uart_cli_rx_hardening.md)
- **Upstream / Reference Drivers:** [`lib/vega-sdk/vega-sdk/bsp/drivers/uart/uart.c`](../../lib/vega-sdk/vega-sdk/bsp/drivers/uart/uart.c), [`debug_uart.c`](../../lib/vega-sdk/vega-sdk/bsp/drivers/uart/debug_uart.c), [`HardwareSerial.cpp`](../../lib/vega-bsp/cores/arduino/HardwareSerial.cpp), [`UARTClass.cpp`](../../lib/vega-bsp/cores/arduino/UARTClass.cpp), [`logs/hardware_inspection/06_STM32_SERIAL_UART_DRIVER.md`](../../logs/hardware_inspection/06_STM32_SERIAL_UART_DRIVER.md), [`18_ESP32_SERIAL_UART.md`](../../logs/hardware_inspection/18_ESP32_SERIAL_UART.md)
- **Domain & Status (`b236a26f2`):** Intermittent reset-to-reset `UART0` CLI character drops (`# ve`, `# st`, `# s`, `# h`, `# _`) and permanent post-prompt `# ` RX freeze — **STILL OPEN ON PHYSICAL SILICON** (`192,380 B` `.bin` flashed by user; further CLI debugging paused at user request to advance to Phase 20 `MDP_SPI_3`).

---

## 2. Decision & Action Scoring Rubric (0 to 10)

| Decision / Action | Score (0–10) | Evaluation Rationale |
| :--- | :---: | :--- |
| **Root-Cause Elimination via `audit19b.3.txt` Cross-Run Diff** | **9.5 / 10** | Same binary (`Nov 27 2026 / 16:16:14`) and same `read_com9.py` session failed in Runs 1–2 (`# `, `# h` -> `# `) and passed Run 3 (`status`, `tasks`, `save`), proving the failure is silicon/firmware-nondeterministic rather than PC tool, stack overflow (`8192 B`), or TX ring sizing. |
| **Exempting `UART0` from `(lsr & 0x0CU)` (`PE \| FE`) Drop** | **9.6 / 10** | Directly matches verified Phase 13.3 silicon evidence ([`phase_13_cli_entry.md`](../../logs/dev/phase_13_cli_entry.md#L116-L125)) and VEGA SDK (`debug_uart.c`, `HardwareSerial.cpp`) while preserving `(lsr & 0x1CU) == 0U` noise filtering on `420 kbaud` `UART1` CRSF. |
| **Self-Recovering Floating `PA17` (`IRQ 1`) Storm Guard** | **9.3 / 10** | Prevents unpulled floating `PA17` (`Header J2_15`) break/framing storms across resets by masking `UART1` after `16` consecutive `PE\|FE\|BI` bytes and auto-rearming after `50 ms` clean idle, preserving CRSF hot-plug behavior. |
| **HW-ISR-Only `IIR` Read & Bounded `DLAB` `TEMT`+Flush Guard** | **9.4 / 10** | Restores `HardwareSerial.cpp`'s hardware-ISR `UART_IIR_FCR` read inside `thejas32_uart0_isr()`/`thejas32_uart1_isr()` without polluting polled `uartIrqHandler()`, and guards `FLASHER` -> SRAM handoffs with bounded `TEMT` (`0x40`) + `32 B` RBR drain. |
| **Silicon Resolution Outcome (`192,380 B` Flash)** | **4.0 / 10** | Although all 4 fixes passed `334/334` host tests and `10/10` silicon audit invariants, user testing of the flashed `192,380 B` binary **still reproduced the intermittent CLI drop / `# ` freeze**, proving a remaining hardware/software trigger beyond these 4 defects. |

---

## 3. Deep-Dive Critique

### What Went Wrong
1. **Insufficient Hypothesis Coverage on `# ` Permanent Silence:** Even after fixing `(lsr & 0x0CU)` on `UART0`, floating `PA17` `IRQ 1` storms, `DLAB` `TEMT`/RBR flush, and HW-ISR `IIR` acknowledgment, the physical board still reproduced both partial command drops and total `# ` silence.
2. **Insufficient Post-Boot Physical Diagnostics:** With `GYRO: None`, active-LOW `LD1` Green (`PA22`) does not toggle during CLI idle and `LD4` (`PROC_HB`) state during `# ` freeze was unlogged, making it impossible to distinguish CPU/trap starvation from pure `UART0` RX starvation remotely.
3. **Unreconciled Host vs. Silicon Contract Drift:** Earlier tests (`Test_RealUartRxAriesC_RingBufferAndErrorRecovery` and `Test_RealSerialUartAriesC_InitOrderNullGuardsAndPollRxErrors`) had silently re-introduced `UART0` `PE|FE` drop assertions despite Phase 13.3's silicon post-mortem.

### What Could Have Been Done Better / Open Investigation Queue (Pre-Flight Gate)
1. **Ab-Initio Test Comparing `UART0` Polled (`UART_IE = 0x00`) vs. `IRQ 0`:** Working `bootstrap_diag.c` (`phase13.5..phase19.4`) used pure polled `UART0` (`UART_IE = 0x00`) without hangs; testing `UART0` with `THEJAS32_IRQ_UART0` disabled (`uartPlatformPollRx()` at `100 Hz` or fast loop) immediately separates a `THEJAS32` interrupt-controller/`handle_trap` latch from a bus/USB-UART/rate issue.
2. **Confirm Physical Host TX Cadence & `1-Byte RBR` Burst Overrun:** Because Erratum 1 disables FIFOs (`FCR = 0x00`), any `> 87 us` ISR/critical blackout during line-pasted `115200` baud strings overwrites intermediate bytes in single-byte `RBR`.
3. **Verify `LD4` (`PROC_HB`) During `# ` Freeze:** Checking whether `LD4` keeps blinking during a `# ` lockup immediately proves whether the scheduler is alive or trapped before flight testing.

### What Were the Good / Brilliant Parts
1. **Clean Elimination of Non-Causes:** Systematic comparison across 6 historical captures proved `read_com9.py`, stack depth (`8192 B`), TX ring capacity (`512 B`), `IIR`-in-poll, and the 4 patched `UART0`/`UART1` issues are not the remaining culprit — avoiding wasted edits when resuming later.
2. **Honest Documentation & Strict TDD Discipline:** Preserved a reversible, `10/10`-audited `192,380 B` firmware state (`334/334` ARIES unit tests green) with all open physical risks explicitly documented prior to Phase 20 (`MDP_SPI_3`).
