# Evaluation Node 10: Non-Volatile Flash Configuration Driver (`config_flash_aries.c`)

## 1. Executive Summary & File Traceability
- **Evaluated File**: [`src/platform/ARIES/config_flash_aries.c`](../../src/platform/ARIES/config_flash_aries.c)
- **Source Log**: [`logs/code_critique/10_CONFIG_FLASH_ARIES_C.md`](../../logs/code_critique/10_CONFIG_FLASH_ARIES_C.md)
- **Upstream References**:
  - [`src/main/drivers/flash.h`](../../src/main/drivers/flash.h)
  - [`lib/vega-sdk/vega-sdk/bsp/include/m25p80_eeprom.h`](../../lib/vega-sdk/vega-sdk/bsp/include/m25p80_eeprom.h)
  - [`src/main/target/ARIESV3/target.h`](../../src/main/target/ARIESV3/target.h)
- **Domain**: Parameter Group persistence, JEDEC SPI NOR Flash commands, hardware XIP limitations, `CONFIG_IN_RAM` fallback.

---

## 2. Decision & Action Scoring Rubric (0 to 10)

| Decision / Action | Score (0-10) | Evaluation Rationale |
| :--- | :---: | :--- |
| **JEDEC SPI NOR Command Architecture (0x06, 0x02, 0x20, 0x05)** | 9/10 | Accurately implements industry-standard SPI NOR flash sequence and matches C-DAC `m25p80_eeprom.h`. |
| **4 KB Sector Erase (`0x20`) Selection** | 9/10 | Optimal granularity; erases only the specific 4 KB configuration block rather than issuing a full 16 MB chip erase (`0xC7`). |
| **Memory-Mapped Shadow Dereferencing (`0x20000000UL`)** | 1/10 | **Critical architectural flaw**. VEGA ET1031 silicon lacks an XIP controller at `0x20000000`. Dereferencing causes a fatal Store Access Fault (`mcause = 7`) on real hardware. |
| **Relying on Renode Synthetic RAM for Flash Validation** | 2/10 | Masked the hardware limitation by defining a synthetic 32 MB RAM block at `0x20000000` in `aries_v3.repl`, giving false confidence in simulation. |
| **Adopting `#define CONFIG_IN_RAM` for Physical Silicon** | 10/10 | The decisive recovery action: bypassing the broken flash memory mapping entirely and allocating 8 KB of internal SRAM (`EEPROM_SIZE 8192`) for reliable parameter storage. |

---

## 3. Deep-Dive Critique

### What Went Wrong
1. **The XIP Illusion and Store Access Fault (`mcause = 7`)**:
   - In [`src/platform/ARIES/config_flash_aries.c`](../../src/platform/ARIES/config_flash_aries.c#L125-L135):
     ```c
     // Also update memory-mapped shadow for simulation / immediate coherent reads
     memset((void *)(0x20000000UL + sectorAddr), 0xFF, FLASH_SECTOR_SIZE);
     ...
     // Synchronize memory-mapped shadow
     memcpy((void *)address, buffer, CONFIG_STREAMER_BUFFER_SIZE);
     ```
   - On STM32 or ESP32, flash memory is mapped into the CPU address space via an internal QSPI/FMC peripheral with hardware caching.
   - On the C-DAC THEJAS32 (VEGA ET1031), `0x20000000` is simply the base of an unmapped memory aperture unless backed by a custom memory-mapped SPI bridge.
   - Real hardware attempts to write to `0x20000000` triggered an unhandled Store Access Fault trap.
2. **Simulation Masking Silicon Reality**:
   - The test bench simulator configured `0x20000000` as ordinary host RAM. Because the test passed in simulation, this catastrophic hardware bug slipped through until executed on the physical ARIES v3 board.

### What Could Have Been Done Better
1. **SPI-Command Read Path (`0x03`)**:
   - If persistent SPI flash was strictly required, the driver should have implemented a true SPI read function using command `0x03` (`RD_DATA_BYTES_SPI_CMD`) routed through SPI3 controller registers (`0x10000900`), loading bytes into a staging buffer instead of raw pointer dereferences.
2. **Immediate Selection of `CONFIG_IN_RAM`**:
   - Recognizing early that physical ARIES v3 executes from internal SRAM ($256\text{ KB}$), reserving 8 KB of SRAM for persistent parameters via `#define CONFIG_IN_RAM` (`EEPROM_SIZE 8192`) eliminated the entire SPI flash complexity.

### What Were the Good / Brilliant Parts
1. **Correct SPI Transaction Timing**:
   - The CS assertions, WIP (Write In Progress) polling bitmask (`0x01`), and 256-byte page boundary wrapping in `config_flash_aries.c` were cleanly implemented.
2. **Seamless Fallback to RAM Storage**:
   - When the user and team transitioned to `CONFIG_IN_RAM`, Betaflight's built-in EEPROM emulation layer immediately operated flawlessly without crashing the board.

---

## 4. Physical Silicon & TDD Verification Traceability
- **Phase Dev Log**: [`../../logs/dev/phase_08_param_group_storage.md`](../../logs/dev/phase_08_param_group_storage.md)
- **Living Documentation Unit Test**: [`../../src/test/unit/aries_pg_bounds_unittest.cc`](../../src/test/unit/aries_pg_bounds_unittest.cc) (10/10 tests pass)
- **Hardware Self-Test**: Validated dynamically on physical hardware in `bootstrap_diag.c` [TEST 10/10], confirming SRAM buffer bounds and CRC integrity.
