# Test Execution Logs: C-DAC ARIES v3.0 (THEJAS32 RISC-V)

This document tracks all unit, integration, and regression test execution results for the Betaflight port on the C-DAC ARIES v3.0 platform.
Per testing protocols, if any test reveals an issue in the main platform code, the issue is documented here as a finding/failure without altering the main codebase during test execution.

---

## Summary of Phases

| Phase | Test Suite | Description | Total Tests | Passed | Failed | Status |
|:---:|:---|:---|:---:|:---:|:---:|:---:|
| **Phase 1** | `aries_infra_unittest` | Modular Test Infrastructure (MMIO, CSR, IRQ, Helpers) | 7 | 7 | 0 | **PASSED** |
| **Phase 2** | `aries_system_unittest` | System Core, Timebase, Conversions & Trap Dispatch | 23 | 23 | 0 | **PASSED** |
| **Phase 3** | `aries_uart_unittest` & `aries_spi_unittest` | Communication Drivers (UART 16550 & SPI Master) | 26 | 26 | 0 | **PASSED** |
| **Phase 4** | `aries_gpio_unittest` & `aries_exti_unittest` | Address-Decoded GPIO & EXTI Callback Routing | - | - | - | Pending |
| **Phase 5** | `aries_flash_unittest` | SPI3 Flash Config & PG Storage Streamer | - | - | - | Pending |
| **Phase 6** | `aries_motor_unittest` | Motor PWM Actuation & Timer 1 Pulse Scheduler | - | - | - | Pending |
| **Phase 7** | `build_verify.sh` | Clean Build & Soft-Float Disassembly Audit | - | - | - | Pending |
| **Phase 8** | `aries_integration_unittest` | End-to-End Safety & System Integration | - | - | - | Pending |

**Cumulative Summary:** 56 tests executed across Phases 1–3, 56 PASSED, 0 FAILED (100% Success Rate).

---

## Phase 1 Detailed Results: Test Infrastructure Bootstrap

- **Test Suite:** `aries_infra_unittest`
- **Source Target:** `src/test/unit/aries/` (`mock_mmio`, `mock_csr`, `mock_thejas32`, `test_helpers`)
- **Test File:** [`src/test/unit/aries_infra_unittest.cc`](src/test/unit/aries_infra_unittest.cc)
- **Execution Date:** 2026-11-02
- **Command:** `make test_aries_infra_unittest`

### Test Cases Table

| Test Case ID | Test Name | Target Module | Result | Details / Observations |
|:---|:---|:---|:---:|:---|
| `INFRA-01` | `AriesInfraTest.MockMmioResetClearsMemoryRegions` | `mock_mmio` | **PASS** | Peripheral, GPIO0, GPIO1, IntCtrl memory reset to 0x00; Flash default set to 0xFF. |
| `INFRA-02` | `AriesInfraTest.MockMmioPatternReset` | `mock_mmio` | **PASS** | Arbitrary memory regions reliably wiped with test pattern (0x33). |
| `INFRA-03` | `AriesInfraTest.MockMmioAccessLogging` | `mock_mmio` | **PASS** | Accurate write/read tracking; query by address and write count filtering verified. |
| `INFRA-04` | `AriesInfraTest.MockCsrBasicOperations` | `mock_csr` | **PASS** | 64-bit cycle counter advance, set, reset, and 32-bit truncation verified. |
| `INFRA-05` | `AriesInfraTest.MockThejas32InterruptManagement` | `mock_thejas32` | **PASS** | IRQ registration, enable mask tracking, and selective handler firing verified. |
| `INFRA-06` | `AriesInfraTest.TestHelpersSequentialAndCanary` | `test_helpers` | **PASS** | Sequential buffer filling/verification and 0xA5A5A5A5 canary checking verified. |
| `INFRA-07` | `AriesInfraTest.TestHelpersAddressFormulas` | `test_helpers` | **PASS** | Exact match on GPIO bit-shift addresses (Bank 0, Bank 1 LD4) and 100 MHz baud divisor (54 for 115200). |

**Phase 1 Result:** 7/7 PASSED (100% Success Rate).

---

## Phase 2 Detailed Results: System Core & Timebase Tests

- **Test Suite:** `aries_system_unittest`
- **Source Target:** [`src/platform/ARIES/system.c`](src/platform/ARIES/system.c) via [`src/test/unit/aries_system_testable.c`](src/test/unit/aries_system_testable.c)
- **Test File:** [`src/test/unit/aries_system_unittest.cc`](src/test/unit/aries_system_unittest.cc)
- **Execution Date:** 2026-11-02
- **Command:** `make test_aries_system_unittest`

### Test Cases Table

| Test Case ID | Test Name | Function Under Test | Result | Details / Observations |
|:---|:---|:---|:---:|:---|
| `SYS-01` | `AriesSystemTest.MicrosReturnsZeroAtCycleZero` | `micros()` | **PASS** | Returns 0 at cycle counter 0; `microsISR()` identical. |
| `SYS-02` | `AriesSystemTest.MicrosReturns1At100Cycles` | `micros()` | **PASS** | Scale factor: 100 clock cycles at 100 MHz = 1 µs. |
| `SYS-03` | `AriesSystemTest.MicrosRolls32BitCorrectly` | `micros()` | **PASS** | Unsigned 32-bit arithmetic wraps cleanly at 4,294,967,295 µs without underflow artifacts. |
| `SYS-04` | `AriesSystemTest.MicrosIsMonotonic` | `micros()` | **PASS** | Monotonic across 10,000 advancing cycle reads ($\text{now} \ge \text{prev}$). |
| `SYS-05` | `AriesSystemTest.MillisScaling` | `millis()` | **PASS** | 100,000 cycles per millisecond; 500 million cycles evaluated to exactly 5000 ms. |
| `SYS-06` | `AriesSystemTest.MillisRolloverWindow` | `millis()` | **PASS** | Aligned with standard 49.71-day rollover window ($2^{32}-1$ ms). |
| `SYS-07` | `AriesSystemTest.ClockCyclesToMicrosConversion` | `clockCyclesToMicros()` | **PASS** | Correct integer division by `usTicks = 100` with truncation below 1 µs. |
| `SYS-08` | `AriesSystemTest.ClockCyclesToMicrosFloat` | `clockCyclesToMicrosf()` | **PASS** | Single-precision floating point conversion via reciprocal multiplication ($0.01\text{f}$). |
| `SYS-09` | `AriesSystemTest.ClockMicrosToCyclesConversion` | `clockMicrosToCycles()` | **PASS** | Microseconds multiplied by 100. |
| `SYS-10` | `AriesSystemTest.ClockCyclesToTenthAndHundredthMicros` | `clockCyclesTo10thMicros` | **PASS** | 0.1 µs and 0.01 µs fractional conversions match expected cycle scaling. |
| `SYS-11` | `AriesSystemTest.TimerInitConfiguresRegistersAndIRQ` | `thejas32_timer_init()` | **PASS** | `LoadCount = 0xFFFFFFFF`, `Control = 0x03` (user-defined periodic, unmasked, enabled), IRQ 7 enabled in `INTR_EN`. |
| `SYS-12` | `AriesSystemTest.Timer0IsrIncrementsOverflowsAndAcknowledgesEOI` | `thejas32_timer0_isr()` | **PASS** | Increments `timer0_overflows` counter and accesses `EOI` register. |
| `SYS-13` | `AriesSystemTest.Timer0GettersReflectHardwareState` | `timer0GetCurrentValue()` | **PASS** | Direct readout of down-counter register `0x10000A04`. |
| `SYS-14` | `AriesSystemTest.Timer0MicrosCalculatesTimeFromHardwareRegisters` | `timer0Micros()` | **PASS** | Accurate elapsed time calculation from down-counter offset divided by 100. |
| `SYS-15` | `AriesSystemTest.Timer0MicrosCorrectsPendingOverflow` | `timer0Micros()` | **PASS** | Corrects for unserviced overflow when `IntrStatus & 1` and counter has wrapped. |
| `SYS-16` | `AriesSystemTest.InterruptRegisterSlotPopulationAndBoundsGuard` | `thejas32_interrupt_register()` | **PASS** | Registers slots 0–31; bounds check discards IRQ indices $\ge 32$ without memory corruption. |
| `SYS-17` | `AriesSystemTest.InterruptEnableAndDisableBits` | `thejas32_interrupt_enable/disable()` | **PASS** | Direct bit-masking on `INTR_CTRL.INTR_EN` register (`0x20010008`). |
| `SYS-18` | `AriesSystemTest.HandleTrapDispatchesExternalInterrupt` | `handle_trap()` | **PASS** | External IRQ (cause 11) queries `INTR_STATUS`, invokes registered handler, returns `mepc` unchanged. |
| `SYS-19` | `AriesSystemTest.HandleTrapMultipleActiveInterrupts` | `handle_trap()` | **PASS** | Multiple active bits in `INTR_STATUS` (IRQ 0 and IRQ 1) dispatched systematically in single trap entry. |
| `SYS-20` | `AriesSystemTest.HandleTrapUnregisteredIrqNoCrash` | `handle_trap()` | **PASS** | Active bit for unregistered IRQ slot does not trigger null pointer invocation. |
| `SYS-21` | `AriesSystemTest.HandleTrapSynchronousIllegalInstructionException` | `handle_trap()` | **PASS** | Synchronous exception (`mcause = 2`) triggers `handle_trap_panic()` and returns `mepc + 4`. |
| `SYS-22` | `AriesSystemTest.HandleTrapSynchronousLoadFaultException` | `handle_trap()` | **PASS** | Synchronous load access fault (`mcause = 5`) triggers `handle_trap_panic()`. |
| `SYS-23` | `AriesSystemTest.CycleCounterInit100MHzConfiguration` | `cycleCounterInit()` | **PASS** | Calculates `usTicks = 100` and `usTicksInv = 0.01f` from `SystemCoreClock = 100000000`. |

**Phase 2 Result:** 23/23 PASSED (100% Success Rate).  
**Failures / Discrepancies in Main Code Found:** None.

---

## Phase 3 Detailed Results: Communication Tests (UART 16550 & SPI Master)

### Part A: UART 16550 Serial Driver

- **Test Suite:** `aries_uart_unittest`
- **Source Target:** [`src/platform/ARIES/serial_uart_aries.c`](src/platform/ARIES/serial_uart_aries.c) via [`src/test/unit/aries_uart_testable.c`](src/test/unit/aries_uart_testable.c)
- **Test File:** [`src/test/unit/aries_uart_unittest.cc`](src/test/unit/aries_uart_unittest.cc)
- **Execution Date:** 2026-11-02
- **Command:** `make test_aries_uart_unittest`

| Test Case ID | Test Name | Function Under Test | Result | Details / Observations |
|:---|:---|:---|:---:|:---|
| `UART-01` | `AriesUartTest.BaudDivisorFor115200` | `thejas32_uart_set_baudrate` | **PASS** | Divisor 54 (`0x36`) programmed via DLAB=1, LCR restored to 8N1 (`0x03`). |
| `UART-02` | `AriesUartTest.BaudDivisorFor9600` | `thejas32_uart_set_baudrate` | **PASS** | Divisor 651 (`0x028B`) verified for 9600 baud at 100 MHz APB clock. |
| `UART-03` | `AriesUartTest.BaudRateZeroDefaultsTo115200` | `thejas32_uart_set_baudrate` | **PASS** | Zero baud rate safely defaults to 115200 without divide-by-zero exception. |
| `UART-04` | `AriesUartTest.LineControlDefault8N1` | `uartReconfigure` | **PASS** | Default line config evaluates to 8 data bits, 1 stop bit, no parity (`0x03`). |
| `UART-05` | `AriesUartTest.FcrEnablesFifos` | `thejas32_uart_set_baudrate` | **PASS** | `FCR = 0x07` enables FIFO mode and resets both TX and RX FIFOs. |
| `UART-06` | `AriesUartTest.RxIngestionIntoBuffer` | `uartIrqHandler` (RX) | **PASS** | Incoming byte read from `UART_DR` and stored in RX ring buffer at head index. |
| `UART-07` | `AriesUartTest.RxRingBufferWraparound` | `uartIrqHandler` (RX) | **PASS** | Head pointer wraps smoothly from 255 to 0 without out-of-bounds corruption. |
| `UART-08` | `AriesUartTest.Sequential300ByteStreamWraparoundOrder` | Ring Buffer Mechanics | **PASS** | 300 sequential bytes streamed through 256-byte buffer preserve 100% order fidelity. |
| `UART-09` | `AriesUartTest.TxDrainPushesBytesInOrder` | `uartIrqHandler` (TX) | **PASS** | TX FIFO drains characters sequentially into `UART_DR` while THRE is asserted. |
| `UART-10` | `AriesUartTest.TxInterruptDisabledOnEmptyQueue` | `uartIrqHandler` (TX) | **PASS** | `UART_IE` bit 1 (THRE interrupt) de-asserted when `txBufferTail == txBufferHead`. |
| `UART-11` | `AriesUartTest.TransmitFifoPrimingWhenTransmitterEmpty` | `uartEnableTxInterrupt` | **PASS** | Idle transmitter primed immediately with first byte, preventing TX queue deadlock. |
| `UART-12` | `AriesUartTest.ChannelIndexLookup` | `uartGetIndex` | **PASS** | Address `0x10000100` maps to index 0 (UART0); `0x10000200` maps to index 1 (UART1). |
| `UART-13` | `AriesUartTest.RxCallbackInvokedWhenSet` | `uartIrqHandler` (Callback) | **PASS** | Directly routes incoming character to registered callback function instead of buffer. |
| `UART-14` | `AriesUartTest.ParityAndStopBitsConfiguration` | `uartReconfigure` | **PASS** | `SERIAL_STOPBITS_2` and `SERIAL_PARITY_EVEN` set LCR to `0x1F`. |

**Part A Result:** 14/14 PASSED.

---

### Part B: SPI Master Bus Driver

- **Test Suite:** `aries_spi_unittest`
- **Source Target:** [`src/platform/ARIES/bus_spi_aries.c`](src/platform/ARIES/bus_spi_aries.c) via [`src/test/unit/aries_spi_testable.c`](src/test/unit/aries_spi_testable.c)
- **Test File:** [`src/test/unit/aries_spi_unittest.cc`](src/test/unit/aries_spi_unittest.cc)
- **Execution Date:** 2026-11-02
- **Command:** `make test_aries_spi_unittest`

| Test Case ID | Test Name | Function Under Test | Result | Details / Observations |
|:---|:---|:---|:---:|:---|
| `SPI-01` | `AriesSpiTest.PrescalerDivFor25MHz` | `spiCalculateDivider` | **PASS** | Frequencies $\ge 20\text{ MHz}$ select prescaler 0 (div /4 = 25 MHz). |
| `SPI-02` | `AriesSpiTest.PrescalerDivFor12MHz` | `spiCalculateDivider` | **PASS** | Frequencies $\ge 10\text{ MHz}$ select prescaler 1 (div /8 = 12.5 MHz). |
| `SPI-03` | `AriesSpiTest.PrescalerDivFor6MHz` | `spiCalculateDivider` | **PASS** | Frequencies $\ge 5\text{ MHz}$ select prescaler 2 (div /16 = 6.25 MHz). |
| `SPI-04` | `AriesSpiTest.PrescalerDivForSlowClock` | `spiCalculateDivider` | **PASS** | Frequencies $< 300\text{ kHz}$ select prescaler 7 (div /512 = 195 kHz). |
| `SPI-05` | `AriesSpiTest.InverseClockForDivisor0` | `spiCalculateClock` | **PASS** | Divisor 0 maps to 25,000,000 Hz. |
| `SPI-06` | `AriesSpiTest.InverseClockForDivisor1` | `spiCalculateClock` | **PASS** | Divisor 1 maps to 12,500,000 Hz. |
| `SPI-07` | `AriesSpiTest.InverseClockForDivisor7` | `spiCalculateClock` | **PASS** | Divisor 7 maps to 195,312 Hz. |
| `SPI-08` | `AriesSpiTest.InverseClockDefaultFallback` | `spiCalculateClock` | **PASS** | Invalid divisor codes default safely to 12,500,000 Hz. |
| `SPI-09` | `AriesSpiTest.HardwareInitRegisters` | `spiInitDevice` | **PASS** | Initializes Baudrate register to `0x10` and Control word to `0x0000` (Mode 0). |
| `SPI-10` | `AriesSpiTest.PolledTransferBoundedTimeoutOnStuckLines` | `thejas32_spi_transfer_byte` | **PASS** | 100,000-cycle bounded wait loop safely aborts and returns 0 without infinite hang. |
| `SPI-11` | `AriesSpiTest.PolledBufferTransfer` | `spiInternalReadWriteBufPolled` | **PASS** | Full 2-byte burst read correctly returns sensor WHO_AM_I data (`0x47`); NULL handlers verified. |
| `SPI-12` | `AriesSpiTest.SequenceStartMode0AndMode3` | `spiSequenceStart` | **PASS** | `leadingEdge = true` selects Mode 0 (`0x0000`); `leadingEdge = false` selects Mode 3 (`0x0030`). |

**Part B Result:** 12/12 PASSED.  
**Phase 3 Total Result:** 26/26 PASSED (100% Success Rate).  
**Failures / Discrepancies in Main Code Found:** None.
