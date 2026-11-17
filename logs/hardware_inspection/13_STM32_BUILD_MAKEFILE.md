# Hardware Inspection Log: `src/platform/STM32/mk/STM32F4.mk`

## 1. File Purpose & Role in Architecture
* **File Path**: `src/platform/STM32/mk/STM32F4.mk`
* **Subsystem**: Platform Make Infrastructure, Toolchain Flags, Vendor SDK Linking, and Per-File Compiler Optimization Rules.
* **Key Components**:
  * Compiler architecture flags: `-mthumb -mcpu=cortex-m4 -march=armv7e-m -mfloat-abi=hard -mfpu=fpv4-sp-d16`.
  * Vendor library switching (`StdPeriph` vs `HAL`).
  * Granular optimization grouping (`SPEED_OPTIMISED_SRC` vs `SIZE_OPTIMISED_SRC`).
  * ST USB Device Library inclusion (Core, CDC, MSC, HID).

---

## 2. Hardware Challenges STM32 Developers Faced & How They Overcame Them

### Challenge 1: Flash Bloat vs Real-Time Execution Speed
* **The Problem**: Compiling the entire Betaflight codebase with `-O3` creates massive binary bloat that overflows 256KB/512KB flash. Conversely, compiling everything with `-Os` (size optimization) degrades the performance of time-critical inner loops (EXTI pin interrupts, gyro sampling, system cycle counting).
* **How It Was Overcome**:
  The build system partitions source files into distinct optimization tiers:
  * `SPEED_OPTIMISED_SRC += common/stm32/system.c STM32/exti.c` $\rightarrow$ Compiled with `-O3` / `-Ofast` for maximum instruction pipeline efficiency.
  * `SIZE_OPTIMISED_SRC += STM32/serial_usb_vcp.c drivers/inverter.c` $\rightarrow$ Compiled with `-Os` to aggressively compress non-time-critical code.

---

### Challenge 2: Heavyweight USB Software Stack
* **The Problem**: Supporting native USB Virtual COM Port (VCP) and Mass Storage Class (MSC) on STM32 requires pulling in over a dozen ST middleware source files (`usbd_core.c`, `usbd_cdc_core.c`, `usbd_msc_bot.c`, `usbd_ioreq.c`), bloating the binary by over 30 KB.
* **How It Was Overcome**:
  The Makefile isolates USB into dedicated modular variables (`VCP_SRC`, `MSC_SRC`) linked conditionally based on target feature flags.

---

## 3. Direct Porting Blueprint for ARIES v3.0 (THEJAS32 / VEGA ET1031)

1. **Massive Code Savings by Omitting USB Stack**:
   * Because ARIES v3.0 routes serial communication to the external Silicon Labs CP2102N chip via `UART0`, we do not need to link any USB device library!
   * This immediately saves ~30 KB of code, giving us a huge advantage in achieving the <128 KB target footprint.
2. **Target Architecture & Compiler Flags for ARIES v3.0**:
   ```makefile
   PLATFORM_SDK := riscv
   CROSS_COMPILE ?= riscv64-unknown-elf-

   ARCH_FLAGS = -march=rv32im -mabi=ilp32 -mcmodel=medlow
   DEVICE_FLAGS = -DTHEJAS32 -DARIESV3 -DUSE_UART

   # Size optimization for <128KB build
   TARGET_CFLAGS = $(ARCH_FLAGS) $(DEVICE_FLAGS) -Os \
                   -ffunction-sections -fdata-sections -nostdlib -nostartfiles
   TARGET_LDFLAGS = -Wl,--gc-sections -T $(PLATFORM_DIR)/link/thejas32_ram.ld
   ```
3. **Integrating [lib/vega-sdk](file:///home/ivan/Desktop/betaflight/lib/vega-sdk)**:
   * Instead of CMSIS/ST HAL, the Makefile will point to `$(ROOT)/lib/vega-sdk/include` for register definitions and peripheral base addresses.
