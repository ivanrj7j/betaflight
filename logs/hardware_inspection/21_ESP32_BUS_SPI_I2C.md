# Hardware Inspection Log: `src/platform/ESP32/bus_spi_esp32.c` & `bus_i2c_esp32.c`
**Consulted Log**: [10_STM32_BUS_SPI_HW.md](10_STM32_BUS_SPI_HW.md)

## 1. File Purpose & Role in Architecture
* **File Paths**:
  * [src/platform/ESP32/bus_spi_esp32.c](../../src/platform/ESP32/bus_spi_esp32.c)
  * [src/platform/ESP32/bus_i2c_esp32.c](../../src/platform/ESP32/bus_i2c_esp32.c)
* **Subsystem**: SPI & I2C Serial Bus Abstraction Layer. Manages high-speed sensor buses (IMU / Gyro / Flash / OSD) and peripheral buses (ADC / Baro / Magnetometer).
* **Key Components**:
  * `spiInitDevice()` / `spiInitBusDMA()`: Configures peripheral clocks, resets hardware FIFOs, binds GDMA channels.
  * `spiSequenceStart()`: Intelligent decision engine: routes small transfers (<= 16 bytes) to 64-byte polled hardware FIFOs and large transfers to GDMA linked descriptors.
  * `i2cInit()` / `i2cWriteBuffer()` / `i2cReadBuffer()`: Hardware command-queue engine (`i2c_ll_hw_cmd_t`) for executing multi-byte I2C transactions.

---

## 2. Comparative Analysis: ESP32 vs. STM32 (`10_STM32_BUS_SPI_HW.md`)

| Feature / Hardware Challenge | STM32 Implementation (`10_STM32_BUS_SPI_HW.md`) | ESP32 Implementation (`bus_spi_esp32.c`, `bus_i2c_esp32.c`) |
| :--- | :--- | :--- |
| **Pin Routing / Interconnect** | Fixed Alternate Function (AF) multiplexing. Pins bound to specific peripheral instances. | Universal GPIO Matrix (`esp_rom_gpio_connect_out_signal` & `connect_in_signal`). Any GPIO can route to SPI2/3 and I2C0/1. |
| **DMA Architecture** | Direct Channel/Stream binding with hardware request multiplexer. Fixed circular/linear buffers. | Descriptor-based General DMA (`GDMA`) using linked-list structures (`lldesc_t` aligned to 4 bytes) with EOF flags. |
| **Memory Alignment & Bouncing** | ARM Cortex-M7 requires L1 D-Cache line invalidation (`SCB_InvalidateDCache_by_Addr`). Memory must be cache-line aligned. | GDMA requires 32-bit word alignment. If `rxBuffer` is unaligned (`rxBuffer & 3 != 0`), driver uses `dummyRxBuf` bounce buffer and memcpys in ISR. |
| **Small Transfer Optimization** | Polling used only if DMA is unavailable or during early boot / DShot conflict. | Explicit short-transfer cutoff (`ESP32S3_SPI_SHORT_POLLED_MAX_SIZE = 16`). GDMA setup overhead costs more CPU than polling the 64-byte FIFO for gyro bursts! |
| **I2C Architecture** | Event/Interrupt or DMA driven state machine monitoring SB, ADDR, TXE, RXNE flags. | Command-list Coprocessor: Software queues high-level opcodes (`I2C_LL_CMD_RESTART`, `WRITE`, `READ`, `STOP`) into hardware command registers, then triggers execution. |
| **Silicon Errata Mitigation** | Errata 2.1.10: Disables DMA on SPI buses sharing `DMA2` with DShot bitbang. | Memory barrier (`memw`) required before handing descriptor ownership bit (`owner = 1`) to GDMA hardware. |

---

## 3. Hardware Challenges ESP32 Developers Faced & How They Overcame Them

### Challenge 1: GDMA Word Alignment Constraint
* **The Problem**: Unlike the CPU which supports byte-level access, ESP32 GDMA engines require memory buffer addresses and descriptor payloads to be 32-bit (4-byte) aligned. If Betaflight passes an unaligned buffer (common in nested packed structs), GDMA generates bus transfer faults.
* **How It Was Overcome**:
  The driver dynamically checks alignment:
  ```c
  const bool rxNeedsBounce = hasRx && (((uintptr_t)rxBuffer & 3U) != 0);
  uint8_t *effectiveRxBuffer = hasRx && !rxNeedsBounce ? rxBuffer : dummyRxBuf[deviceNum];
  ```
  If unaligned, it directs GDMA into a pre-allocated word-aligned bounce buffer (`dummyRxBuf`), and performs a fast `memcpy` in `spiRxIrqHandler()` before invoking the segment callback.

### Challenge 2: GDMA Setup Overhead vs. High-Frequency Gyro Reads
* **The Problem**: Gyro sensor reads (ICM42688 / BMI270) are typically 8 to 15 bytes long, executed at 8 kHz (every 125 µs). Setting up GDMA descriptors, managing memory barriers, and handling two context switches for interrupts consumed *more* CPU cycles than polling.
* **How It Was Overcome**:
  ESP32 implemented a threshold in `spiSequenceStart()`:
  ```c
  #define ESP32S3_SPI_SHORT_POLLED_MAX_SIZE 16
  // Transactions <= 16 bytes use the hardware's 64-byte internal FIFO via polled mode
  ```
  This freed GDMA channels for Flash, OSD, and Blackbox logging where transfer sizes exceed several hundred bytes.

### Challenge 3: I2C State Machine Hangs & Bus Latching
* **The Problem**: I2C slaves (especially external compasses and barometers) frequently latch SDA low if the MCU resets during an active read transaction.
* **How It Was Overcome**:
  1. Implemented `i2cRecover()` which pulses SCL 9 times to release stuck slave state machines.
  2. Implemented hardware command sequencing where the entire sequence (START, ADDR, READ, STOP) is committed atomically.

---

## 4. Key Takeaways & Porting Blueprint for ARIES v3.0 (THEJAS32 / VEGA ET1031)

1. **Adopt Polled SPI for Minimal <128KB Firmware**:
   * For the minimal ARIES v3.0 port, adopting ESP32's 64-byte polled architecture approach is ideal. Polled SPI for reading gyro registers avoids needing complex DMA descriptor managers, saving ~4–6 KB of code and SRAM.
2. **I2C Driver for External ADC (U9) on ARIES v3.0**:
   * The ARIES v3.0 board routes analog inputs (Battery voltage, Current sensor) through an external 4-channel I2C ADC chip (`U9`) connected to `I2C2`.
   * The I2C driver on THEJAS32 can follow a clean polled write/read buffer flow (START -> ADDR -> REG -> RESTART -> READ -> STOP), exactly mirroring Betaflight's `i2cWriteBuffer()` and `i2cReadBuffer()`.
3. **No Cache Invalidation or Word Alignment Faults on VEGA ET1031**:
   * The VEGA RV32IM core in THEJAS32 does not use unaligned GDMA bounce buffers or L1 D-Cache invalidation registers, greatly simplifying the driver implementation compared to both STM32H7 and ESP32.
