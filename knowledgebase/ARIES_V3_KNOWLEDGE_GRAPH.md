# ARIES v3.0 Hardware & Porting Knowledge Graph

This document models the complete entity-relationship knowledge graph for the **ARIES v3.0 (THEJAS32 / VEGA ET1031)** hardware platform based on the official C-DAC datasheets (`ARIESv3 0_Datasheet-4.pdf` and `ARIESv3 0_Datasheet_14-10-24.pdf`).

### Connected Knowledge Nodes
* **Master Knowledge Index**: [INDEX.md](file:///home/ivan/Desktop/betaflight/knowledgebase/INDEX.md)
* **Hardware Datasheet Specs**: [ARIES_V3_DATASHEET_ANALYSIS.md](file:///home/ivan/Desktop/betaflight/knowledgebase/ARIES_V3_DATASHEET_ANALYSIS.md)
* **Silicon & Core Manual**: [THEJAS32_PROGRAMMER_MANUAL_ANALYSIS.md](file:///home/ivan/Desktop/betaflight/knowledgebase/THEJAS32_PROGRAMMER_MANUAL_ANALYSIS.md)
* **Serial Protocol & Handshake**: [MSP_CONFIGURATOR_HANDSHAKE_GRAPH.md](file:///home/ivan/Desktop/betaflight/knowledgebase/MSP_CONFIGURATOR_HANDSHAKE_GRAPH.md)
* **Subsystem Architecture**: [BETAFLIGHT_ARCHITECTURE_GRAPH.md](file:///home/ivan/Desktop/betaflight/knowledgebase/BETAFLIGHT_ARCHITECTURE_GRAPH.md)
* **128KB Optimization**: [TARGET_OPTIMIZATION_128KB.md](file:///home/ivan/Desktop/betaflight/knowledgebase/TARGET_OPTIMIZATION_128KB.md)

---

## 1. Top-Level Hardware Architecture Knowledge Graph

```mermaid
flowchart TD
    subgraph ARIES_V3["ARIES v3.0 Board (C-DAC DIR-V)"]
        subgraph POWER_SYSTEM["Power Distribution Tree"]
            VIN["External VIN (7-12V DC Jack / Pad)"]
            VUSB["USB-C VCC_5V (5V Max 5.5V)"]
            SW_REG["Switching Regulator / LDO"]
            OPAMP["Power Arbiter (Op-Amp)"]
            VCC5V["VCC 5V Rail"]
            LDO_33["3.3V LDO (U1)"]
            LDO_12["1.2V LDO (U7)"]
            VCC33["VCC 3.3V (IO Rail - Max 12mA/pin)"]
            VCC12["VCC 1.2V (Core Logic Rail)"]

            VIN --> SW_REG --> VCC5V
            VUSB --> OPAMP --> VCC5V
            VCC5V --> LDO_33 --> VCC33
            VCC33 --> LDO_12 --> VCC12
        end

        subgraph THEJAS32_SOC["THEJAS32 ASIC / SoC"]
            subgraph CPU_CORE["Core Processing Unit"]
                VEGA["VEGA ET1031 RISC-V 32-bit Core"]
                OSC["100 MHz System Oscillator (U4)"]
                SRAM["256 KB Internal SRAM"]
                OSC -->|"CLK (Pin 44)"| VEGA
                VEGA <--> SRAM
            end

            subgraph SOC_PERIPHERALS["SoC Integrated Peripherals"]
                URT0["UART0 Controller"]
                URT1["UART1 Controller"]
                URT2["UART2 Controller"]

                SPI0["SPI0 Master Controller"]
                SPI1["SPI1 Master Controller"]
                SPI2["SPI2 Master Controller"]
                SPI3["SPI3 Master Controller"]

                I2C0_CTL["I2C0 Controller"]
                I2C1_CTL["I2C1 Controller"]
                I2C2_CTL["I2C2 Controller"]

                PWM_CTL["8-Channel PWM Module"]
                TIM_CTL["3x Hardware Timers (Timer 0..2)"]
                GPIO_CTL["32-Bit GPIO Controller (GPIO0..31)"]
            end
        end

        subgraph ONBOARD_DEVICES["On-Board Peripherals & Transceivers"]
            CP2102N["CP2102N USB-UART Bridge (U10)"]
            FLASH_MEM["2 MB SPI Boot Flash (U5)"]
            I2C_ADC["4-Channel I2C ADC IC (U9)"]
            RGB_LED["Status RGB LED (LD1)"]
            HB_LED["Processor Heartbeat LED (LD4)"]
            RST_IC["Reset Circuit & Button (U8 / RESET)"]
        end

        subgraph BOARD_HEADERS["External Pin Headers"]
            HDR_J1["Header J1 (SPI0, I2C1, PWM3-4)"]
            HDR_J2["Header J2 (UART1, PWM0-2, GPIO0-10)"]
            HDR_J3["Header J3 (PWM5-7, GPIO11-15)"]
            HDR_J4["Header J4 (SPI2)"]
            HDR_J5["Header J5 (UART2)"]
            HDR_J7["Header J7 (SPI1)"]
            HDR_J9["Header J9 (GPIO25-31)"]
            HDR_J10["Header J10 (I2C0, ADC CH0-3, GPIO16-21)"]
            HDR_J12["Header J12 (Boot Select)"]
        end
    end

    %% Wiring & Connections
    VCC12 -.-> THEJAS32_SOC
    VCC33 -.-> THEJAS32_SOC
    VCC33 -.-> ONBOARD_DEVICES

    URT0 <-->|"Pins 107/108"| CP2102N
    CP2102N <-->|"USB-C J6"| VUSB

    URT1 <-->|"TX: Pin 45, RX: Pin 50"| HDR_J2
    URT2 <-->|"TX: Pin 106, RX: Pin 105"| HDR_J5

    SPI0 <-->|"SCK:91, MISO:90, MOSI:87, CS:92"| HDR_J1
    SPI1 <-->|"SCK:40, MISO:41, MOSI:42, CS:39"| HDR_J7
    SPI2 <-->|"SCK:101, MISO:98, MOSI:97, CS:102"| HDR_J4
    SPI3 <-->|"Dedicated Flash Bus (Pins 7,10,11,12)"| FLASH_MEM

    I2C0_CTL <-->|"SCL: Pin 33, SDA: Pin 34"| HDR_J10
    I2C1_CTL <-->|"SCL: Pin 96, SDA: Pin 95"| HDR_J1
    I2C2_CTL <-->|"Dedicated ADC Bus (Pins 31, 32)"| I2C_ADC
    I2C_ADC <-->|"Analog In Channels 0..3"| HDR_J10

    PWM_CTL -->|"PWM0..PWM2"| HDR_J2
    PWM_CTL -->|"PWM3..PWM4"| HDR_J1
    PWM_CTL -->|"PWM5..PWM7"| HDR_J3

    GPIO_CTL -->|"GPIO22 (Green), GPIO23 (Blue), GPIO24 (Red)"| RGB_LED
    GPIO_CTL -->|"PROC_HB (Pin 16)"| HB_LED
    GPIO_CTL <--> HDR_J2
    GPIO_CTL <--> HDR_J3
    GPIO_CTL <--> HDR_J9
    GPIO_CTL <--> HDR_J10
```

---

## 2. Betaflight Flight Controller Component Mapping Graph

This graph specifies how the ARIES v3.0 hardware translates directly into standard Betaflight flight controller functional blocks:

```mermaid
graph LR
    subgraph BF_CORE["Betaflight Core Subsystems"]
        BF_SCHED["Scheduler & Timebase"]
        BF_CLI["MSP / CLI Serial Engine"]
        BF_RX["Serial Receiver (CRSF / ELRS / SBUS)"]
        BF_TEL["Telemetry & GPS"]
        BF_IMU["Sensors (Gyro & Accelerometer)"]
        BF_BB["Blackbox DataFlash"]
        BF_COMP["Barometer & Compass"]
        BF_BAT["Battery & Current Sensing"]
        BF_MOTORS["Motor Output Mixer"]
        BF_LED["Status Indicator Service"]
    end

    subgraph HAL_DRIVERS["Target Platform Drivers (Vega SDK HAL)"]
        HAL_TIMER["system_timer.c (Timer 0 / RISC-V mtime)"]
        HAL_UART0["serial_uart0.c (UART0 Driver)"]
        HAL_UART1["serial_uart1.c (UART1 Driver)"]
        HAL_UART2["serial_uart2.c (UART2 Driver)"]
        HAL_SPI0["bus_spi0.c (SPI0 Driver)"]
        HAL_SPI1["bus_spi1.c (SPI1 Driver)"]
        HAL_I2C0["bus_i2c0.c (I2C0 Driver)"]
        HAL_I2C2["adc_i2c2.c (I2C ADC Driver U9)"]
        HAL_PWM["motor_pwm.c (PWM Timer Driver)"]
        HAL_GPIO["io_gpio.c (GPIO Driver)"]
    end

    subgraph PHYSICAL_HW["ARIES v3.0 Hardware"]
        HW_TIMER["Timer 0 / 100MHz Tick"]
        HW_CP2102N["CP2102N USB-C Connector (J6)"]
        HW_J2["Header J2 (Pins 13, 15)"]
        HW_J5["Header J5 (Pins 1, 2)"]
        HW_J1_SPI["Header J1 (SPI0: Pins 5,6,7,8)"]
        HW_J7_SPI["Header J7 (SPI1: Pins 2,3,4,6)"]
        HW_J10_I2C["Header J10 (I2C0: Pins 10, 12)"]
        HW_U9_ADC["External I2C ADC (U9 / J10 Pins 2,4,6,8)"]
        HW_PWM_PINS["Headers J2 (0-2), J1 (3-4), J3 (5-7)"]
        HW_RGB["Onboard RGB LED (GPIO22, 23, 24)"]
    end

    %% Subsystem to HAL
    BF_SCHED --> HAL_TIMER
    BF_CLI --> HAL_UART0
    BF_RX --> HAL_UART1
    BF_TEL --> HAL_UART2
    BF_IMU --> HAL_SPI0
    BF_BB --> HAL_SPI1
    BF_COMP --> HAL_I2C0
    BF_BAT --> HAL_I2C2
    BF_MOTORS --> HAL_PWM
    BF_LED --> HAL_GPIO

    %% HAL to HW
    HAL_TIMER --> HW_TIMER
    HAL_UART0 --> HW_CP2102N
    HAL_UART1 --> HW_J2
    HAL_UART2 --> HW_J5
    HAL_SPI0 --> HW_J1_SPI
    HAL_SPI1 --> HW_J7_SPI
    HAL_I2C0 --> HW_J10_I2C
    HAL_I2C2 --> HW_U9_ADC
    HAL_PWM --> HW_PWM_PINS
    HAL_GPIO --> HW_RGB
```

---

## 3. Peripheral Entity Relationship Schema

```mermaid
erDiagram
    THEJAS32_SOC ||--|| VEGA_ET1031_CORE : contains
    THEJAS32_SOC ||--|| INTERNAL_SRAM : embeds_256KB
    THEJAS32_SOC ||--o{ UART_CONTROLLER : integrates_3x
    THEJAS32_SOC ||--o{ SPI_CONTROLLER : integrates_4x
    THEJAS32_SOC ||--o{ I2C_CONTROLLER : integrates_3x
    THEJAS32_SOC ||--o{ PWM_OUTPUT : provides_8x
    THEJAS32_SOC ||--o{ HW_TIMER : provides_3x
    THEJAS32_SOC ||--o{ GPIO_PIN : routes_32x

    SPI_CONTROLLER ||--|| BOOT_FLASH_IC : SPI3_dedicated_to
    I2C_CONTROLLER ||--|| EXTERNAL_ADC_IC : I2C2_dedicated_to
    UART_CONTROLLER ||--|| CP2102N_BRIDGE : UART0_hardwired_to

    GPIO_PIN ||--|| RGB_LED_GREEN : GPIO22
    GPIO_PIN ||--|| RGB_LED_BLUE : GPIO23
    GPIO_PIN ||--|| RGB_LED_RED : GPIO24
    GPIO_PIN ||--|| HEARTBEAT_LED : PROC_HB_Pin16

    UART_CONTROLLER ||--o{ EXTERNAL_HEADER_PINS : UART1_J2_UART2_J5
    SPI_CONTROLLER ||--o{ EXTERNAL_HEADER_PINS : SPI0_J1_SPI1_J7_SPI2_J4
    I2C_CONTROLLER ||--o{ EXTERNAL_HEADER_PINS : I2C0_J10_I2C1_J1
    PWM_OUTPUT ||--o{ EXTERNAL_HEADER_PINS : PWM_J1_J2_J3
    EXTERNAL_ADC_IC ||--o{ EXTERNAL_HEADER_PINS : ADC_Channels_J10
```

---

## 4. Key Architectural Takeaways for Betaflight Porting

1. **Architecture & Instruction Set**:
   * Processor is a 32-bit RISC-V (`rv32imac` or similar VEGA ET1031 configuration).
   * Toolchain required: `riscv32-unknown-elf-gcc` or `riscv64-unknown-elf-gcc -march=rv32... -mabi=ilp32`.
2. **Interrupt Controller (PLIC / CLIC / CLINT)**:
   * RISC-V standard machine timer (`mtime` / `mtimecmp`) and PLIC (Platform-Level Interrupt Controller) are used for peripheral IRQs (UART, SPI, Timer).
3. **Memory Constrained Footprint**:
   * Internal SRAM: 256 KB (plenty for Betaflight which typically runs comfortably in 64KB - 128KB on STM32F405 / STM32F411).
   * External SPI Flash: 2 MB boot storage.
4. **Asynchronous I2C ADC vs Monolithic SAR ADC**:
   * In traditional STM32 targets, ADC is sampled via internal peripheral DMA. On ARIES v3.0, battery voltage and current monitoring must read from `U9` via `I2C2` bus.
5. **No Native USB OTG Core in SoC**:
   * Unlike STM32F4/F7/H7 which implement USB Full Speed / High Speed VCP internally, ARIES v3.0 relies on the external CP2102N bridge connected to `UART0`. Betaflight will talk to the PC via standard serial UART at 115200 baud or higher.
