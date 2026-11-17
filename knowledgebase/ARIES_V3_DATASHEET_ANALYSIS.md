# ARIES v3.0 Board & THEJAS32 SoC Hardware Specification Knowledge Base

### Connected Knowledge Nodes
* **Master Knowledge Index**: [INDEX.md](INDEX.md)
* **Visual Topology Graph**: [ARIES_V3_KNOWLEDGE_GRAPH.md](ARIES_V3_KNOWLEDGE_GRAPH.md)
* **Silicon & Core Manual**: [THEJAS32_PROGRAMMER_MANUAL_ANALYSIS.md](THEJAS32_PROGRAMMER_MANUAL_ANALYSIS.md)
* **Serial Protocol & Handshake**: [MSP_CONFIGURATOR_HANDSHAKE_GRAPH.md](MSP_CONFIGURATOR_HANDSHAKE_GRAPH.md)
* **Subsystem Architecture**: [BETAFLIGHT_ARCHITECTURE_GRAPH.md](BETAFLIGHT_ARCHITECTURE_GRAPH.md)
* **128KB Optimization**: [TARGET_OPTIMIZATION_128KB.md](TARGET_OPTIMIZATION_128KB.md)

---

## 1. Executive Summary & Hardware Overview

The **ARIES v3.0** is an embedded development board designed by the Centre for Development of Advanced Computing (C-DAC) under the Digital India RISC-V (DIR-V) Program. It is centered around the **THEJAS32 ASIC/SoC**, which incorporates the **VEGA ET1031** 32-bit RISC-V microprocessor.

### Core Architecture Parameters
* **Microprocessor**: VEGA ET1031 (RISC-V 32-bit ISA compliant core)
* **Clock Frequency**: 100 MHz (driven by external 100 MHz oscillator U4 on `CLK` pin 44)
* **Internal SRAM**: 256 KB
* **Non-Volatile Storage (Flash)**: 2 MB external SPI Flash memory (`U5`), interfaced via dedicated `SPI3` controller
* **Logic Supply Voltage**: 1.2V DC (supplied via low-dropout regulator `U7` to `VDD`/`PVDDC` pins)
* **I/O Supply Voltage**: 3.3V DC (supplied via low-dropout regulator `U1` to `VDDIO`/`PVDDIO` pins)
* **External Input Voltage**: 7V to 12V DC via DC barrel power jack (`J8`) or VIN pad; 5V via USB-C (`J6`)
* **I/O Current Rating**: 12 mA max DC current per I/O pin
* **Operating Temperature**: -0 °C to +85 °C

---

## 2. Datasheet Revision Comparison & Discrepancy Analysis

Two datasheets were reviewed:
1. **March 2023 Revision** (`ARIESv3 0_Datasheet-4.pdf` - 12 pages)
2. **October 2024 Revision** (`ARIESv3 0_Datasheet_14-10-24.pdf` - 17 pages)

### Key Differences & Refinements:
| Feature / Area | 2023 Initial Release (`Datasheet-4`) | 2024 Updated Release (`Datasheet_14-10-24`) | Porting Impact |
| :--- | :--- | :--- | :--- |
| **USB-to-UART Interface** | Labeled generically as "FTDI" in power tree | Specified explicitly as **Silicon Labs CP2102N** USB-to-UART bridge | UART0 connects directly to CP2102N for serial CLI/MSP. Uses standard CP210x drivers. |
| **VIN Power Regulation** | Indicated as LDO | Indicated as **Switching Regulator** step-down to 5V | Better thermal efficiency when running off flight pack (2S-3S directly, or 5V BEC). |
| **Pin Labeling Scheme** | Grouped as `GPIO0(0..15)` and `GPIO1(0..15)` | Flattened into `GPIO0` through `GPIO31` with secondary SoC naming | Eliminates numbering ambiguity; GPIO numbers directly match SDK definitions. |
| **JTAG / Test Pins** | Explicitly labeled `TJTAG*` / `TST*` | Labeled `RFIU1` through `RFIU8` (Reserved for Internal Use) | Clarifies internal board wiring and pull-down configurations. |
| **Exposed GPIO Count** | Stated as 32 GPIOs total | Clarified: 23 general GPIO headers, 3 dedicated to RGB LED, 6 reserved/internal | In Betaflight target definitions, only exposed header pins should be mapped. |

---

## 3. Peripheral Architecture & Header Pin Mapping

### 3.1 Serial Interfaces (UART)
The SoC contains 3 UART controllers:
* **UART0**:
  * **TX**: Pin 108 (`UART_TX0` / `URT0SOUT`) $\rightarrow$ Connected directly to CP2102N USB Bridge (`U10_17`)
  * **RX**: Pin 107 (`UART_RX0` / `URT0SIN`) $\rightarrow$ Connected directly to CP2102N USB Bridge (`U10_18`)
  * *Flight Controller Role*: Betaflight MSP, CLI, VCP communication over USB-C.
* **UART1**:
  * **TX**: Pin 45 (`UART_TX1` / `URT1SOUT`) $\rightarrow$ Header `J2_13`
  * **RX**: Pin 50 (`UART_RX1` / `URT1SIN`) $\rightarrow$ Header `J2_15`
  * *Flight Controller Role*: Serial RX (CRSF, ELRS, SBUS, IBUS) or Telemetry / GPS.
* **UART2**:
  * **TX**: Pin 106 (`UART_TX2` / `URT2SOUT`) $\rightarrow$ Header `J5_2`
  * **RX**: Pin 105 (`UART_RX2` / `URT2SIN`) $\rightarrow$ Header `J5_1`
  * *Flight Controller Role*: GPS, ESC Telemetry, or Digital VTX (HDZero, Walksnail, DJI).

---

### 3.2 SPI Interfaces
The SoC contains 4 SPI controllers:
* **SPI0**:
  * **MOSI**: Pin 87 (`SPI_MOSI0`) $\rightarrow$ Header `J1_7`
  * **MISO**: Pin 90 (`SPI_MISO0`) $\rightarrow$ Header `J1_6`
  * **SCLK**: Pin 91 (`SPI_SCLK0`) $\rightarrow$ Header `J1_5`
  * **CS/SS**: Pin 92 (`SPI_SS0`) $\rightarrow$ Header `J1_8`
  * *Flight Controller Role*: Primary high-speed SPI bus for IMU (MPU6000, BMI270, ICM42688P).
* **SPI1**:
  * **MOSI**: Pin 42 (`SPI_MOSI1`) $\rightarrow$ Header `J7_3`
  * **MISO**: Pin 41 (`SPI_MISO1`) $\rightarrow$ Header `J7_2`
  * **SCLK**: Pin 40 (`SPI_SCLK1`) $\rightarrow$ Header `J7_4`
  * **CS/SS**: Pin 39 (`SPI_SS1`) $\rightarrow$ Header `J7_6`
  * *Flight Controller Role*: Secondary SPI bus (External Flash DataFlash / Blackbox logging or MAX7456 OSD).
* **SPI2**:
  * **MOSI**: Pin 97 (`SPI_MOSI2`) $\rightarrow$ Header `J4_3`
  * **MISO**: Pin 98 (`SPI_MISO2`) $\rightarrow$ Header `J4_2`
  * **SCLK**: Pin 101 (`SPI_SCLK2`) $\rightarrow$ Header `J4_4`
  * **CS/SS**: Pin 102 (`SPI_SS2`) $\rightarrow$ Header `J4_6`
  * *Flight Controller Role*: Auxiliary SPI bus / Barometer or external sensors.
* **SPI3**:
  * **MOSI**: Pin 7 (`SPI_MOSI3`) $\rightarrow$ Dedicated connection to onboard SPI Boot Flash (`U5_5`)
  * **MISO**: Pin 10 (`SPI_MISO3`) $\rightarrow$ Dedicated connection to onboard SPI Boot Flash (`U5_2`)
  * **SCLK**: Pin 11 (`SPI_SCLK3`) $\rightarrow$ Dedicated connection to onboard SPI Boot Flash (`U5_6`)
  * **CS/SS**: Pin 12 (`SPI_SS3`) $\rightarrow$ Dedicated connection to onboard SPI Boot Flash (`U5_1`)
  * *Flight Controller Role*: System bootloader and application storage (2 MB onboard flash). Not exposed to headers.

---

### 3.3 I2C Interfaces
The SoC provides 3 I2C interfaces:
* **I2C0**:
  * **SCL**: Pin 33 (`I2C_SCL0`) $\rightarrow$ Header `J10_12`
  * **SDA**: Pin 34 (`I2C_SDA0`) $\rightarrow$ Header `J10_10`
  * *Flight Controller Role*: External sensors (Compass / Magnetometer QMC5883L/IST8310, Barometer BMP280/DPS310).
* **I2C1**:
  * **SCL**: Pin 96 (`I2C_SCL1`) $\rightarrow$ Header `J1_1`
  * **SDA**: Pin 95 (`I2C_SDA1`) $\rightarrow$ Header `J1_2`
  * *Flight Controller Role*: Auxiliary I2C bus.
* **I2C2**:
  * **SCL**: Pin 32 (`I2C_SCL2`) $\rightarrow$ Dedicated connection to onboard I2C ADC IC (`U9_10`)
  * **SDA**: Pin 31 (`I2C_SDA2`) $\rightarrow$ Dedicated connection to onboard I2C ADC IC (`U9_9`)
  * *Flight Controller Role*: Internal communication to sample analog input voltages.

---

### 3.4 Analog-to-Digital Converter (ADC)
* Unlike monolithic MCUs with on-chip SAR ADCs (e.g. STM32), ARIES v3.0 routes analog inputs through an **external 4-channel I2C ADC chip (`U9`)**:
  * **ADC_CH0**: Header `J10_2` $\rightarrow$ `U9_4` (Ideal for Battery Voltage Monitoring / V_BAT via divider)
  * **ADC_CH1**: Header `J10_4` $\rightarrow$ `U9_5` (Ideal for Current Sensor / I_BAT)
  * **ADC_CH2**: Header `J10_6` $\rightarrow$ `U9_6` (RSSI or Analog Pitot/Airspeed)
  * **ADC_CH3**: Header `J10_8` $\rightarrow$ `U9_7` (Spare analog / external sensor)
* *Porting Note*: The Betaflight ADC HAL driver for this target must fetch samples asynchronously over `I2C2` rather than reading raw internal SAR ADC registers.

---

### 3.5 Pulse Width Modulation (PWM) & Timers
* **PWM Channels (8 hardware outputs)**:
  * `PWM[0]`: Header `J2_5` (Pin 86) $\rightarrow$ Motor 1
  * `PWM[1]`: Header `J2_3` (Pin 85) $\rightarrow$ Motor 2
  * `PWM[2]`: Header `J2_1` (Pin 82) $\rightarrow$ Motor 3
  * `PWM[3]`: Header `J1_10` (Pin 81) $\rightarrow$ Motor 4
  * `PWM[4]`: Header `J1_9` (Pin 80) $\rightarrow$ Motor 5 / Servo 1
  * `PWM[5]`: Header `J3_3` (Pin 77) $\rightarrow$ Motor 6 / Servo 2
  * `PWM[6]`: Header `J3_2` (Pin 76) $\rightarrow$ Motor 7 / LED Strip
  * `PWM[7]`: Header `J3_1` (Pin 75) $\rightarrow$ Motor 8 / Beeper
* **Timers**: 3 Hardware Timers (Timer 0, Timer 1, Timer 2).
  * Used for Betaflight system tick (`micros()`, `millis()`), scheduler timebase, and delay routines.

---

### 3.6 GPIO & Status Indicators
* **32 General Purpose I/O lines total**:
  * `GPIO0` - `GPIO10`: Header `J2` (`J2_11`, `J2_9`, `J2_7`, `J2_16`, `J2_14`, `J2_12`, `J2_10`, `J2_8`, `J2_6`, `J2_4`, `J2_2`)
  * `GPIO11` - `GPIO15`: Header `J3` (`J3_8`, `J3_7`, `J3_6`, `J3_5`, `J3_4`)
  * `GPIO16` - `GPIO21`: Header `J10` (`J10_11`, `J10_9`, `J10_7`, `J10_5`, `J10_3`, `J10_1`)
  * `GPIO22`: Connected to **RGB LED Green** (`LD1_4`)
  * `GPIO23`: Connected to **RGB LED Blue** (`LD1_3`)
  * `GPIO24`: Connected to **RGB LED Red** (`LD1_2`)
  * `GPIO25` - `GPIO31`: Header `J9` (`J9_7`, `J9_6`, `J9_5`, `J9_4`, `J9_3`, `J9_2`, `J9_1`)
* **Dedicated Hardware Status Signals**:
  * `PROC_HB` (Pin 16): Heartbeat LED (`LD4_1`)
  * `PUSH_RESETN` (Pin 43): Active-low reset button / reset IC `U8`
  * `BOOT_SEL` (Pin 15): Boot selector switch on Header `J12_1`

---

## 4. Flight Controller Resource Allocation Matrix

| Flight Controller Function | Required Interface | Selected ARIES v3.0 Resource | Board Pin / Header |
| :--- | :--- | :--- | :--- |
| **Betaflight CLI / MSP (USB)** | UART (Virtual COM) | `UART0` (via CP2102N) | USB-C (`J6`) / Internal Pins 107, 108 |
| **Receiver (CRSF / ELRS / SBUS)** | Serial RX UART | `UART1` | `J2_15` (RX1), `J2_13` (TX1) |
| **Telemetry / GPS / OSD** | Serial Port | `UART2` | `J5_1` (RX2), `J5_2` (TX2) |
| **Primary IMU (Gyro + Accel)** | High Speed SPI | `SPI0` | `J1_5` (SCK), `J1_6` (MISO), `J1_7` (MOSI), `J1_8` (CS) |
| **Blackbox Logging / Flash** | SPI Bus | `SPI1` | `J7_4` (SCK), `J7_2` (MISO), `J7_3` (MOSI), `J7_6` (CS) |
| **Barometer / Magnetometer** | I2C Bus | `I2C0` | `J10_12` (SCL), `J10_10` (SDA) |
| **Battery Voltage & Current** | Analog ADC | `U9` I2C ADC (via `I2C2`) | `J10_2` (CH0: VBat), `J10_4` (CH1: Current) |
| **Motor 1 (PWM / DShot timing)** | Timer / PWM Out | `PWM[0]` | `J2_5` |
| **Motor 2 (PWM / DShot timing)** | Timer / PWM Out | `PWM[1]` | `J2_3` |
| **Motor 3 (PWM / DShot timing)** | Timer / PWM Out | `PWM[2]` | `J2_1` |
| **Motor 4 (PWM / DShot timing)** | Timer / PWM Out | `PWM[3]` | `J1_10` |
| **Status LEDs (Arm, FC State)** | GPIO Out | `GPIO22`, `GPIO23`, `GPIO24` | Onboard RGB LED (`LD1`) |
| **System Microsecond Clock** | Hardware Timer | `Timer 0` (VEGA SDK) | Internal core peripheral |
| **Task / RTOS / Scheduler Tick** | SysTick / MTIMER | RISC-V `mtime` / `mtimecmp` | Internal RISC-V Core |
