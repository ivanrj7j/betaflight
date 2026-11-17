# Evaluation Node 12: Linker & Subsystem Stub Drivers (`stub_drivers.c`)

## 1. Executive Summary & File Traceability
- **Evaluated File**: [`src/platform/ARIES/stub_drivers.c`](../../src/platform/ARIES/stub_drivers.c)
- **Source Log**: [`logs/code_critique/12_STUB_DRIVERS_C.md`](../../logs/code_critique/12_STUB_DRIVERS_C.md)
- **Upstream References**:
  - [`src/main/drivers/dma.h`](../../src/main/drivers/dma.h)
  - [`src/main/drivers/adc.h`](../../src/main/drivers/adc.h)
  - [`src/main/target/ARIESV3/target.h`](../../src/main/target/ARIESV3/target.h)
- **Domain**: Unresolved GCC symbols, atomic built-ins on RV32IM, DMA stubs, ADC voltage fallback, soft reboot persistence.

---

## 2. Decision & Action Scoring Rubric (0 to 10)

| Decision / Action | Score (0-10) | Evaluation Rationale |
| :--- | :---: | :--- |
| **Implementing `__atomic_fetch_add_4` for RV32IM** | 9/10 | Essential fix for toolchain compatibility. VEGA ET1031 lacks the RISC-V Atomic ('A') extension (`lr.w`/`sc.w`); providing this symbol prevents unresolved libatomic link errors. |
| **Atomic Fallback State-Preservation Oversight** | 4/10 | Unconditionally called `__enable_irq()` on exit, which unmasks interrupts if called from inside an active interrupt service routine. |
| **Flat DMA Interface Stubs** | 8/10 | Safely satisfies Betaflight DMA call-sites without dragging in complex DMA engine code on silicon that has no DMA channels. |
| **ADC Stub Returning 0V** | 3/10 | Returning raw value `0` causes Betaflight battery telemetry to calculate 0.0V, permanently locking arming under `ARMING_DISABLED_BATTERY` unless `USE_ADC` is strictly removed. |
| **RAM Persistent Objects Array** | 8/10 | Preserves soft reboot codes across warm restarts in internal SRAM. |

---

## 3. Deep-Dive Critique

### What Went Wrong
1. **Interrupt Nesting Clobbered by Atomic Emulation**:
   - In [`src/platform/ARIES/stub_drivers.c`](../../src/platform/ARIES/stub_drivers.c#L35-L42):
     ```c
     unsigned int __atomic_fetch_add_4(volatile void *ptr, unsigned int val, int memorder) {
         __disable_irq();
         unsigned int old = *(volatile unsigned int *)ptr;
         *(volatile unsigned int *)ptr = old + val;
         __enable_irq();
         return old;
     }
     ```
   - If `__atomic_fetch_add_4` is called inside an ISR (e.g. updating a packet counter in a UART or EXTI interrupt), `__enable_irq()` sets `mstatus.MIE = 1`. This re-enables global interrupts while the ISR is still executing, breaking interrupt nesting guarantees.
2. **Battery Voltage Detection Lockout**:
   - `adcGetValue()` returns `0`.
   - Betaflight core logic monitors ADC readings to calculate battery state. A 0V reading triggers immediate critical battery alarms and disallows motor arming.

### What Could Have Been Done Better
1. **State-Preserving MSTATUS Atomic Sequence**:
   - Instead of unconditional disable/enable, capture and restore the previous `MIE` bit:
     ```c
     uint32_t mstatus_val;
     __asm__ volatile ("csrrci %0, mstatus, 8" : "=r"(mstatus_val));
     unsigned int old = *(volatile unsigned int *)ptr;
     *(volatile unsigned int *)ptr = old + val;
     if (mstatus_val & 8) {
         __asm__ volatile ("csrs mstatus, 8");
     }
     ```
2. **Explicit Undefinition of `USE_ADC`**:
   - Ensuring `target.h` explicitly defines `#undef USE_ADC` ensures the flight controller knows no battery ADC hardware is wired, bypassing voltage failsafe lockout completely.

### What Were the Good / Brilliant Parts
1. **Surgical Linker Symbol Resolution**:
   - Betaflight's build system assumes STM32 HAL peripherals with DMA, multiple timer channels, and hardware atomic extensions.
   - `stub_drivers.c` acts as a complete isolation shim that allows upstream Betaflight subsystems to link without modifying dozens of upstream driver files.
2. **Deterministic RV32IM Software Atomics**:
   - Emulating atomics via fast CSR manipulation takes only ~6 clock cycles, providing fully thread-safe atomics on RV32IM without heavy external libraries.
