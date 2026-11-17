# Hardware Inspection Log: `src/platform/common/stm32/bus_spi_hw.c`

## 1. File Purpose & Role in Architecture
* **File Path**: `src/platform/common/stm32/bus_spi_hw.c`
* **Subsystem**: SPI Bus Driver Hardware Engine, DMA Transaction Completion Handlers, Dynamic Clock Prescaler Calculation, and Data Cache Management.
* **Key Components**:
  * `spiRxIrqHandler()` / `spiTxIrqHandler()`: DMA completion callbacks managing Chip Select (`CS`) negation and cache invalidation.
  * `spiCalculateDivider()`: Power-of-2 clock prescaler calculator.
  * `spiInitBusDMA()`: Allocates and validates DMA channels for each active SPI bus.

---

## 2. Hardware Challenges STM32 Developers Faced & How They Overcame Them

### Challenge 1: ARM Cortex-M7 L1 Data Cache Incoherency
* **The Problem**: On high-performance Cortex-M7 flight controllers (STM32H7), the CPU has an L1 Data Cache enabled. When SPI DMA writes incoming sensor data (e.g. gyro readings) directly into SRAM, the L1 D-Cache does not know the RAM changed. If CPU code reads the buffer, it reads stale cache lines, resulting in erratic gyro data and flyaways!
* **How It Was Overcome**:
  In `spiRxIrqHandler()`, right after DMA finishes, the driver calculates cache line alignment and forces a cache invalidation before handing data to the sensor layer:
  ```c
  #ifdef __DCACHE_PRESENT
  SCB_InvalidateDCache_by_Addr(
      (uint32_t *)((uint32_t)bus->curSegment->u.buffers.rxData & ~CACHE_LINE_MASK),
      (((uint32_t)bus->curSegment->u.buffers.rxData & CACHE_LINE_MASK) +
        bus->curSegment->len - 1 + CACHE_LINE_SIZE) & ~CACHE_LINE_MASK);
  #endif
  ```

---

### Challenge 2: STM32F4 Silicon Errata 2.1.10 (DMA2 Concurrent Access Corruption)
* **The Problem**: ST Errata Sheet `dm00037591` documents a hardware bug: memory corruption occurs on `DMA2` if AHB peripherals (GPIO ports) and APB peripherals (SPI buses) are accessed concurrently. Because DShot Bitbang uses `DMA2` to stream motor pulses to GPIO, running SPI DMA concurrently on `DMA2` (SPI1) caused gyro packet corruption or motor twitching.
* **How It Was Overcome**:
  The driver actively checks if DShot bitbang is enabled. If true, it refuses to bind DMA to any SPI bus that uses `DMA2`, transparently falling back to polled SPI transfer:
  ```c
  const bool dshotBitbangActive = isDshotBitbangActive(&motorConfig()->dev);
  if (dshotBitbangActive && (DMA_DEVICE_NO(dmaTxIdentifier) == 2)) {
      dmaTxIdentifier = DMA_NONE;
      break;
  }
  ```

---

### Challenge 3: Asynchronous Segment Chaining with Dynamic Chip Select Negation
* **The Problem**: Reading high-speed sensors (e.g. MPU6000 or ICM42688P) requires multi-stage transactions (write register address, then read $N$ bytes of accelerometer/gyro data). Toggling Chip Select in software between stages adds microsecond delays.
* **How It Was Overcome**:
  Betaflight defines transactions as arrays of `busSegment_t`. Each segment specifies whether Chip Select should remain asserted or be negated (`negateCS` flag). The DMA ISR automatically de-asserts CS (`IOHi(dev->busType_u.spi.csnPin)`) only when the final segment finishes.

---

## 3. Direct Porting Blueprint for ARIES v3.0 (THEJAS32 / VEGA ET1031)

1. **No Cache Incoherency Complications**:
   * The VEGA ET1031 core in THEJAS32 operates without an aggressive L1 cache that requires line-by-line invalidation. Data written into SRAM is immediately coherent and readable by the CPU.
2. **No Silicon Errata Between GPIO & SPI**:
   * The concurrent bus corruption bug specific to STM32 `DMA2` does not exist on THEJAS32.
3. **Polled vs Interrupt SPI for Initial Prototype**:
   * For the <128KB minimal target, SPI can use simple polled transmission:
     * Write byte to SPI TX data register.
     * Wait for SPI Status Register `TX_EMPTY` or `BUSY == 0`.
     * Read byte from SPI RX data register.
   * This eliminates the need for complex DMA channel mapping while still running at up to 10–20 MHz bus speeds for reading IMU and Flash!
