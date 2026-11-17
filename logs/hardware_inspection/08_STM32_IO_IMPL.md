# Hardware Inspection Log: `src/platform/common/stm32/io_impl.c`

## 1. File Purpose & Role in Architecture
* **File Path**: `src/platform/common/stm32/io_impl.c`
* **Subsystem**: Universal GPIO Pin Abstraction (`IO_t`), Pin Indexing, and Port Masking.
* **Key Components**:
  * `IOInitGlobal()`: Initializes dynamic pin record table `ioRecs` from compile-time bitmasks.
  * `IOGetByTag()`: Fast $O(1)$ lookup mapping a compact `ioTag_t` (e.g. `DEFIO_TAG_E(PA10)`) to its active `ioRec_t` descriptor using population count.
  * `IO_GPIOPortIdx()` / `IO_GPIOPinIdx()`: Hardware arithmetic converting pointers to numeric port/pin indices.

---

## 2. Hardware Challenges STM32 Developers Faced & How They Overcame Them

### Challenge 1: Memory Bloat from Multi-Port Pin Tables
* **The Problem**: STM32 chips have multiple 16-bit GPIO ports (GPIOA through GPIOI, representing up to 144 pins). If an `ioRec_t` structure (8–12 bytes) were allocated for all potential pins across all ports, hundreds of bytes of SRAM would be wasted on pins that do not physically exist on smaller packages (e.g. QFN32 or LQFP48).
* **How It Was Overcome**:
  1. The build system generates compact 16-bit bitmasks (`ioDefUsedMask[port]`) representing only pins actually present in the specific MCU target package.
  2. In `IOGetByTag()`, hardware population count (`popcount`) calculates the exact array index without sparse array gaps:
     ```c
     int offset = popcount(((1 << pinIdx) - 1) & ioDefUsedMask[portIdx]);
     offset += ioDefUsedOffset[portIdx];
     return ioRecs + offset;
     ```
  This compressed table eliminated hundreds of wasted bytes in SRAM.

---

### Challenge 2: Fast Port and Pin Arithmetic Without Division
* **The Problem**: Determining the pin index ($0..15$) from a one-hot bitmask (`0x0004` $\rightarrow$ Pin 2) or port index from base pointers (`0x40020000` $\rightarrow$ Port A) using division or branching is slow inside high-speed driver routines.
* **How It Was Overcome**:
  1. **Port Index**: Exploited the architectural fact that STM32 GPIO register blocks are spaced exactly 1024 bytes (`0x400`) apart:
     ```c
     return (((size_t)IO_GPIO(io) - GPIOA_BASE) >> 10);
     ```
  2. **Pin Index**: Used the Count Leading Zeros compiler intrinsic:
     ```c
     return 31 - __builtin_clz(IO_Pin(io));
     ```

---

## 3. Direct Porting Blueprint for ARIES v3.0 (THEJAS32 / VEGA ET1031)

1. **Flat 32-Bit GPIO Architecture**:
   * STM32 separates pins into multi-letter 16-pin ports (`PA0..PA15`, `PB0..PB15`, `PC0..PC15`).
   * In contrast, the THEJAS32 SoC on ARIES v3.0 implements a **flat single 32-bit GPIO controller (`GPIO0` to `GPIO31`)**.
2. **Simplified `ioTag_t` Implementation**:
   * On ARIES v3.0, port indexing is unnecessary:
     ```c
     typedef uint8_t ioTag_t; // Simply pin number 0 to 31
     #define IO_TAG(pin) (pin)
     ```
   * An array of 32 `ioRec_t` structs takes only $32 \times 8 = 256$ bytes of SRAM.
   * `popcount` and sparse port masking can be replaced with a direct array index `ioRecs[pinIdx]`, saving execution time and binary size!
