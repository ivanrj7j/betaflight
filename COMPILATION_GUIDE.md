# C-DAC ARIES v3.0 (THEJAS32 / VEGA ET1031) Betaflight Compilation & Flashing Guide

This beginner-friendly guide walks you step-by-step through compiling Betaflight for the **C-DAC ARIES v3.0** development board and flashing the firmware to the hardware.

---

## 1. Where to Find Your Compiled Binaries

All compilation outputs are generated in the `obj/` folder of the Betaflight repository:

| File Format | File Path | Description / Use Case |
| :--- | :--- | :--- |
| **Intel HEX (`.hex`)** | [`obj/betaflight_2026.12.0-alpha_ARIESV3.hex`](obj/betaflight_2026.12.0-alpha_ARIESV3.hex) | Standard format containing absolute flash addresses (`0x20000000`). Used by most GUI flashers, OpenOCD, and Betaflight Configurator. |
| **Raw Binary (`.bin`)** | [`obj/main/betaflight_ARIESV3.bin`](obj/main/betaflight_ARIESV3.bin) | Pure binary dump (708 KB). Used with direct SPI flash programmers or bootloader write commands at offset `0x20000000`. |
| **ELF Executable (`.elf`)** | [`obj/main/betaflight_ARIESV3.elf`](obj/main/betaflight_ARIESV3.elf) | Executable with full symbol table and debug information. Used for GDB hardware debugging and OpenOCD flashing. |
| **Memory Map (`.map`)** | [`obj/main/betaflight_ARIESV3.map`](obj/main/betaflight_ARIESV3.map) | Detailed breakdown of memory sections and variable addresses. |

---

## 2. Prerequisites & Environment Setup

### 2.1 System Dependencies (Ubuntu / Debian Linux)
Open your terminal and install standard build tools:

```bash
sudo apt update
sudo apt install -y build-essential git python3 make
```

### 2.2 RISC-V Toolchain
The ARIES v3.0 board features the **VEGA ET1031** microprocessor core, which uses the **RV32IM** architecture with 32-bit soft-float ABI (`ilp32`).

You can use the installed ESP-IDF RISC-V toolchain (`riscv32-esp-elf-gcc` v13.2.0) or any standard RISC-V 32-bit toolchain.

* **Installed Toolchain Location:**
  ```text
  ~/.espressif/tools/riscv32-esp-elf/esp-13.2.0_20250707/riscv32-esp-elf/bin/
  ```

* **Verify the Compiler:**
  Run this command in terminal to confirm the compiler is accessible:
  ```bash
  ~/.espressif/tools/riscv32-esp-elf/esp-13.2.0_20250707/riscv32-esp-elf/bin/riscv32-esp-elf-gcc --version
  ```
  Expected output:
  ```text
  riscv32-esp-elf-gcc (crosstool-NG esp-13.2.0_20250707) 13.2.0
  ```

---

## 3. Step-by-Step Compilation

Navigate to your Betaflight repository root:
```bash
cd /path/to/betaflight
```

### 3.1 Clean Previous Builds
To ensure a clean slate, delete any previous build artifacts:
```bash
make TARGET=ARIESV3 clean
```

### 3.2 Build the Firmware
Compile and link the project:
```bash
make TARGET=ARIESV3
```

> [!NOTE]
> The Makefile is pre-configured with the installed toolchain path in `src/platform/ARIES/mk/ARIES.mk`. If compiling on another machine, you can specify the toolchain prefix on the command line:
> ```bash
> make TARGET=ARIESV3 ARM_SDK_PREFIX=/path/to/bin/riscv32-esp-elf-
> ```

### 3.3 Generate All Output Formats (ELF, HEX, BIN)
To create the Intel HEX file ready for flashing:
```bash
make TARGET=ARIESV3 hex
```

To create a raw `.bin` file directly:
```bash
~/.espressif/tools/riscv32-esp-elf/esp-13.2.0_20250707/riscv32-esp-elf/bin/riscv32-esp-elf-objcopy -O binary obj/main/betaflight_ARIESV3.elf obj/main/betaflight_ARIESV3.bin
```

### 3.4 Check Binary Size & Memory Utilization
Run the size inspection tool:
```bash
~/.espressif/tools/riscv32-esp-elf/esp-13.2.0_20250707/riscv32-esp-elf/bin/riscv32-esp-elf-size obj/main/betaflight_ARIESV3.elf
```

Expected output:
```text
   text    data     bss     dec     hex filename
 718592    6336   58928  783856   bf5f0 ./obj/main/betaflight_ARIESV3.elf
```
* **Flash Usage:** ~708 KB / 2,048 KB (34.5% of total Flash)
* **RAM Usage:** ~63.7 KB / 256 KB (24.9% of total SRAM)

---

## 4. Flashing the C-DAC ARIES v3.0 Board

The ARIES v3.0 board features a dual USB interface:
1. **USB-UART Bridge (Silicon Labs CP2102N):** Connects to THEJAS32 UART0 for Betaflight CLI and serial communication (`/dev/ttyUSB0`).
2. **Onboard JTAG Debugger:** Used for programming the external SPI Flash chip at base address `0x20000000`.

### 4.1 USB Permissions (Linux `dialout` Group)
To communicate with the board without `sudo`, add your user to the `dialout` group:
```bash
sudo usermod -a -G dialout $USER
```
*(Log out and log back in for this to take effect).*

---

### Method A: Flashing via OpenOCD & JTAG (Recommended)

1. Connect the ARIES v3.0 board to your PC using a micro-USB cable.
2. Confirm the device is detected:
   ```bash
   lsusb
   dmesg | tail -20
   ```
3. Run OpenOCD using the VEGA / THEJAS32 configuration:
   ```bash
   openocd -f /path/to/aries_openocd.cfg -c "program obj/main/betaflight_ARIESV3.elf verify reset exit"
   ```
   Or if flashing raw binary to Flash:
   ```bash
   openocd -f /path/to/aries_openocd.cfg -c "init; halt; flash write_image erase obj/main/betaflight_ARIESV3.bin 0x20000000; reset run; exit"
   ```

---

### Method B: Flashing via VEGA Flasher / Python Tool

If using the official C-DAC VEGA upload utility (used in the ARIES Arduino core):
```bash
python3 /path/to/vega_flasher.py -p /dev/ttyUSB0 -b 115200 -a 0x20000000 -w obj/main/betaflight_ARIESV3.bin
```

---

## 5. Verifying the Running Firmware

Once flashed, the board will boot Betaflight from Flash `0x20000000`:

1. Open a serial monitor connected to the CP2102N USB port (`/dev/ttyUSB0`) at **115200 baud**:
   ```bash
   picocom -b 115200 /dev/ttyUSB0
   ```
   *(or use `minicom`, `putty`, or Arduino Serial Monitor with newline set to CRLF).*

2. Type `#` or press `Enter` to enter the **Betaflight CLI**:
   ```text
   # version
   # Betaflight / ARIESV3 (ARV3) 2026.12.0-alpha

   # status
   MCU: THEJAS32 / VEGA ET1031 (100MHz)
   System Uptime: 12 seconds
   Current Load: 8%
   ```

3. Type `tasks` to verify the PID flight loop and scheduler tasks are executing at their configured frequencies.

---

## 6. Common Build Questions & Troubleshooting

### Q: Why does the compiler complain about `csrr` opcode?
**A:** Ensure `-march=rv32im_zicsr` is used instead of `-march=rv32im`. In GCC 12 and 13, CSR instructions are part of the `_zicsr` extension.

### Q: Why does `make` say `undefined reference to __atomic_fetch_add_4`?
**A:** The VEGA ET1031 core is RV32IM (no hardware 'A' atomic instruction extension). Betaflight's ARIES platform handles this through an interrupt-safe software wrapper in `src/platform/ARIES/stub_drivers.c`.

### Q: Can I flash this with the Betaflight Configurator GUI?
**A:** Once the UART0 driver is connected to Betaflight's MSP protocol (Phase 3), the Betaflight Configurator can connect to `/dev/ttyUSB0` to configure PIDs, rates, and sensors. Direct USB DFU flashing is not supported because THEJAS32 does not have an on-chip USB peripheral; firmware updates are performed via JTAG or serial bootloader.
