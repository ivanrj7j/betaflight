# Evaluation Node 17: Non-Inverted CRSF Receiver & Safe Battery Telemetry (`serial_uart_aries.c`, `crsf.c`, `stub_drivers.c`)

## 1. Executive Summary & File Traceability
- **Evaluated Files**:
  - [`src/platform/ARIES/serial_uart_aries.c`](../../src/platform/ARIES/serial_uart_aries.c#L128-L351)
  - [`src/main/rx/crsf.c`](../../src/main/rx/crsf.c#L659-L700)
  - [`src/platform/ARIES/stub_drivers.c`](../../src/platform/ARIES/stub_drivers.c#L293-L312)
  - [`src/platform/ARIES/target/ARIESV3/target.h`](../../src/platform/ARIES/target/ARIESV3/target.h#L73-L176)
  - [`src/platform/ARIES/bootstrap_diag.c`](../../src/platform/ARIES/bootstrap_diag.c#L1119-L1330)
  - [`src/test/unit/aries_rx_crsf_unittest.cc`](../../src/test/unit/aries_rx_crsf_unittest.cc#L530-L1170)
- **Source Log / Test Record**:
  - [`logs/dev/phase_19_crsf_rx_battery.md`](../../logs/dev/phase_19_crsf_rx_battery.md)
  - [`logs/dev/phase_19_crsf_rx_battery.log`](../../logs/dev/phase_19_crsf_rx_battery.log)
  - [`tempfiles/phase19.1.txt`](../../tempfiles/phase19.1.txt)
- **Upstream / Reference Drivers**:
  - [`lib/vega-sdk/vega-sdk/bsp/drivers/uart/uart.c`](../../lib/vega-sdk/vega-sdk/bsp/drivers/uart/uart.c)
  - [`logs/hardware_inspection/11_STM32_ADC_IMPL.md`](../../logs/hardware_inspection/11_STM32_ADC_IMPL.md)
  - [`logs/hardware_inspection/23_ESP32_ADC.md`](../../logs/hardware_inspection/23_ESP32_ADC.md)
  - [`src/platform/STM32/serial_uart_stm32f4xx.c`](../../src/platform/STM32/serial_uart_stm32f4xx.c)
  - [`src/platform/ESP32/serial_uart_esp32.c`](../../src/platform/ESP32/serial_uart_esp32.c)
- **Domain**: Non-inverted ExpressLRS/CRSF RX on `UART1` (`420,000 baud`, Divisor `15`), baud clamping, atomic `LSR` error preservation, `uartGetIndex(NULL)` safety, deferred RX IRQ arming, and `ARMING_DISABLED_BATTERY` lockout prevention (`#undef USE_ADC` + honest `adcGetValue() == 0` fallback).

---

## 2. Decision & Action Scoring Rubric (0 to 10)

| Decision / Action | Score (0-10) | Evaluation Rationale |
| :--- | :---: | :--- |
| **1. Non-Inverted CRSF on `UART1` (Divisor 15)** | 10/10 | Matches THEJAS32 16550 non-inverted UART IP (`PA16`/`PA17`) at `416,667 bps` ($-0.79\%$ error) without an external inverter. |
| **2. Synchronized High-Baud Clamping (`thejas32_uart_clamp_baudrate`)** | 10/10 | Prevents 921.6k ($-3.12\%$ error) framing corruption and ensures `s->port.baudRate` reflects clamped `420000`. |
| **3. Bounded 32B RX Drain & Single-Read `LSR` Filtering** | 10/10 | Post-Audit 06 fix drops the `LSR` pre-read in `thejas32_uart_poll_rx()`, preserving `PE|FE` flags alongside `uartGetIndex(NULL) == -1` and deferred `UART_IE` arming until `uartReconfigure()`. |
| **4. `crsfRxInit()` `SERIAL_PORT_NONE` Reset** | 10/10 | Prevents stale `serialPort` pointer across reconfiguration without modifying protocol logic. |
| **5. Honest `adcGetValue() == 0` + `#undef USE_ADC` Guard** | 10/10 | Post-Audit 06 removed the spoofed `aries_adc_raw` (`18.15 V`) array; `#undef USE_ADC` keeps `VOLTAGE_METER_NONE` active cleanly until real `U9` I2C2 reads land (`// shortcut:` annotated). |
| **6. Empirical `IIR` (`0x01`), `diag_failures` Gate & `420 us` WCET** | 10/10 | Corrected `IIR` vs `FCR` readback check (`(u1_iir & 0xC0U) == 0U`), gated Tests `01..21` on `diag_failures == 0`, and built full `TARGET=ARIESV3` (`222,096 B` SRAM, `0` RVC). |

---

## 3. Deep-Dive Critique

### What Went Wrong
1. **Uninverted `IIR` vs `FCR` Readback Assumption (`phase19.txt`)**:
   - Offset `0x08` (`UART_IIR_FCR`) writes `FCR` but reads `IIR`. Bit `0 == 1` indicates no interrupt pending, so `(u1_iir & 0x01U) == 0U` misreported `[CHECK CONFIG]` on healthy silicon; bits `[7:6] == 00` are the authoritative FIFO-disabled proof (`IIR=0x00000001`).
2. **Audit 06 UART & Mock Battery Hazards (Remediated `2026-11-27`)**:
   - Pre-Audit `serial_uart_aries.c` had an `LSR` double-read in `thejas32_uart_poll_rx()`, armed `UART_IE` before `uartOpen()` configured `rxCallback`, and aliased `uartGetIndex(NULL)` to `UART0`; `stub_drivers.c` spoofed `18.15 V` via `aries_adc_raw[]`. All four were fixed and covered by real-C-driver tests (`aries_rx_crsf 26/26`, `aries_uart_hw 17/17`, `aries_uart_rx 19/19`).
3. **`crsfRxInit()` Early-Return State Leak**:
   - Returning `false` on `SERIAL_PORT_NONE` previously left static `serialPort` pointing to an earlier session's handle.

### What Could Have Been Done Better
1. **Physical Bench External Hardware Attachment**:
   - Live RF ExpressLRS RX packets on `PA17`, `U9` `I2C2` battery divider reads, and host XMODEM execution of the newly built full `TARGET=ARIESV3` [`betaflight_ARIESV3.bin`](../../tempfiles/betaflight_ARIESV3.bin) (`191,112 B`) and hardened [`betaflight_bootstrap.bin`](../../tempfiles/betaflight_bootstrap.bin) (`44,892 B`) await bench testing.

### What Were the Good / Brilliant Parts
1. **Real Upstream + Real Platform Driver Integration in Host TDD**:
   - Linking unmodified upstream `crsf.c`, `rx.c`, `voltage.c`, and `battery.c` against real `serial_uart_aries.c`, `stub_drivers.c`, and `drivers/serial.c` (`aries_host_mmio.c`) verified real C register behavior and blocking polled-TX drainage (`1,347/1,347` runnable host tests across `97/110` targets).
2. **Empirically Hardened Silicon Verification & Full `ARIESV3` Closure**:
   - [`tempfiles/phase19.4.txt`](../../tempfiles/phase19.4.txt) verified all 21 diagnostic self-tests (`45,132 B`, `0` RVC, Test 18 `M4 = 110,000` cycles, `19 us` IMU burst, `VBAT_raw=0`, `390 <= 420 us` peak, heartbeat & CLI), and [`tempfiles/phase19.5.txt`](../../tempfiles/phase19.5.txt) verified full `TARGET=ARIESV3` (`191,224 B` `.bin`, `222,208 B` SRAM, `MSP API: 1.49`, CLI `status`/`version`/`exit`, `System rate: 10`, clean warm reboot).
