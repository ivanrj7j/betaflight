# Hardware Inspection Log: `src/platform/ESP32/io_esp32.c`

## 1. File Purpose & Role in Architecture
* **File Path**: `src/platform/ESP32/io_esp32.c`
* **Subsystem**: Single-Port Flat GPIO Architecture and Direct Fast Pin Access (`IORead`, `IOWrite`, `IOHi`, `IOLo`, `IOToggle`).
* **Consulted Log Comparison**:
  * [08_STM32_IO_IMPL.md](08_STM32_IO_IMPL.md) (STM32 Multi-port GPIOA–GPIOI, sparse bitmask compression, popcount indexing)

---

## 2. Similarities & Differences: STM32 vs ESP32

| Dimension | STM32 Implementation ([Log 08](08_STM32_IO_IMPL.md)) | ESP32 Implementation (`io_esp32.c`) | Porting Implication for ARIES v3.0 (THEJAS32) |
| :--- | :--- | :--- | :--- |
| **Port Structure** | Multi-letter 16-pin banks (`GPIOA` through `GPIOI`). Requires tracking port base addresses spaced at `0x400` intervals. | **Single Flat GPIO Port** (`DEFIO_PORT_USED_COUNT == 1`). Pins are numbered linearly $0..N$. | **Matches ESP32 perfectly**: THEJAS32 has a single flat 32-bit GPIO controller (`GPIO0` to `GPIO31`). |
| **Pin Lookup (`IOGetByTag`)** | Compressed bitmask with hardware `popcount` intrinsic to compute offsets without sparse gaps. | Direct $O(1)$ flat array index: `return &ioRecs[pinIdx];`. | **Adopt ESP32 pattern**: Zero bitmask calculation; direct index `ioRecs[pinIdx]` saves code size and execution time. |
| **Port Index Arithmetic** | Pointer subtraction: `(((size_t)IO_GPIO(io) - GPIOA_BASE) >> 10)`. | Constant zero: `return 0; // Single port`. | Constant zero: Single flat port on ARIES v3.0. |
| **Fast Bit Setting (`IOHi` / `IOLo`)** | Writes to 32-bit Bit Set/Reset Register (`GPIOx->BSRR`). | Calls `gpio_ll_set_level(&GPIO, pin, level)`. | Write directly to THEJAS32 GPIO Output Data Register or Direction Register. |

---

## 3. Deep Architectural Analysis: Single-Port Flat GPIO vs Multi-Bank GPIO

On STM32, the need to support up to 9 separate 16-bit GPIO ports forced Betaflight to generate complex sparse bitmasks to conserve RAM on smaller packages.

On ESP32, developers recognized that the hardware treats pins as a flat global pool ($0..48$). By setting `DEFIO_PORT_USED_COUNT = 1`, the entire `IOGetByTag()` function collapses into:
```c
IO_t IOGetByTag(ioTag_t tag)
{
    const int pinIdx = DEFIO_TAG_PIN(tag);
    if (pinIdx >= DEFIO_PIN_USED_COUNT) {
        return NULL;
    }
    return &ioRecs[pinIdx];
}
```
This is virtually zero overhead, perfectly suited for resource-constrained environments.

---

## 4. Porting Blueprint for ARIES v3.0 (THEJAS32 / VEGA ET1031)

* The ARIES v3.0 board features **32 GPIO pins** on the THEJAS32 SoC:
  * `GPIO0`–`GPIO10` (Header J2)
  * `GPIO11`–`GPIO15` (Header J3)
  * `GPIO16`–`GPIO21` (Header J10)
  * `GPIO22` (RGB Green), `GPIO23` (RGB Blue), `GPIO24` (RGB Red)
  * `GPIO25`–`GPIO31` (Header J9)
* **Action**: We will copy ESP32's flat `io_esp32.c` architecture directly into `src/platform/THEJAS32/io_thejas32.c`, setting:
  $$\text{DEFIO\_PIN\_USED\_COUNT} = 32, \quad \text{DEFIO\_PORT\_USED\_COUNT} = 1$$
* This gives instantaneous $O(1)$ pin control with under 256 bytes of RAM overhead!
