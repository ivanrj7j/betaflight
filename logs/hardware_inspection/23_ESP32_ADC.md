# Hardware Inspection Log: `src/platform/ESP32/adc_esp32.c`
**Consulted Log**: [11_STM32_ADC_IMPL.md](11_STM32_ADC_IMPL.md)

## 1. File Purpose & Role in Architecture
* **File Path**: [src/platform/ESP32/adc_esp32.c](../../src/platform/ESP32/adc_esp32.c)
* **Subsystem**: Analog-to-Digital Converter (ADC) Hardware Abstraction Layer.
* **Key Components**:
  * `adcInit()`: Configures ADC1 resolution (12-bit) and 12 dB input attenuation (`ADC_ATTEN_DB_12`) for 0–3.3V measurement range.
  * `adcReadChannel()`: Polled oneshot conversion engine with timeout protection.
  * `adcGetValue()`: Non-DMA, on-demand conversion getter executed during the 10 Hz battery monitor task.

---

## 2. Comparative Analysis: ESP32 vs. STM32 (`11_STM32_ADC_IMPL.md`)

| Feature / Hardware Challenge | STM32 Implementation (`11_STM32_ADC_IMPL.md`) | ESP32 Implementation (`adc_esp32.c`) |
| :--- | :--- | :--- |
| **Conversion Architecture** | Continuous Scan Mode driven by hardware circular DMA streaming into SRAM. | On-demand polled oneshot conversion (`adc_oneshot_ll_start`). Zero DMA streams used! |
| **Execution Rate & Bus Impact** | Free-running hardware scan continuously consuming AHB/DMA bus bandwidth. | Converted on-demand only when queried by `TASK_BATTERY` (~10 Hz to 50 Hz). Zero background bus load. |
| **Channel Mapping Complexity** | Dynamic sequence indices (`dmaIndex`) required because enabled channels shift DMA array positions. | Direct hardware channel binding (`adcOperatingConfig[source].channel`). |
| **Voltage Calibration** | Factory programmed internal bandgap reference address (`VREFINTCAL`) compensated at runtime. | Fixed nominal eFuse bandgap reference (`1100 mV`) or built-in calibration curve. |
| **Code Footprint** | Large: multi-stage HAL DMA configuration, sequence registers, and calibration routines (~2.5 KB). | Ultra-compact: single oneshot polling function (< 0.8 KB). |

---

## 3. Hardware Challenges ESP32 Developers Faced & How They Overcame Them

### Challenge 1: Disjoint Silicon Pin-to-Channel Mapping
* **The Problem**: On original ESP32 silicon, ADC1 channels were mapped non-linearly across disjoint GPIO ranges:
  * Channels 0–3 were on `GPIO36`–`GPIO39`.
  * Channels 4–7 were on `GPIO32`–`GPIO35`.
  * On ESP32-S3, channels 0–9 were reorganized linearly from `GPIO1` to `GPIO10`.
* **How It Was Overcome**:
  The driver abstracted the pin resolution through compile-time SoC checks:
  ```c
  #if defined(ESP32S3)
      int channel = pin - ESP32_ADC1_GPIO_BASE;
  #else
      if (pin >= 36 && pin <= 39) return pin - 36;
      if (pin >= 32 && pin <= 35) return pin - 32 + 4;
  #endif
  ```

### Challenge 2: Avoiding Unnecessary DMA Channel Allocation
* **The Problem**: General-purpose DMA channels on microcontrollers are scarce (ESP32 only has 3 pairs on S3, shared with SPI and peripherals). Using DMA for battery monitoring (which updates at ~10 Hz for human pilots) wastefully locks up a DMA channel that high-speed gyro or Blackbox logging needs.
* **How It Was Overcome**:
  ESP32 developers chose **not to use DMA at all for ADC**. Instead, `adcGetValue()` executes a brief oneshot conversion:
  ```c
  adc_oneshot_ll_set_channel(ADC_UNIT_1, channel);
  adc_oneshot_ll_start(ADC_UNIT_1);
  while (!adc_oneshot_ll_get_event(ADC_LL_EVENT_ADC1_ONESHOT_DONE) && --timeout > 0);
  ```
  Since an ADC conversion takes ~10–20 microseconds and runs only every 100 milliseconds, CPU impact is virtually zero (0.02% CPU usage).

---

## 4. Key Takeaways & Porting Blueprint for ARIES v3.0 (THEJAS32 / VEGA ET1031)

1. **Confirmation: DMA is NOT Required for ADC in Betaflight**:
   * ESP32 proves that a polled / on-demand ADC architecture is 100% compliant with Betaflight flight control code and Configurator telemetry.
2. **Implementation Strategy for ARIES v3.0 External I2C ADC (U9)**:
   * ARIES v3.0 does not have an internal SAR ADC; pins `ADC_CH0`–`ADC_CH3` on Header `J10` connect to an external 4-channel I2C ADC chip (`U9`) on `I2C2`.
   * Exactly like ESP32, we do NOT need complex circular DMA. A simple periodic I2C read from `U9` into a static array `aries_adc_raw[4]` satisfies `adcGetValue()`.
3. **Target Optimization for <128KB Configurator Target**:
   * For initial MSP handshake and Configurator connection, ADC can return a fixed dummy voltage (e.g. 12.6V = 12600 mV) or 0. This saves Flash space and eliminates external I2C device dependencies during early board bring-up.
