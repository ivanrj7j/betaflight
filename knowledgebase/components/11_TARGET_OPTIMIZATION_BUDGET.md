# Porting Component Knowledge Node: Target Optimization & 128KB Memory Budget
**Component ID**: `PORT-COMP-11`  
**Parent Knowledge Graph**: [PORTING_KNOWLEDGE_GRAPH.md](file:///home/ivan/Desktop/betaflight/knowledgebase/PORTING_KNOWLEDGE_GRAPH.md)  
**Primary References**:
* [TARGET_OPTIMIZATION_128KB.md](file:///home/ivan/Desktop/betaflight/knowledgebase/TARGET_OPTIMIZATION_128KB.md) (Target optimization strategy under 128 KB)
* [13_STM32_BUILD_MAKEFILE.md](file:///home/ivan/Desktop/betaflight/logs/hardware_inspection/13_STM32_BUILD_MAKEFILE.md) (Multi-tier optimization `-O3` vs `-Os` & modular stripping)
* [25_ESP32_BUILD_AND_LINKER.md](file:///home/ivan/Desktop/betaflight/logs/hardware_inspection/25_ESP32_BUILD_AND_LINKER.md) (`dma_stub` mechanics & RISC-V sizing)
* [THEJAS32_PROGRAMMER_MANUAL_ANALYSIS.md](file:///home/ivan/Desktop/betaflight/knowledgebase/THEJAS32_PROGRAMMER_MANUAL_ANALYSIS.md) (250 KB internal SRAM constraint)

---

## 1. Architectural Goal & Constraint Analysis

The primary objective for the initial ARIES v3.0 port is compiling a fully modular, stable Betaflight firmware image under **128 KB** capable of executing completely inside the **250 KB Internal SRAM** (`0x00200000` to `0x0023E7FF`) and establishing a high-speed telemetry connection with Betaflight Configurator:

```
+-----------------------------------------------------------------------+
|              TOTAL INTERNAL SRAM CAPACITY: 250 KB (256,000 Bytes)     |
+------------------------------------+----------------------------------+
|  FIRMWARE BINARY IMAGE (~72 KB)    |    FREE RUNTIME SRAM (~178 KB)   |
|  - .text (Code):        ~56 KB     |    - Dynamic Heap Buffer         |
|  - .rodata (Strings):   ~12 KB     |    - Stack Space (_Min_Stack = 8KB)|
|  - .data (Initialized):  ~2 KB     |    - Ring Buffers & Caches       |
|  - .bss (Zeroed Vars):   ~2 KB     |    - Hot Reboot Reserved Flags   |
+------------------------------------+----------------------------------+
```

---

## 2. Modular Feature Stripping Breakdown

Backed directly by [TARGET_OPTIMIZATION_128KB.md](file:///home/ivan/Desktop/betaflight/knowledgebase/TARGET_OPTIMIZATION_128KB.md) and [13_STM32_BUILD_MAKEFILE.md](file:///home/ivan/Desktop/betaflight/logs/hardware_inspection/13_STM32_BUILD_MAKEFILE.md):

| Excluded Feature / Subsystem | Configuration Macro Omitted | Flash/Code Savings | Rationale & Architectural Alternative |
| :--- | :--- | :---: | :--- |
| **Native ST USB Middleware** | `-DUSE_VCP` omitted | **~30 KB** | Hardware CP2102N USB-UART bridge on pins 107/108 handles USB natively. |
| **GPS & Navigation Engine** | `-DUSE_GPS` omitted | **~35 KB** | Not required for bench bringup or initial Configurator handshake. |
| **OSD Screen Rendering** | `-DUSE_OSD` omitted | **~28 KB** | No analog MAX7456 or digital HD OSD chip present on ARIES v3.0. |
| **Blackbox & FATFS Logging** | `-DUSE_BLACKBOX` omitted | **~22 KB** | Eliminates SDCard SPI / FAT32 filesystem code and buffers. |
| **General DMA Machinery** | `-DUSE_DMA` omitted (`dma_stub`) | **~6 KB** | All peripherals (UART0, SPI, I2C, ADC) operate cleanly in polled mode. |
| **Advanced Gyro Filter Math** | Dynamic notch, RPM filters omitted | **~14 KB** | Simple biquad filtering suffices for basic flight control. |
| **Total Memory Savings** | | **~135 KB** | **Reduces standard 220 KB binary to ~72 KB!** |

---

## 3. Included Essential Subsystems

| Included Subsystem | Purpose & Functionality | Estimated Footprint |
| :--- | :--- | :---: |
| **Cooperative Scheduler** | Task timing, non-preemptive main loop (`scheduler.c`) | ~6 KB |
| **Parameter Group Engine** | Runtime settings storage and reset defaults (`pg.c`) | ~10 KB |
| **MSP Protocol Parser** | MultiWii Serial Protocol v1/v2 decoder (`msp_serial.c`) | ~18 KB |
| **THEJAS32 HAL Drivers** | UART0, System Clocks, Flat GPIO, 8-Ch PWM, ADC cache | ~14 KB |
| **Motor Mixer & VTable** | Motor direction and speed testing sliders (`motor.c`) | ~8 KB |
| **C Runtime & Soft-Float Math**| Soft-float emulation (`__muldf3`, etc.) & libc primitives | ~16 KB |
| **Total Estimated Footprint** | | **~72 KB** |

---

## 4. Compiler & Linker Optimization Directives

File: `src/main/target/ARIESV3/target.mk`

```makefile
# Target Architecture Flags for RISC-V RV32IM Soft-Float
ARCH_FLAGS = -march=rv32im -mabi=ilp32

# Multi-Tier Size Optimization Flags
CFLAGS += $(ARCH_FLAGS)
CFLAGS += -Os                     # Optimize strictly for binary size
CFLAGS += -flto                   # Link-Time Optimization (inlines cross-file functions)
CFLAGS += -ffunction-sections     # Place each function into its own ELF section
CFLAGS += -fdata-sections         # Place each global variable into its own section
CFLAGS += -fno-common             # Place uninitialized globals in .bss
CFLAGS += -fomit-frame-pointer    # Free up 's0/fp' register for general allocation

# Linker Optimization & Dead-Code Elimination
LD_FLAGS += $(ARCH_FLAGS)
LD_FLAGS += -nostartfiles
LD_FLAGS += -static
LD_FLAGS += -Wl,--gc-sections     # Strip all unreferenced functions and variables!
LD_FLAGS += -Wl,--cref            # Output cross-reference map
LD_FLAGS += -Wl,-Map,$(TARGET_MAP)
```

---

## 5. Board Configuration Header: `src/main/target/ARIESV3/target.h`

```c
#pragma once

#define TARGET_BOARD_IDENTIFIER "AR30"
#define USBD_PRODUCT_STRING     "ARIES v3.0 Flight Controller"

// Clock Configuration
#define SYSTEM_CLOCK_FREQ_HZ    100000000ULL // 100 MHz

// Serial Ports
#define USE_UART0
#define UART0_RX_PIN            108
#define UART0_TX_PIN            107
#define SERIAL_PORT_COUNT       1

// Motors (Standard PWM / OneShot125 via dedicated 8-ch hardware PWM)
#define USE_MOTOR
#define USE_PWM_OUTPUT
#define MAX_SUPPORTED_MOTORS    4

// Status Indicator
#define USE_LED
#define LED0_PIN                0 // GPIO0 (Pin 25)

// Features explicitly stripped to guarantee <128KB binary
#undef USE_VCP
#undef USE_DMA
#undef USE_GPS
#undef USE_OSD
#undef USE_BLACKBOX
#undef USE_TRANSPONDER
#undef USE_FLASHFS
```

---

## 6. Verification Checklist
- [ ] Compiler produces `target.bin` with file size $< 100\,\text{KB}$ (well below the 128 KB ceiling).
- [ ] XMODEM transfer of binary into SRAM at `0x00200000` finishes in $<15\,\text{seconds}$ at 115,200 baud.
- [ ] Firmware boots and runs entirely within internal SRAM without accessing external flash.
