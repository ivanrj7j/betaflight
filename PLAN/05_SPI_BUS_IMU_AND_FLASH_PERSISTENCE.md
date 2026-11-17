# Domain Plan 05: SPI Bus, IMU & Flash Persistence

> **Component Plan ID**: `PLAN-DOM-05`  
> **Parent Architecture Document**: [`PLAN/00_MASTER_PLAN_OVERVIEW.md`](file:///home/ivan/Desktop/betaflight/PLAN/00_MASTER_PLAN_OVERVIEW.md)  
> **Primary Knowledge References**:
> - [`knowledgebase/codebase_evaluations/09_EVAL_BUS_SPI_ARIES_C.md`](file:///home/ivan/Desktop/betaflight/knowledgebase/codebase_evaluations/09_EVAL_BUS_SPI_ARIES_C.md)
> - [`knowledgebase/codebase_evaluations/10_EVAL_CONFIG_FLASH_ARIES_C.md`](file:///home/ivan/Desktop/betaflight/knowledgebase/codebase_evaluations/10_EVAL_CONFIG_FLASH_ARIES_C.md)
> - [`knowledgebase/components/07_BUS_SPI_AND_I2C.md`](file:///home/ivan/Desktop/betaflight/knowledgebase/components/07_BUS_SPI_AND_I2C.md)
> - [`knowledgebase/CRITICAL_KNOWLEDGE_GAPS_AND_ASSUMPTIONS.md`](file:///home/ivan/Desktop/betaflight/knowledgebase/CRITICAL_KNOWLEDGE_GAPS_AND_ASSUMPTIONS.md)
> - [`lib/vega-sdk/vega-sdk/examples/spi/spi_write_read_pgm/main.c`](file:///home/ivan/Desktop/betaflight/lib/vega-sdk/vega-sdk/examples/spi/spi_write_read_pgm/main.c)

---

## 1. Executive Domain Summary

The SPI Bus, Inertial Measurement Unit (IMU), and Non-Volatile Flash Persistence subsystem handles high-speed telemetry acquisition and configuration retention on C-DAC ARIES v3.0:
1. **SPI0 Master Controller (`bus_spi_aries.c` on Header J1)**:
   - Drives SPI0 on pins `PA5` (SCK), `PA6` (MISO), `PA7` (MOSI), `PA8` (CS).
   - Operates in polled mode without DMA, saving $\approx 6\text{ KB}$ of driver overhead to comply with the 248 KB SRAM budget.
   - **Clocks Clamped to 12.5 MHz (Divisor 1)**: Overcomes previous overclocking flaw that pushed SCK to 25.0 MHz (exceeding ICM-42688P's 24.0 MHz absolute rating). A 14-byte raw gyro burst takes only $8.96\,\mu\text{s}$ at 12.5 MHz ($< 1\%$ of 1.0 kHz flight cycle).
2. **IMU Synchronization & Alignment Guardrails**:
   - Synchronizes raw telemetry reads to the 1.0 kHz EXTI rising-edge interrupt on `PA0` (DRDY).
   - Isolates sensor orientation (`CW0`, `CW90`, `CW180`, `CW270`, `CUSTOM`) to avoid dangerous flyaway behaviors from unverified laboratory breakout mountings.
3. **The Non-XIP Flash Reality & Dual Persistence Architecture**:
   - **Ground Truth**: Physical THEJAS32 silicon lacks hardware memory-mapped XIP flash at `0x20000000`. Raw pointer dereferencing triggers fatal Store Access Faults (`mcause = 7`).
   - **Operational Mode 1 (`CONFIG_IN_RAM`)**: Stores Parameter Groups inside an 8 KB SRAM buffer (`EEPROM_SIZE 8192`), providing 100% stable parameter persistence across warm soft-reboots.
   - **Operational Mode 2 (Cold-Reboot JEDEC Command Driver)**: Implements true cold-boot non-volatile persistence via C-DAC's proven `MDP_SPI_3` (`0x10200100`) command engine (`0x03` Read, `0x02` Page Program, `0x20` 4KB Sector Erase, and `CSAAT` pin control), eliminating unmapped memory traps.

---

## 2. Microcomponent Decomposition

```
PLAN/05_SPI_BUS_IMU_AND_FLASH_PERSISTENCE.md
 ├── Microcomponent 05_A: Polled SPI0 Master Controller & Conservative 12.5 MHz Clock Engine (bus_spi_aries.c)
 ├── Microcomponent 05_B: Segmented Bidirectional Bus Engine & Bus-Timeout Error Recovery
 ├── Microcomponent 05_C: ICM-42688P High-Rate Sensor Synchronization & Header J1 Pinout Binding
 ├── Microcomponent 05_D: In-SRAM Configuration Architecture (CONFIG_IN_RAM & EEPROM_SIZE 8192)
 └── Microcomponent 05_E: JEDEC Command-Based Cold-Reboot Flash Persistence Engine (MDP_SPI_3)
```

---

### Microcomponent 05_A: Polled SPI0 Master Controller & Conservative 12.5 MHz Clock Engine (bus_spi_aries.c)

#### 1. Why (Silicon Necessity & Architectural Traceability)
* **The 25 MHz Overclocking Flaw**: In earlier code ([`src/platform/ARIES/bus_spi_aries.c#L74-L85`](file:///home/ivan/Desktop/betaflight/src/platform/ARIES/bus_spi_aries.c#L74-L85)), setting the SPI clock to $\ge 20\,\text{MHz}$ selected divisor 0, producing a 25.0 MHz SCK on the 100 MHz APB bus. The ICM-42688P specifies an absolute maximum clock rate of 24.0 MHz. Running at 25 MHz degrades signal integrity, inducing SPI CRC corruptions and telemetry glitches.
* **Polled Mode Determinism**: The VEGA ET1031 has no general-purpose DMA engine connected to SPI0. Attempting to simulate DMA via interrupts increases context switching latency. Polled register access completes a 14-byte gyro burst in $8.96\,\mu\text{s}$, consuming $< 0.9\%$ of the 1.0 kHz ($1000\,\mu\text{s}$) flight cycle while avoiding complex DMA alignment bounce buffers.

#### 2. How (Detailed Algorithmic & Register Execution)
1. **Peripheral Base Address & Pinout**:
   - `SPI0_BASE`: `0x10001000` (Header J1)
   - Pins: `PA5` (SCK), `PA6` (MISO), `PA7` (MOSI), `PA8` (CS)
   - Registers:
     - `+0x00`: `Control` (Bit 0: Enable, Bit 4: CPHA, Bit 5: CPOL, Bits 15-8: Baud Divisor)
     - `+0x04`: `Status` (Bit 0: Busy, Bit 1: RX Done, Bit 2: TX Empty)
     - `+0x08`: `TxData`
     - `+0x0C`: `RxData`
2. **Shift-by-4 Baud Divisor Architecture**:
   - C-DAC IP requires the baud divisor to be shifted:
     $$\text{Baudrate Register} = (bBaudCFD \ll 4)$$
   - Divisor 1 produces:
     $$f_{\text{SCK}} = \frac{100\,\text{MHz}}{8} = 12.5\,\text{MHz}$$
   - Clamp logic in `thejas32_spi_find_divisor()`:
     ```c
     // Enforce strict 12.5 MHz maximum ceiling to protect ICM-42688P (24.0 MHz max rating)
     if (freq >= 12500000) {
         return 1; // 12.5 MHz
     } else if (freq >= 6250000) {
         return 2; // 6.25 MHz
     } else {
         return 3; // 3.125 MHz (Slow safe mode for JEDEC ID reads)
     }
     ```
3. **SPI Clock Polarity & Phase Configuration**:
   - Mode 0 (CPOL=0, CPHA=0): Default for ICM-42688P and SPI flash.
   - Mode 3 (CPOL=1, CPHA=1): Supported via bitwise OR on `Control` bits 5 and 4 based on Betaflight device descriptor `leadingEdge`.

#### 3. Expected Inputs
* **Clock Frequencies**: Fast (12.5 MHz) or Standard/Slow (3.125 MHz).
* **Device Instances**: `SPIDEV_1` (Header J1 SPI0).

#### 4. Expected Results
* SCK strictly clamped to 12.5 MHz with 0% chance of exceeding 24 MHz.
* Clean 14-byte sensor bursts executed in $< 9\,\mu\text{s}$.
* Zero DMA complexity; compact code footprint.

---

### Microcomponent 05_B: Segmented Bidirectional Bus Engine & Bus-Timeout Error Recovery

#### 1. Why (Silicon Necessity & Architectural Traceability)
* **Silent Failure Trap**: Earlier code returned raw registers upon timeout without resetting the controller or clearing internal shift flags. If an SPI slave (e.g. disconnected breakout) held lines floating, all future transactions failed silently.
* **Segmented Transfers (`busSegment_t`)**: Modern Betaflight drivers chain multiple memory buffers in a single CS assertion (e.g., sending 1 register address byte, followed by reading $N$ payload bytes). The driver must cleanly support `txData == NULL` (read-only) and `rxData == NULL` (write-only).

#### 2. How (Detailed Algorithmic & Timeout Recovery Implementation)
1. **Single-Byte Polled Transfer Engine with Timeout Bounds**:
   ```c
   uint8_t thejas32_spi_transfer_byte(thejas32_spi_t *spi, uint8_t out) {
       uint32_t timeout = 50000;
       
       // Wait until TX register is empty
       while ((spi->Status & THEJAS32_SPI_STATUS_BUSY) && --timeout);
       if (!timeout) {
           thejas32_spi_reset_controller(spi);
           return 0xFF;
       }
       
       spi->TxData = out;
       
       timeout = 50000;
       // Wait until RX byte is latched
       while (!(spi->Status & THEJAS32_SPI_STATUS_RX_DONE) && --timeout);
       if (!timeout) {
           thejas32_spi_reset_controller(spi);
           return 0xFF;
       }
       
       return (uint8_t)(spi->RxData & 0xFF);
   }
   ```
2. **Controller Auto-Recovery Function**:
   - If `timeout == 0`:
     - Flush `RxData` dummy read: `volatile uint32_t dummy = spi->RxData; (void)dummy;`
     - Disable and re-enable SPI controller bit 0 in `Control` register to clear internal shift registers.
     - Increment error counter: `spiBusErrorsCount++`.
3. **Multi-Segment Chaining Support (`spiTransfer`)**:
   - Accept pointers `const uint8_t *txData` and `uint8_t *rxData`.
   - If `txData == NULL`, transmit dummy byte `0xFF`.
   - If `rxData == NULL`, discard incoming byte.
   - Return `true` on success, `false` on bus timeout.

#### 3. Expected Inputs
* **Buffers**: `txData`, `rxData`, `length`.
* **Hardware State**: `spi->Status` register.

#### 4. Expected Results
* 100% robust transmission with zero deadlocks on disconnected sensor headers.
* Automatic recovery of SPI controller shift registers upon timeout.
* Seamless handling of null transmit/receive segments.

---

### Microcomponent 05_C: ICM-42688P High-Rate Sensor Synchronization & Header J1 Pinout Binding

#### 1. Why (Silicon Necessity & Architectural Traceability)
* **Real-Time Gyro Updates**: The flight control loop depends on fresh angular rates sampled at 1.0 kHz. Polling without synchronization causes aliasing and jitter.
* **Sensor Pinout Binding**:
  - `PA5`: SPI0 SCK
  - `PA6`: SPI0 MISO
  - `PA7`: SPI0 MOSI
  - `PA8`: SPI0 CS
  - `PA0`: Gyro DRDY Interrupt
* **Anti-Assumption Guardrail (Sensor Orientation)**: Breakout boards manufactured by different vendors place the ICM-42688P chip in varying physical rotations relative to the flight controller board. Hardcoding the orientation matrix can cause inverted control responses and catastrophic flip-overs on arming.

#### 2. How (Detailed Algorithmic & Sensor Integration)
1. **Device Initialization Sequence**:
   - Assert CS LOW (`IOLo(spi0_cs)`).
   - Read `WHO_AM_I` register (`0x75`): Expect `0x47` for ICM-42688P.
   - Negate CS HIGH (`IOHi(spi0_cs)`).
   - Configure Gyro & Accel ODR (Output Data Rate) to 1.0 kHz (matching `DEFAULT_PID_PROCESS_DENOM 8`).
   - Configure low-pass filters (LPF) to 258 Hz bandwidth.
   - Enable Data Ready Interrupt on `INT1` pin (wired to `PA0`).
2. **Interrupt-Driven Gyro Task Triggering**:
   - Connect `PA0` to `EXTIChannel 0`.
   - On `thejas32_gpio0_isr` firing, signal `TASK_GYROPID` in the Betaflight scheduler.
   - The scheduler executes `gyroUpdate()`:
     - Asserts CS.
     - Transfers 1 register address (`0x1F` - `ACCEL_DATA_X1`) followed by 14 consecutive bytes (3-axis Accel, 3-axis Gyro, 1-axis Temperature).
     - Negates CS.
3. **Sensor Alignment Configuration**:
   - Expose orientation configuration in `target.h`:
     ```c
     #define SENSOR_ALIGNMENT_DEFAULT CW0
     ```
   - Permit runtime CLI modification via `set gyro_1_align = CW90` or `CUSTOM` matrix, allowing the laboratory team to calibrate physical axes on the bench without recompiling.

#### 3. Expected Inputs
* **Sensor Header**: Physical breakout connected to Header J1.
* **Interrupt**: 1.0 kHz pulse on `PA0`.

#### 4. Expected Results
* `WHO_AM_I` returns `0x47` on boot.
* Gyro data acquired at 1.0 kHz with $< 10\,\mu\text{s}$ bus transfer overhead.
* Axes freely configurable in Configurator GUI without hardcoded assumptions.

---

### Microcomponent 05_D: In-SRAM Configuration Architecture (`CONFIG_IN_RAM` & `EEPROM_SIZE 8192`)

#### 1. Why (Silicon Necessity & Architectural Traceability)
* **The No-XIP Ground Truth**: C-DAC THEJAS32 silicon does not implement memory-mapped external flash. Any CPU read or write to `0x20000000` causes an immediate Store/Load Access Fault (`mcause = 5/7`).
* **The 4 KB Overflow Risk**: Parameter Groups in Betaflight 4.x exceed 4,800 bytes. Lowering `EEPROM_SIZE` to 4096 triggers a boot panic (`FAILURE_CONFIG_STORE_FAILURE`).
* **In-SRAM Reliability**: Storing configuration in internal SRAM (`.bss`) guarantees 100% crash-free parameter reads and writes across warm soft-reboots (`save` command in CLI or Configurator).

#### 2. How (Detailed Algorithmic & Memory Implementation)
1. **Target Header Configuration**:
   - In [`src/platform/ARIES/target/ARIESV3/target.h`](file:///home/ivan/Desktop/betaflight/src/platform/ARIES/target/ARIESV3/target.h):
     ```c
     #define CONFIG_IN_RAM
     #define EEPROM_SIZE 8192
     ```
2. **Memory Buffer Allocation**:
   - Betaflight's `config_eeprom.c` allocates:
     ```c
     static uint8_t eepromData[EEPROM_SIZE] __attribute__((aligned(4)));
     ```
   - This places 8,192 bytes inside `.bss` in SRAM (`0x00202000` to `0x0023FFFF`).
3. **Warm-Boot Persistence**:
   - When the user issues `save` in CLI or Configurator:
     - `writeConfigToEEPROM()` serializes all Parameter Groups into `eepromData[]`.
     - System triggers software soft-reboot via `systemReset()`.
     - On warm restart, `startup.S` initializes `.data` and `.bss`, but can optionally preserve `eepromData` if a hot-boot magic signature (`0xDEADBEEF`) is validated.

#### 3. Expected Inputs
* **Configuration State**: PIDs, rates, mixer, filter settings from Configurator.
* **Storage Medium**: 8 KiB SRAM block.

#### 4. Expected Results
* Zero memory access traps (`mcause = 7` strictly eliminated).
* 100% parameter retention across soft reboots.
* Zero startup boot panics.

---

### Microcomponent 05_E: JEDEC Command-Based Cold-Reboot Flash Persistence Engine (`MDP_SPI_3`)

#### 1. Why (Silicon Necessity & Architectural Traceability)
* **Cold-Reboot Retention**: While `CONFIG_IN_RAM` persists through warm restarts, powering off the quadcopter erases SRAM. True cold-reboot persistence requires storing parameters in non-volatile SPI flash.
* **The Proven Solution**: C-DAC's reference implementation in [`lib/vega-sdk/vega-sdk/examples/spi/spi_write_read_pgm/main.c`](file:///home/ivan/Desktop/betaflight/lib/vega-sdk/vega-sdk/examples/spi/spi_write_read_pgm/main.c) proves how physical SPI flash is accessed on ARIES v3:
  - Target controller: `MDP_SPI_3` (`0x10200100`).
  - Chip Select control: `SPI_set_CSAAT_pin(MDP_SPI_3, 1)`.
  - Command byte protocol: JEDEC standard opcodes (`0x03`, `0x02`, `0x20`, `0x06`, `0x05`).
* **Zero Pointer Dereferencing**: The driver operates strictly via command buffers into a local SRAM staging array.

#### 2. How (Detailed Algorithmic & JEDEC Protocol Implementation)
1. **Peripheral Base Address**:
   - `SPI3_BASE`: `0x10200100` (`MDP_SPI_3`)
   - Flash Chip: Onboard Winbond / GigaDevice 2 MB SPI NOR Flash (`U5`).
2. **JEDEC Command Standard**:
   - `0x06`: Write Enable (`WREN`)
   - `0x05`: Read Status Register (`RDSR` - poll bit 0 `WIP`)
   - `0x20`: 4 KB Sector Erase (`SE4K`)
   - `0x02`: Page Program (`PP` - up to 256 bytes)
   - `0x03`: Read Data Bytes (`READ`)
3. **Execution Sequences**:
   - **Step 1: 4 KB Sector Erase**:
     1. Issue Write Enable (`0x06`).
     2. Assert CS with `CSAAT=1`.
     3. Send `0x20` followed by 3-byte address (`sectorAddr >> 16`, `sectorAddr >> 8`, `sectorAddr`).
     4. De-assert CS (`CSAAT=0`).
     5. Poll Status Register (`0x05`) until `WIP` bit 0 clears.
   - **Step 2: Page Program (256-Byte Chunks)**:
     1. For each 256-byte slice of the 8 KB configuration buffer:
        - Issue Write Enable (`0x06`).
        - Assert CS with `CSAAT=1`.
        - Send `0x02` followed by 3-byte address.
        - Send 256 data bytes sequentially.
        - De-assert CS (`CSAAT=0`).
        - Poll Status Register (`0x05`) until `WIP` bit 0 clears.
   - **Step 3: Staged Read Data Bytes (`0x03`)**:
     1. Assert CS with `CSAAT=1`.
     2. Send `0x03` followed by 3-byte address.
     3. Clock in 8,192 bytes directly into `eepromData[]` staging buffer.
     4. De-assert CS (`CSAAT=0`).
4. **Safety Assertion**:
   - Raw pointers (`0x20000000`) are strictly banned. All read/write operations pass through `spi3_transfer_byte()`.

#### 3. Expected Inputs
* **Data Buffer**: 8,192 bytes from Parameter Group serializer.
* **Flash Controller**: `MDP_SPI_3` (`0x10200100`).

#### 4. Expected Results
* Configuration persists across complete power disconnects (cold boot).
* JEDEC standard commands executed without memory access faults.
* Clean fallback to `CONFIG_IN_RAM` if flash is unpopulated or fails CRC check.

---

## 3. Subsystem Traceability Matrix

| Microcomponent ID | Functionality / Feature | Target Source Files | Silicon Verification Target |
| :--- | :--- | :--- | :--- |
| **05_A** | Polled SPI0 Master & 12.5 MHz SCK Clamp | `src/platform/ARIES/bus_spi_aries.c` | SCK clamped to 12.5 MHz; 14-byte gyro read completes in $< 9\,\mu\text{s}$. |
| **05_B** | Segmented Transfers & Bus Auto-Recovery | `src/platform/ARIES/bus_spi_aries.c` | Automatic shift register flush on timeout; null TX/RX supported. |
| **05_C** | ICM-42688P Sync & Orientation Config | `src/main/drivers/accgyro/`, `target.h` | 1.0 kHz EXTI DRDY sync on `PA0`; configurable orientation matrix. |
| **05_D** | In-SRAM Config Storage (`CONFIG_IN_RAM`) | `src/platform/ARIES/target/ARIESV3/target.h` | `EEPROM_SIZE 8192` in RAM; 0 boot panics; survives soft reboot. |
| **05_E** | JEDEC Flash Persistence (`MDP_SPI_3`) | `src/platform/ARIES/config_flash_aries.c` | JEDEC opcodes (`0x03`, `0x02`, `0x20`, `CSAAT`); cold reboot persistence. |

---

## 4. Anti-Assumption Guardrails

1. **Never Exceed 24.0 MHz SPI Clock on IMU**:
   - *Never* use Divisor 0 (25.0 MHz) on SPI0. High-grade sensors (ICM-42688P) are rated for 24 MHz max. Always clamp to Divisor 1 (12.5 MHz).
2. **Never Dereference Pointer `0x20000000`**:
   - *Never* use pointer access for flash memory. THEJAS32 has no XIP controller at `0x20000000`. Memory writes cause fatal Store Access Faults (`mcause = 7`).
3. **Never Lower `EEPROM_SIZE` Below 8192**:
   - *Never* reduce `EEPROM_SIZE` to 4096 to save RAM. Betaflight 4.x Parameter Groups require $\approx 5.1\text{ KB}$.
4. **Never Hardcode Sensor Orientation**:
   - *Never* assume physical IMU mounting matches `CW0`. Always verify orientation on the bench and permit CLI overrides.