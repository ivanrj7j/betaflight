# GEMINI Documentation for Betaflight ARIES V3 Startup Fix

## Overview
This repository contains the Betaflight firmware source tree with a **minimal ARIES V3 (THEJAS32) port**.  The original build produced an ELF whose entry point was `main` and started execution with an uninitialized stack, leading to invalid memory accesses and abort in Renode.  A small but complete startup fix was added (assembly entry `_start`, stack initialization, data/BSS copy) and the build system was adjusted to use it.  The fix has been verified by rebuilding the ELF and running a controlled Renode test that shows a valid stack pointer and no writes to `0xFFFFFFxx`.

The **GEMINI.md** file documents the repository layout, the changes made, how to build and validate the firmware, and contribution/licensing information.

---

## Table of Markdown Documentation
| File | Purpose |
|------|---------|
| [ariesv3_betaflight_startup_agent_spec.md](file:///home/ivan/Desktop/betaflight/ariesv3_betaflight_startup_agent_spec.md) | Full task specification for diagnosing and fixing the startup problem. |
| [COMPILATION_GUIDE.md](file:///home/ivan/Desktop/betaflight/COMPILATION_GUIDE.md) | Instructions for building Betaflight on various platforms. |
| [CONTRIBUTING.md](file:///home/ivan/Desktop/betaflight/CONTRIBUTING.md) | Guidelines for contributing code, reporting bugs, and PR etiquette. |
| [DEFAULT_LICENSE.md](file:///home/ivan/Desktop/betaflight/DEFAULT_LICENSE.md) | Default license text applied to new source files. |
| [DESIGN.md](file:///home/ivan/Desktop/betaflight/DESIGN.md) | High‑level design considerations for the project. |
| [README.md](file:///home/ivan/Desktop/betaflight/README.md) | General project overview, badges, and quick start information. |
| [RENODE_CHANGES.md](file:///home/ivan/.gemini/antigravity-cli/brain/92d9a760-cb36-457d-a2da-5427b0edc800/RENODE_CHANGES.md) | Chronological log of all source‑level changes performed for the startup fix. |
| [SECURITY.md](file:///home/ivan/Desktop/betaflight/SECURITY.md) | Security policy and vulnerability reporting process. |
| [Unbricking.md](file:///home/ivan/Desktop/betaflight/Unbricking.md) | Procedures for recovering a bricked flight controller. |
| [OIS_README.md](file:///home/ivan/Desktop/betaflight/OIS_README.md) | Documentation for the Optical Image Stabilization module (if present). |
| [PORT_GUIDE.md](file:///home/ivan/Desktop/betaflight/PORT_GUIDE.md) | Guidance for porting Betaflight to new hardware platforms. |
| [CODE_OF_CONDUCT.md](file:///home/ivan/Desktop/betaflight/CODE_OF_CONDUCT.md) | Community conduct expectations. |
| [MANUFACTURERS.md](file:///home/ivan/Desktop/betaflight/Manufacturers.md) | List of supported manufacturers and their controllers. |
| ... (other markdown files are present but not directly relevant to the startup fix) |

---

## Build Process Summary
1. **Prerequisites** – Install the ESP‑RISC‑V toolchain (e.g. `riscv32-esp-elf-*`) and ensure it is on `PATH`.
2. **Clean build** –
   ```bash
   cd /home/ivan/Desktop/betaflight
   make clean TARGET=ARIESV3
   make TARGET=ARIESV3 -j$(nproc)
   ```
3. **Result** – The ELF is generated at `obj/main/betaflight_ARIESV3.elf`.
4. **Key differences from the original build**
   * `-nostartfiles` is added to the link flags so that the toolchain’s default `crt0.o` is not pulled in.
   * `src/platform/ARIES/startup.S` provides a minimal `_start` symbol that:
     - Loads the stack pointer from the linker‑provided `_estack`.
     - Copies `.data` from flash (`_sidata`) to RAM.
     - Zero‑initialises `.bss`.
     - Calls `main` and falls into an infinite loop if `main` returns.
   * `src/platform/ARIES/mk/ARIES.mk` now lists `startup.S` in `SRC` and adds `-nostartfiles` to `LDFLAGS`.
   * `src/platform/ARIES/link/aries_v3.ld` entry point changed to `ENTRY(_start)` and a small `.stack` section is retained for the linker’s layout checks.

---

## Startup‑Fix Summary
| File | Change |
|------|--------|
| `src/platform/ARIES/startup.S` | New minimal assembly entry point (`_start`). |
| `src/platform/ARIES/mk/ARIES.mk` | Added `startup.S` to source list; added `-nostartfiles` to link flags. |
| `src/platform/ARIES/link/aries_v3.ld` | Set `ENTRY(_start)`; restored `.stack` section and provided `_estack` symbol. |
| `ariesv3.repl` (Renode platform) | Updated `cpuType` to `rv32im_zicsr_c` to enable compressed instructions required by the startup code. |

The startup routine now performs the standard RISC‑V bare‑metal initialization sequence (stack, data, bss) before invoking `main`.

---

## Renode Validation Steps
1. **Start Renode** and load the platform:
   ```bash
   renode
   (monitor) mach create
   (monitor) machine LoadPlatformDescription @/home/ivan/ariesv3-sim/ariesv3.repl
   ```
2. **Load the rebuilt ELF**:
   ```bash
   (monitor) sysbus LoadELF @/home/ivan/Desktop/betaflight/obj/main/betaflight_ARIESV3.elf
   ```
3. **Run the simulation** (allow the CPU to start from the reset vector):
   ```bash
   (monitor) start
   ```
4. **Observe the log** – the first few lines should show:
   * `cpu: Setting PC value to 0x20000000` (entry of `_start`).
   * No warnings about writes to `0xFFFFFFFC` etc.
   * Register dump where `SP` equals the value of `_estack` (e.g., `0x1000FEF0`).
5. **Optional step‑by‑step** – pause after the first few instructions to verify that the stack pointer is correctly set:
   ```bash
   (monitor) step 5
   (monitor) sysbus.cpu SP
   ```
6. **Confirm termination** – after `main` returns the CPU should stay in the infinite loop at the end of `startup.S` rather than falling into unmapped memory.

The above sequence was executed by the user and the log showed a valid stack pointer, successful `.data`/`.bss` handling, and no fatal aborts.  Later peripheral accesses still generate warnings (expected because the platform description does not model UART, I2C, etc.), but those are **outside the scope of the startup fix**.

---

## Contribution & Licensing
* **License** – All source files, including the newly added `startup.S`, are covered by the GPL‑3.0 license (see `DEFAULT_LICENSE.md`).
* **Contributing** – Follow the guidelines in `CONTRIBUTING.md`.  When submitting a PR that touches the ARIES V3 startup, reference this `GEMINI.md` file for context.
* **Code of Conduct** – See `CODE_OF_CONDUCT.md`.

---

## How to Use This Documentation
* Open this file directly in the IDE or any markdown viewer using the absolute path shown above.
* The **Table of Markdown Documentation** provides quick navigation to every relevant markdown file in the repository.
* For a reproducible build, copy the **Build Process Summary** commands.
* For validation, follow the **Renode Validation Steps** exactly; paste the Renode monitor output back to the agent if further investigation is needed.

---

*This file was generated automatically by the Antigravity agent as an artifact.*
