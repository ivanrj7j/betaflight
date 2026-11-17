# Domain Plan 02: Build System & Memory Optimization

> **Component Plan ID**: `PLAN-DOM-02`  
> **Parent Architecture Document**: [`PLAN/00_MASTER_PLAN_OVERVIEW.md`](00_MASTER_PLAN_OVERVIEW.md)  
> **Primary Knowledge References**:
> - [`knowledgebase/codebase_evaluations/13_EVAL_MAKEFILES_ARIES_MK_TARGET_MK.md`](../knowledgebase/codebase_evaluations/13_EVAL_MAKEFILES_ARIES_MK_TARGET_MK.md)
> - [`knowledgebase/codebase_evaluations/14_EVAL_TARGET_H_AND_CONFIG.md`](../knowledgebase/codebase_evaluations/14_EVAL_TARGET_H_AND_CONFIG.md)
> - [`knowledgebase/components/11_TARGET_OPTIMIZATION_BUDGET.md`](../knowledgebase/components/11_TARGET_OPTIMIZATION_BUDGET.md)
> - [`knowledgebase/TARGET_OPTIMIZATION_128KB.md`](../knowledgebase/TARGET_OPTIMIZATION_128KB.md)
> - [`knowledgebase/CRITICAL_KNOWLEDGE_GAPS_AND_ASSUMPTIONS.md`](../knowledgebase/CRITICAL_KNOWLEDGE_GAPS_AND_ASSUMPTIONS.md)

---

## 1. Executive Domain Summary

The build system and target configuration engine forms the protective boundary around the C-DAC ARIES v3.0 (THEJAS32 / VEGA ET1031) port. It is directly responsible for translating Betaflight 4.x source code into a functional binary that strictly complies with the physical silicon reality:
1. **0 Compressed Instructions**: The core is strictly RV32IM. The build system must isolate the toolchain's uncompressed multilib libraries (`rv32i_zicsr_zifencei/ilp32`) so standard routines (`libc`, `libgcc`) do not introduce 16-bit compressed instructions.
2. **Execution Inside 248 KB SRAM (`0x00202000` to `0x0023FFFF`)**: Because external SPI flash lacks memory-mapped XIP capabilities on physical silicon, the entire `.text`, `.rodata`, `.data`, `.bss`, heap, and stack must fit within the 250 KB internal SRAM.
3. **Dead-Code Elimination & Modular Stripping**: By applying `-ffunction-sections`, `-fdata-sections`, `-Wl,--gc-sections`, and surgical `MCU_EXCLUDES` pruning of graphical OSD, CMS, VTX, SDCard, and complex navigation drivers, the binary size is compressed from $> 800\text{ KB}$ down to $\approx 235\text{ KB}$.
4. **Compile-Time Safety & Assertion Guards**: Enforces parameter storage bounds (`EEPROM_SIZE 8192`) in RAM to prevent the fatal `FAILURE_CONFIG_STORE_FAILURE` boot panic.

---

## 2. Microcomponent Decomposition

```
PLAN/02_BUILD_SYSTEM_AND_MEMORY_OPTIMIZATION.md
 ├── Microcomponent 02_A: Dynamic Toolchain Detection & Multilib RVC Isolation Engine (ARIES.mk)
 ├── Microcomponent 02_B: Target Makefile Compilation Hierarchy & Subsystem Exclusion (target.mk)
 ├── Microcomponent 02_C: Target Header Architecture & Silicon Boundary Enforcement (target.h, config.h)
 ├── Microcomponent 02_D: Parameter Group Budgeting, RAM Storage & Compile-Time Static Assertions
 └── Microcomponent 02_E: Binary Size Auditing, Opcode Verification Pipeline & Artifact Generation
```

---

### Microcomponent 02_A: Dynamic Toolchain Detection & Multilib RVC Isolation Engine

#### 1. Why (Silicon Necessity & Architectural Traceability)
* **The Multilib Trap**: The VEGA ET1031 processor core is RV32IM. It does not implement the standard RISC-V Compressed Instruction extension ('C'). Any execution of a 16-bit opcode raises an unhandled Illegal Instruction Trap (`mcause = 2`).
* **Toolchain Defaults**: Modern cross-compilers (such as `riscv32-esp-elf-gcc` or `riscv-none-elf-gcc` 13+) default to multilib variants containing compressed code (`rv32imafc`) unless specifically directed. Standard runtime helpers like 64-bit integer division (`__divsi3`, `__udivdi3` in `libgcc.a`) and memory routines (`memset`, `memcpy` in `libc.a`) will silently crash the CPU if pulled from compressed multilib archives.
* **Environment Portability**: Previous iterations hardcoded workstation paths (`/home/ivan/...`) in [`src/platform/ARIES/mk/ARIES.mk`](../src/platform/ARIES/mk/ARIES.mk#L3-L9), breaking builds in CI/CD containers or across different developer machines.

#### 2. How (Detailed Algorithmic & Makefile Logic)
1. **Dynamic Toolchain Discovery**:
   - Query the host system `PATH` for available RISC-V toolchains in order of priority: `riscv32-esp-elf-gcc`, `riscv-none-elf-gcc`, `riscv64-unknown-elf-gcc`.
   - If not found on `PATH`, allow an environment override `RISCV_TOOLCHAIN_PATH`.
   - Derive the binary prefix (`CROSS_COMPILE`) and root directory dynamically.
2. **Multilib Querying via GCC `-print-file-name`**:
   - Instead of hardcoding paths, use GCC's built-in multilib query engine with the target flags:
     ```make
     TOOLCHAIN_RV32I_LIB := $(shell $(CC) -march=rv32i_zicsr -mabi=ilp32 -print-file-name=libc.a | xargs dirname)
     TOOLCHAIN_RV32I_LIBGCC := $(shell $(CC) -march=rv32i_zicsr -mabi=ilp32 -print-file-name=libgcc.a | xargs dirname)
     ```
   - This automatically discovers the exact host filesystem directory containing pure uncompressed 32-bit RISC-V library archives.
3. **Compiler Flag Architecture (GCC 13+ ISA Specification)**:
   - `-march=rv32im_zicsr -mabi=ilp32`: Mandates pure 32-bit integer instructions with hardware multiply/divide ('M') and standard CSR manipulation ('_zicsr').
   - `-fno-builtin-memcpy -fno-builtin-memset`: Blocks the compiler from emitting inline compressed patterns or replacing custom routines with compressed libc calls.
   - `-Os -flto -ffunction-sections -fdata-sections -fno-common -fomit-frame-pointer`: Optimizes aggressively for binary footprint, isolates symbols for linker garbage collection, and releases register `s0/fp` for register allocation.
4. **Linker Flag Architecture**:
   - `-nostartfiles -static -Wl,--gc-sections -Wl,--cref`: Guarantees that only reachable code enters the binary.
   - Explicit library search ordering: `-L$(TOOLCHAIN_RV32I_LIBGCC) -L$(TOOLCHAIN_RV32I_LIB) -lgcc -lc -lm`.

#### 3. Expected Inputs
* **Host Toolchain**: `riscv32-esp-elf-gcc` (Espressif 13.2.0) or `riscv-none-elf-gcc` installed and present on `PATH`.
* **Architecture Targets**: `rv32im_zicsr` / `ilp32`.
* **Configuration Files**: `src/platform/ARIES/mk/ARIES.mk`.

#### 4. Expected Results
* Dynamic discovery executes with 0 hardcoded paths.
* Multilib paths point exclusively to `rv32i_zicsr_zifencei/ilp32`.
* Compiler flags strictly enforce `-march=rv32im_zicsr -mabi=ilp32`.
* Linking completes without compressed opcode leakage.

---

### Microcomponent 02_B: Target Makefile Compilation Hierarchy & Subsystem Exclusion

#### 1. Why (Silicon Necessity & Architectural Traceability)
* **248 KB SRAM Ceiling**: Physical internal SRAM on THEJAS32 is 250 KB (`0x00200000` to `0x0023E7FF`), with execution starting at `0x00202000` (effective size 248 KB).
* **Upstream Bloat**: An unpruned Betaflight 4.x target compiles to $> 850\text{ KB}$, far exceeding available SRAM.
* **Non-Existent Peripherals**: ARIES v3.0 lacks on-chip USB OTG (VCP is replaced by CP2102N UART), MAX7456 analog OSD, SPI SD card, Barometer, Magnetometer, and GPS hardware. Compiling drivers for these components wastes $> 200\text{ KB}$ of memory.

#### 2. How (Detailed Algorithmic & Makefile Logic)
1. **Source Tree Inclusion in `target.mk`**:
   - Specify platform directories: `SRC_DIR += src/platform/ARIES/`.
   - Include core device drivers: `serial_uart_aries.c`, `io_aries.c`, `exti_aries.c`, `bus_spi_aries.c`, `pwm_motor_aries.c`, `system.c`, `stub_drivers.c`.
2. **Aggressive Dead-Code Pruning via `MCU_EXCLUDES`**:
   - In [`src/platform/ARIES/target/ARIESV3/target.mk`](../src/platform/ARIES/target/ARIESV3/target.mk), define explicit exclusion patterns:
     ```make
     MCU_EXCLUDES += \
         drivers/display \
         io/displayport \
         io/osd \
         io/cms \
         io/vtx \
         drivers/vtx \
         drivers/barometer \
         drivers/compass \
         drivers/sdcard \
         io/flashfs \
         io/asyncfatfs \
         flight/failsafe_gps \
         navigation
     ```
3. **Linker Script Binding**:
   - Explicitly point the linker to the memory-bounded script:
     `LD_SCRIPT = $(PLATFORM_DIR)/target/ARIESV3/aries_v3.ld`.

#### 3. Expected Inputs
* **Source Tree**: Betaflight 4.x core source tree (`src/main/`).
* **Target Makefile**: `src/platform/ARIES/target/ARIESV3/target.mk`.
* **Exclusion Targets**: Graphical OSD, CMS, VTX, SDCard, GPS, Barometer, Magnetometer.

#### 4. Expected Results
* Excluded driver source files are completely omitted from the compile list.
* Object file generation strictly restricted to core flight, scheduler, MSP, and ARIES HAL drivers.
* Intermediate compilation artifacts stay within bounds.

---

### Microcomponent 02_C: Target Header Architecture & Silicon Boundary Enforcement

#### 1. Why (Silicon Necessity & Architectural Traceability)
* **Soft-Float Timing Constraints**: The VEGA ET1031 is an integer-only core running at 100 MHz. Betaflight's PID loop executes floating-point Butterworth filters, PT1 filters, D-term derivation, and attitude math via software emulation. Running at 8 kHz or 4 kHz requires $> 350\%$ CPU capacity, causing complete flight loop stall and watchdog resets.
* **Pin Multiplexing Isolation**: The target header must configure a collision-free 12-pin mapping for the board's headers (J1 & J2).
* **Missing Hardware Isolation**: Defining `#undef USE_ADC` or `#undef USE_VCP` ensures higher-level code does not attempt to poll non-existent hardware.

#### 2. How (Detailed Algorithmic & Header Configuration)
1. **Flight Loop Clamping**:
   - In [`src/platform/ARIES/target/ARIESV3/target.h`](../src/platform/ARIES/target/ARIESV3/target.h):
     ```c
     #define DEFAULT_PID_PROCESS_DENOM 8 // Clamps 8 kHz gyro to 1.0 kHz PID loop
     ```
   - Clamping to 1.0 kHz allocates $1000\,\mu\text{s}$ per cycle. Soft-float math takes $\approx 290\,\mu\text{s}$ to $420\,\mu\text{s}$, leaving $> 58\%$ idle CPU headroom.
2. **Zero-Conflict 12-Pin Allocation Matrix**:
   - **Header J1**:
     - `PA5`: SPI0 SCK
     - `PA6`: SPI0 MOSI
     - `PA7`: SPI0 MISO
     - `PA8`: SPI0 CS (ICM-42688P)
     - `PA0`: Gyro EXTI DRDY
     - `PA4`: Motor 4 PWM
   - **Header J2**:
     - `PA1`: Motor 1 PWM
     - `PA2`: Motor 2 PWM
     - `PA3`: Motor 3 PWM
   - **Onboard Peripherals**:
     - `PA30`: UART0 TX (CP2102N USB Bridge)
     - `PA31`: UART0 RX (CP2102N USB Bridge)
     - `PA16`: UART1 TX (CRSF / ELRS Receiver)
     - `PA17`: UART1 RX (CRSF / ELRS Receiver)
     - `PA22`: LED0 (Status Indicator)
3. **Comprehensive Feature Exclusion Guard**:
   - Explicit `#undef` of: `USE_VCP`, `USE_DMA`, `USE_OSD`, `USE_CMS`, `USE_VTX_CONTROL`, `USE_BARO`, `USE_MAG`, `USE_GPS`, `USE_BLACKBOX`, `USE_FLASHFS`.
4. **ADC Fallback Handling**:
   - When `#undef USE_ADC` is active, battery voltage telemetry must default to `BATTERY_NOT_PRESENT` to prevent spurious alarms on EdgeTX/OpenTX radios.

#### 3. Expected Inputs
* **Header Files**: `src/platform/ARIES/target/ARIESV3/target.h`, `config.h`.
* **Hardware Specs**: 100 MHz soft-float core, J1/J2 pinout layout.

#### 4. Expected Results
* Flight loop rate clamped to exactly 1.0 kHz.
* Zero pin conflicts across headers J1, J2, and onboard transceivers.
* Heavy peripheral drivers eliminated from build tree.

---

### Microcomponent 02_D: Parameter Group Budgeting, RAM Storage & Compile-Time Static Assertions

#### 1. Why (Silicon Necessity & Architectural Traceability)
* **Non-XIP Flash Limitation**: Physical SPI flash is not memory-mapped on THEJAS32. Attempting to point Betaflight's Parameter Group structures to raw flash addresses causes fatal Load/Store Traps (`mcause = 5/7`).
* **The 4 KB vs 8 KB Buffer Overflow Hazard**: Active Parameter Groups in Betaflight 4.x (PIDs, gyro filters, RC rates, mixer, failsafe) occupy $\approx 5.1\text{ KB}$. Defining `EEPROM_SIZE 4096` triggers a buffer size panic (`FAILURE_CONFIG_STORE_FAILURE`) on boot.
* **SRAM Safety**: We must allocate exactly 8 KiB (`EEPROM_SIZE 8192`) in RAM via `#define CONFIG_IN_RAM`.
* **Static Verification**: A compile-time static assertion is mandatory to prevent accidental reduction of `EEPROM_SIZE`.

#### 2. How (Detailed Algorithmic & Code Guard Implementation)
1. **Config in RAM Definition**:
   - In `target.h`:
     ```c
     #define CONFIG_IN_RAM
     #define EEPROM_SIZE 8192
     ```
2. **Compile-Time Static Assertion Guard**:
   - In `src/platform/ARIES/target/ARIESV3/config.h`:
     ```c
     #if !defined(EEPROM_SIZE) || (EEPROM_SIZE < 8192)
     #error "FATAL BUILD ERROR: EEPROM_SIZE must be >= 8192 bytes for Betaflight 4.x Parameter Groups!"
     #endif
     ```
3. **Parameter Group Allocation Monitoring**:
   - Add a build-time script or check that inspects `pg_registry` symbol sizes in the generated ELF cross-reference map (`.map`) to ensure total PG definitions remain $< 7.5\text{ KB}$, retaining at least 512 bytes of headroom inside the 8 KB RAM buffer.

#### 3. Expected Inputs
* **Configuration Headers**: `target.h`, `config.h`.
* **Parameter Group Registry**: `src/main/common/streambuf.h`, `src/main/pg/pg.h`.

#### 4. Expected Results
* Build fails immediately with an informative `#error` if `EEPROM_SIZE` is altered below 8192.
* Runtime configuration stores into 8 KB SRAM buffer with zero memory traps.
* Zero boot panics on initial configuration load.

---

### Microcomponent 02_E: Binary Size Auditing, Opcode Verification Pipeline & Artifact Generation

#### 1. Why (Silicon Necessity & Architectural Traceability)
* **Ground Truth 1 (Strictly 0 RVC Opcodes)**: A single 16-bit compressed instruction will halt the processor (`mcause = 2`).
* **Ground Truth 2 (Total Binary $\le 248\text{ KB}$)**: Firmware executes entirely in SRAM (`0x00202000` to `0x0023FFFF`). Any overflow overwrites the top of SRAM or corrupts runtime stacks.
* **Pre-Flashing Guarantee**: Automated verification during compilation prevents bricking or silent boot failures.

#### 2. How (Detailed Algorithmic & Verification Pipeline)
1. **Binary Size Enforcement in Makefile Post-Link**:
   - Execute `size -B` on the resulting ELF file:
     ```bash
     $(SIZE) -B $(TARGET_ELF)
     ```
   - Calculate total memory footprint: $\text{Footprint} = \text{text} + \text{data} + \text{bss}$.
   - Add Makefile assertion script:
     ```make
     @TOTAL_SIZE=$$( $(SIZE) -B $(TARGET_ELF) | awk 'NR==2 {print $$1 + $$2 + $$3}' ); \
     if [ $$TOTAL_SIZE -gt 253952 ]; then \
         echo "ERROR: Firmware size ($$TOTAL_SIZE bytes) exceeds 248 KB SRAM limit!"; \
         exit 1; \
     else \
         echo "SUCCESS: Firmware size ($$TOTAL_SIZE bytes) fits within 248 KB SRAM budget."; \
     fi
     ```
2. **Automated RVC Instruction Audit (`grep c.`)**:
   - Disassemble the entire ELF image:
     ```bash
     $(OBJDUMP) -d $(TARGET_ELF) > $(BUILD_DIR)/disasm.s
     ```
   - Audit for compressed opcodes (RISC-V compressed instructions begin with `c.`):
     ```bash
     grep -E "\sc\.[a-z]" $(BUILD_DIR)/disasm.s | wc -l
     ```
   - Mandate that the count strictly equals **0**. If greater than 0, fail the build immediately and print the offending function names and object files.
3. **Artifact Generation**:
   - Emit raw binary: `$(OBJCOPY) -O binary $(TARGET_ELF) $(TARGET_BIN)`.
   - Generate full cross-reference memory map: `-Wl,-Map,$(BUILD_DIR)/$(TARGET).map`.

#### 3. Expected Inputs
* **Compiled ELF**: `$(BUILD_DIR)/betaflight_ARIESV3.elf`.
* **Disassembly & Size Utilities**: `riscv32-esp-elf-size`, `riscv32-esp-elf-objdump`, `riscv32-esp-elf-objcopy`.

#### 4. Expected Results
* `grep -E "\sc\.[a-z]"` returns strictly **0**.
* Total footprint is verified $\le 248\text{ KB}$ ($\le 253,952\text{ bytes}$).
* Raw binary `.bin` ready for XMODEM loading to SRAM at `0x00202000`.

---

## 3. Subsystem Traceability Matrix

| Microcomponent ID | Functionality / Feature | Target Source Files | Silicon Verification Target |
| :--- | :--- | :--- | :--- |
| **02_A** | Dynamic Toolchain & Multilib RVC Isolation | `src/platform/ARIES/mk/ARIES.mk` | GCC `-print-file-name` discovers `rv32i_zicsr_zifencei/ilp32`; 0 compressed instructions. |
| **02_B** | Modular Stripping & Dead-Code Elimination | `src/platform/ARIES/target/ARIESV3/target.mk` | `MCU_EXCLUDES` prunes $> 500\text{ KB}$ of unused drivers; total binary $< 248\text{ KB}$. |
| **02_C** | Target Header & 1.0 kHz Loop Clamping | `src/platform/ARIES/target/ARIESV3/target.h` | `DEFAULT_PID_PROCESS_DENOM 8` guarantees $> 58\%$ idle CPU headroom. |
| **02_D** | RAM Config & Static Assertions | `src/platform/ARIES/target/ARIESV3/config.h` | `#error` triggered if `EEPROM_SIZE < 8192`; 0 boot panics. |
| **02_E** | Automated Sizing & Disassembly Audit | `Makefile`, post-build scripts | Automated check asserts $\text{Size} \le 248\text{ KB}$ and `grep c.` equals 0. |

---

## 4. Anti-Assumption Guardrails

1. **Host Environment Independence**:
   - *Never* assume the toolchain lives in `/home/ivan/`. All Makefile targets must resolve via `PATH` with fallback overrides.
2. **No Compressed Multilibs**:
   - *Never* allow standard `-lc` or `-lgcc` to resolve to default toolchain multilibs without verifying via `-print-file-name`.
3. **Parameter Group Allocation**:
   - *Never* assume Parameter Groups will fit in 4096 bytes. Betaflight 4.x structures require $\approx 5.1\text{ KB}$, so `EEPROM_SIZE 8192` is an inviolable requirement.
4. **Execution Ceiling**:
   - *Never* assume external flash can execute code via XIP. The binary size ceiling is strictly **248 KB**, dictated by internal SRAM.