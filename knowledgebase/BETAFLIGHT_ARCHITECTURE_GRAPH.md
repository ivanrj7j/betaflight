# Betaflight Subsystem & Execution Architecture Knowledge Graph

## 1. Overview & Architectural Hierarchy

Betaflight is structured into modular layers spanning from hardware abstraction up to application and serial protocol handlers.

### Connected Knowledge Nodes
* **Hardware Physical Platform**: [ARIES_V3_DATASHEET_ANALYSIS.md](file:///home/ivan/Desktop/betaflight/knowledgebase/ARIES_V3_DATASHEET_ANALYSIS.md)
* **SoC & Memory Layout**: [THEJAS32_PROGRAMMER_MANUAL_ANALYSIS.md](file:///home/ivan/Desktop/betaflight/knowledgebase/THEJAS32_PROGRAMMER_MANUAL_ANALYSIS.md)
* **Hardware Visual Graph**: [ARIES_V3_KNOWLEDGE_GRAPH.md](file:///home/ivan/Desktop/betaflight/knowledgebase/ARIES_V3_KNOWLEDGE_GRAPH.md)
* **Serial MSP & Configurator Handshake**: [MSP_CONFIGURATOR_HANDSHAKE_GRAPH.md](file:///home/ivan/Desktop/betaflight/knowledgebase/MSP_CONFIGURATOR_HANDSHAKE_GRAPH.md)
* **128 KB Optimization Strategy**: [TARGET_OPTIMIZATION_128KB.md](file:///home/ivan/Desktop/betaflight/knowledgebase/TARGET_OPTIMIZATION_128KB.md)
* **Master Knowledge Index**: [INDEX.md](file:///home/ivan/Desktop/betaflight/knowledgebase/INDEX.md)
* **Betaflight Docs Reference**: [betaflight.com App Architecture](file:///home/ivan/Desktop/betaflight/knowledgebase/betaflight.com/docs/development/App-Architecture.mdx) | [Bus Drivers](file:///home/ivan/Desktop/betaflight/knowledgebase/betaflight.com/docs/development/API/BusDrivers.md)

---

## 2. Betaflight Layered Architecture Knowledge Graph

```mermaid
graph TD
    subgraph APPLICATION_LAYER["Application & Host Communication"]
        MSP_SRV["MSP Protocol Server (src/main/msp/)"]
        CLI_SRV["CLI Interactive Terminal (src/main/cli/)"]
        BLACKBOX_APP["Blackbox Logging Engine"]
    end

    subgraph SCHEDULER_CORE["Cooperative Task Scheduler (src/main/scheduler/)"]
        TASK_SYSTEM["TASK_SYSTEM (10 Hz)"]
        TASK_SERIAL["TASK_SERIAL (100 Hz - MSP/CLI drain)"]
        TASK_GYROPID["TASK_GYROPID (Realtime loop)"]
        TASK_BATTERY["TASK_BATTERY (50 Hz)"]
    end

    subgraph FLIGHT_SUBSYSTEMS["Flight Control Subsystems (src/main/fc/)"]
        RC_CONTROLLER["RC Processing & Smoothing"]
        PID_CONTROLLER["PID Loop Calculation"]
        MIXER["Motor / Servo Output Mixer"]
        SENSORS["Sensor Fusion & Attitude Estimation (IMU)"]
    end

    subgraph BUS_DEVICE_ABSTRACTION["Bus & Device Abstraction (src/main/drivers/bus.h)"]
        EXT_DEV["extDevice_t (Agnostic Device Descriptor)"]
        BUS_DEV["busDevice_t (Bus Instance Descriptor)"]
        BUS_OPS["busReadRegister / busWriteRegister API"]
        EXT_DEV --> BUS_DEV
        BUS_DEV --> BUS_OPS
    end

    subgraph DRIVER_LAYER["Peripheral Drivers (src/main/drivers/)"]
        DRV_SERIAL["serial.c / serial_uart.c"]
        DRV_SPI["bus_spi.c"]
        DRV_I2C["bus_i2c.c"]
        DRV_PWM["pwm_output.c"]
        DRV_TIMER["timer.c"]
    end

    subgraph PLATFORM_HAL["Target Platform HAL (src/platform/ / src/main/target/)"]
        ARIES_STARTUP["crt.S / startup_ariesv3.S"]
        ARIES_SYS["system_ariesv3.c (Clocks, Interrupts)"]
        ARIES_UART["uart_thejas32.c (16550 registers)"]
        ARIES_TIMER["timer_thejas32.c (RISC-V mtime / HW Timers)"]
        ARIES_SPI["spi_thejas32.c"]
        ARIES_I2C["i2c_thejas32.c"]
    end

    subgraph PHYSICAL_SOC["Physical Hardware (THEJAS32 / VEGA ET1031)"]
        HW_CORE["100 MHz RISC-V RV32IM CPU"]
        HW_UART["UART0 / CP2102N"]
        HW_SPI["SPI0 / SPI1 / SPI2 / SPI3"]
        HW_I2C["I2C0 / I2C1 / I2C2"]
        HW_TIM["Hardware Timers 0..2"]
    end

    %% Inter-layer relationships
    MSP_SRV --> TASK_SERIAL
    CLI_SRV --> TASK_SERIAL
    TASK_SERIAL --> DRV_SERIAL
    TASK_GYROPID --> FLIGHT_SUBSYSTEMS
    FLIGHT_SUBSYSTEMS --> BUS_DEVICE_ABSTRACTION
    BUS_DEVICE_ABSTRACTION --> DRIVER_LAYER
    DRIVER_LAYER --> PLATFORM_HAL
    PLATFORM_HAL --> PHYSICAL_SOC
```

---

## 3. The Betaflight Cooperative Task Loop

Betaflight uses a priority-based cooperative task scheduler located in `src/main/scheduler/scheduler.c`.

```mermaid
stateDiagram-v2
    [*] --> SystemInit: Boot from reset (0x200000)
    SystemInit --> HardwareInit: Clocks, PLIC, Timers, UART0
    HardwareInit --> ConfigInit: Load Parameter Groups (pg.c)
    ConfigInit --> SchedulerInit: Register Tasks in taskTable[]
    SchedulerInit --> MainLoop: Enter scheduler loop

    state MainLoop {
        [*] --> CheckRealtimeTask: Is GYRO/PID due?
        CheckRealtimeTask --> ExecuteRealtime: Yes -> Execute immediately
        CheckRealtimeTask --> EvaluateQueue: No -> Check dynamic task queue
        EvaluateQueue --> SelectTask: Find highest priority due task
        SelectTask --> ExecuteTask: Run task->taskFunc()
        ExecuteTask --> CheckRealtimeTask
    }
```

### Essential Tasks for Configurator Communication:
1. `TASK_SYSTEM`: Heartbeat indicator, uptime tracking, CPU load calculation.
2. `TASK_SERIAL`: Polls and drains `UART0` buffers, executes `mspSerialProcess()`, and feeds CLI tokens.

---

## 4. Bus Abstraction Pattern (`extDevice_t` and `busDevice_t`)

As documented in [BusDrivers.md](file:///home/ivan/Desktop/betaflight/knowledgebase/betaflight.com/docs/development/API/BusDrivers.md), Betaflight completely decouples high-level sensor drivers (gyro, accelerometer, compass, barometer, flash memory) from the underlying bus hardware:

```mermaid
classDiagram
    class extDevice_t {
        +busDevice_t* bus
        +uint8_t busType_e
        +uint32_t deviceIndex
        +uint16_t clkDivisor
    }

    class busDevice_t {
        +uint32_t busType
        +void* busHardwareInstance
        +busDeviceInit()
        +busTransfer()
    }

    class SensorDriver {
        +mpu6000Detect(extDevice_t *dev)
        +mpu6000Read(extDevice_t *dev)
    }

    SensorDriver --> extDevice_t : Calls generic busReadRegister()
    extDevice_t --> busDevice_t : Resolves to SPI0 / I2C0
    busDevice_t --> PlatformHAL : Executes hardware transfer
```

### Porting Implication for ARIES v3.0:
To support any sensor or memory chip in Betaflight, we only need to implement the standard `busDevice_t` interface for:
* `bus_spi.c` $\rightarrow$ THEJAS32 SPI controller driver
* `bus_i2c.c` $\rightarrow$ THEJAS32 I2C controller driver
No sensor driver code needs to be modified!
