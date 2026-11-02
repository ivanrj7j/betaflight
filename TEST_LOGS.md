# Test Execution Logs: C-DAC ARIES v3.0 (THEJAS32 RISC-V)

This document tracks all unit, integration, and regression test execution results for the Betaflight port on the C-DAC ARIES v3.0 platform.
Per testing protocols, if any test reveals an issue in the main platform code, the issue is documented here as a finding/failure without altering the main codebase during test execution.

---

## Summary of Phases

| Phase | Test Suite | Description | Total Tests | Passed | Failed | Status |
|:---:|:---|:---|:---:|:---:|:---:|:---:|
| **Phase 1** | `aries_infra_unittest` | Modular Test Infrastructure (MMIO, CSR, IRQ, Helpers) | 7 | 7 | 0 | **PASSED** |
| **Phase 2** | `aries_system_unittest` | System Core, Timebase, Conversions & Trap Dispatch | 23 | 23 | 0 | **PASSED** |
| **Phase 3** | `aries_uart_unittest` & `aries_spi_unittest` | Communication Drivers (UART 16550 & SPI Master) | - | - | - | Pending |
| **Phase 4** | `aries_gpio_unittest` & `aries_exti_unittest` | Address-Decoded GPIO & EXTI Callback Routing | - | - | - | Pending |
| **Phase 5** | `aries_flash_unittest` | SPI3 Flash Config & PG Storage Streamer | - | - | - | Pending |
| **Phase 6** | `aries_motor_unittest` | Motor PWM Actuation & Timer 1 Pulse Scheduler | - | - | - | Pending |
| **Phase 7** | `build_verify.sh` | Clean Build & Soft-Float Disassembly Audit | - | - | - | Pending |
| **Phase 8** | `aries_integration_unittest` | End-to-End Safety & System Integration | - | - | - | Pending |

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
