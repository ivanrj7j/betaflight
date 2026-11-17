# Hardware Inspection Log: `src/platform/common/stm32/adc_impl.c`

## 1. File Purpose & Role in Architecture
* **File Path**: `src/platform/common/stm32/adc_impl.c`
* **Subsystem**: Analog-to-Digital Conversion (ADC) Hardware Abstraction, Multi-Channel Sequence Mapping, and Battery/Current Sensor Sampling.
* **Key Components**:
  * `adcOperatingConfig[ADC_SOURCE_COUNT]`: Dynamic lookup table mapping logical analog sources (`ADC_BATTERY`, `ADC_CURRENT`, `ADC_RSSI`) to their physical sequence position.
  * `adcValues[ADC_SOURCE_COUNT]`: Circular DMA-fed RAM buffer holding latest 12-bit conversion results.
  * `adcGetValue(source)`: Fast non-blocking getter returning latest digitized sensor value.
  * `adcVerifyPin()`: Validates whether a requested GPIO pin has silicon connectivity to the selected ADC peripheral.

---

## 2. Hardware Challenges STM32 Developers Faced & How They Overcame Them

### Challenge 1: Asynchronous DMA Sequence Channel Decoupling
* **The Problem**: On STM32, multiple analog inputs are sampled via a regular hardware sequence (Scan Mode) driven by DMA into RAM. However, the order in which pins are scanned depends on which features the pilot enabled:
  * If only Battery Voltage is enabled: DMA buffer index `0` holds Battery Voltage.
  * If both Current and Battery Voltage are enabled: DMA buffer index `0` might hold Current, and index `1` holds Battery Voltage.
  * Reading from a hardcoded index would return the wrong sensor data (e.g., interpreting current sensor noise as battery voltage).
* **How It Was Overcome**:
  The driver dynamically assigns a `dmaIndex` during `adcInit()`. When high-level flight code queries a sensor, it indexes via the resolved pointer:
  ```c
  const unsigned dmaIndex = adcOperatingConfig[source].dmaIndex;
  return dmaIndex < ARRAYLEN(adcValues) ? adcValues[dmaIndex] : 0;
  ```

---

### Challenge 2: Factory Calibration of Internal Bandgap (`VREFINTCAL`)
* **The Problem**: STM32 ADC conversion accuracy fluctuates with operating voltage (VDD) and ambient temperature. Converting raw ADC counts to exact millivolts requires measuring the internal bandgap reference.
* **How It Was Overcome**:
  STM32 chips store a factory calibration value (`VREFINT_CAL_ADDR`) programmed at ST testing facilities at 3.3V. `adc_impl.c` reads this constant at boot:
  ```c
  int32_t adcVREFINTCAL;
  ```
  This allows mathematical compensation for power supply droop.

---

## 3. Direct Porting Blueprint for ARIES v3.0 (THEJAS32 / VEGA ET1031)

### The Fundamental Architectural Difference:
* **STM32**: Monolithic internal multi-channel SAR ADC mapped directly into memory via circular DMA.
* **ARIES v3.0**: **No internal SAR ADC on the THEJAS32 chip!**
  * Channels `ADC_CH0` through `ADC_CH3` on Header `J10` route to an **external 4-channel I2C ADC chip (`U9`) connected via dedicated `I2C2`**.

### Porting Strategy:
1. **Asynchronous Background Task for ADC**:
   * We will retain the `adcValues[4]` array in RAM.
   * Instead of an internal DMA controller populating it, a lightweight cooperative scheduler task (`TASK_BATTERY` or an asynchronous I2C state machine) periodically sends a 4-channel read request to `U9` over `I2C2`:
     ```c
     // ARIES v3.0 ADC Cache Buffer
     volatile uint16_t aries_adc_raw[4]; // [0]=VBAT, [1]=CURRENT, [2]=RSSI, [3]=AUX

     uint16_t adcGetValue(adcSource_e source) {
         switch (source) {
             case ADC_BATTERY: return aries_adc_raw[0];
             case ADC_CURRENT: return aries_adc_raw[1];
             case ADC_RSSI:    return aries_adc_raw[2];
             default:          return 0;
         }
     }
     ```
2. **Impact on Initial Configurator Port**:
   * For the <128KB minimal Configurator target, battery voltage can return a static mock value (e.g., `12600` mV = 12.6V 3S lipo) or zero until the `I2C2` driver is initialized in flight testing.
