# Evaluation Node 13: Build System Architecture (`ARIES.mk` & `target.mk`)

## 1. Executive Summary & File Traceability
- **Evaluated Files**:
  - [`src/platform/ARIES/mk/ARIES.mk`](file:///home/ivan/Desktop/betaflight/src/platform/ARIES/mk/ARIES.mk)
  - [`src/platform/ARIES/target/ARIESV3/target.mk`](file:///home/ivan/Desktop/betaflight/src/platform/ARIES/target/ARIESV3/target.mk)
- **Source Log**: [`logs/code_critique/13_MAKEFILES_ARIES_MK_TARGET_MK.md`](file:///home/ivan/Desktop/betaflight/logs/code_critique/13_MAKEFILES_ARIES_MK_TARGET_MK.md)
- **Upstream References**:
  - [`Makefile`](file:///home/ivan/Desktop/betaflight/Makefile)
  - [`lib/vega-sdk/vega-sdk/bsp/common/config.mk`](file:///home/ivan/Desktop/betaflight/lib/vega-sdk/vega-sdk/bsp/common/config.mk)
- **Domain**: Cross-compilation flags, multilib library paths, GCC 13+ ISA specifications, dead-code elimination, SRAM budget enforcement.

---

## 2. Decision & Action Scoring Rubric (0 to 10)

| Decision / Action | Score (0-10) | Evaluation Rationale |
| :--- | :---: | :--- |
| **Multilib RVC Override (`TOOLCHAIN_RV32I_LIB`)** | 10/10 | **Masterstroke engineering fix**. Espressif's default GCC toolchain links standard libraries built with compressed ('C') instructions (`rv32imafc`), which instantly crash the VEGA ET1031 (an RV32IM core). Explicitly overriding to `rv32i_zicsr_zifencei/ilp32` solved this fatal trap. |
| **GCC 13+ ISA Migration (`-march=rv32im_zicsr`)** | 10/10 | Correctly handles the architectural separation of CSR instructions into the `_zicsr` extension mandated in GCC 13+. |
| **No-Builtin Memory Flags (`-fno-builtin-memcpy/memset`)** | 9/10 | Prevents the compiler from substituting inline loops with calls to toolchain `libc` memset/memcpy routines that contain 16-bit compressed instructions. |
| **Aggressive Driver Pruning (`MCU_EXCLUDES`)** | 10/10 | Pruning OSD, CMS, VTX, SDCard, and unused baro/mag drivers shrank the final binary from $> 800\text{ KB}$ down to $\approx 235\text{ KB}$, enabling the firmware to fit inside the 248 KB SRAM execution budget. |
| **Hardcoded Toolchain Path (`/home/ivan/...`)** | 4/10 | Fragile across environments; fails to compile if executed under another username, inside a Docker container, or on CI runners unless paths are manually updated. |

---

## 3. Deep-Dive Critique

### What Went Wrong
1. **Hardcoded User Environment Paths**:
   - In [`src/platform/ARIES/mk/ARIES.mk`](file:///home/ivan/Desktop/betaflight/src/platform/ARIES/mk/ARIES.mk#L3-L9):
     ```make
     ARM_SDK_PREFIX ?= /home/ivan/.espressif/tools/riscv32-esp-elf/esp-13.2.0_20250707/riscv32-esp-elf/bin/riscv32-esp-elf-
     TOOLCHAIN_RV32I_LIB := /home/ivan/.espressif/tools/riscv32-esp-elf/esp-13.2.0_20250707/riscv32-esp-elf/riscv32-esp-elf/lib/rv32i_zicsr_zifencei/ilp32
     TOOLCHAIN_RV32I_LIBGCC := /home/ivan/.espressif/tools/riscv32-esp-elf/esp-13.2.0_20250707/riscv32-esp-elf/lib/gcc/riscv32-esp-elf/13.2.0/rv32i_zicsr_zifencei/ilp32
     ```
   - While effective for local development on the author's workstation, this prevents headless CI builds and team collaboration without manual Makefile editing.

### What Could Have Been Done Better
1. **Dynamic Toolchain Path Detection**:
   - Query `PATH` first using `which riscv32-esp-elf-gcc` or `riscv-none-elf-gcc`, and dynamically derive the library subpaths:
     ```make
     TOOLCHAIN_BIN ?= $(shell which riscv32-esp-elf-gcc 2>/dev/null)
     ifneq ($(TOOLCHAIN_BIN),)
         TOOLCHAIN_ROOT := $(patsubst %/bin/riscv32-esp-elf-gcc,%,$(TOOLCHAIN_BIN))
         ARM_SDK_PREFIX := $(TOOLCHAIN_ROOT)/bin/riscv32-esp-elf-
         TOOLCHAIN_RV32I_LIB := $(shell $(TOOLCHAIN_BIN) -march=rv32i_zicsr -mabi=ilp32 -print-file-name=libc.a | xargs dirname)
         TOOLCHAIN_RV32I_LIBGCC := $(shell $(TOOLCHAIN_BIN) -march=rv32i_zicsr -mabi=ilp32 -print-file-name=libgcc.a | xargs dirname)
     endif
     ```
   - GCC's built-in `-print-file-name` automatically resolves the exact multilib directory regardless of host operating system or installation prefix!

### What Were the Good / Brilliant Parts
1. **The Multilib RVC Isolation**:
   - The VEGA ET1031 silicon implements the `RV32IM` instruction set without Compressed instructions ('C').
   - When using modern Espressif or standard RISC-V toolchains, standard C runtime functions (like integer division `__divsi3` in `libgcc.a` or string routines in `libc.a`) default to `rv32imafc`.
   - By explicitly pinning `TOOLCHAIN_RV32I_LIB` and `TOOLCHAIN_RV32I_LIBGCC` to the pure uncompressed `rv32i_zicsr_zifencei/ilp32` multilib folders, the build system guaranteed that not a single compressed 16-bit instruction slipped into the binary.
2. **Surgical SRAM Footprint Optimization**:
   - The `MCU_EXCLUDES` list in `target.mk` excluded bloated graphical subsystems (CMS, OSD, MAX7456), blackbox sdcard logging, and extraneous sensor protocols. This reduced the compiled footprint by over $70\%$, allowing full quadcopter stabilization, PID filtering, and MSP telemetry to run entirely in SRAM ($< 248\text{ KB}$).
