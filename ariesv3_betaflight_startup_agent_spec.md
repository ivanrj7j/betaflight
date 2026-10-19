# Agent Task Specification: Diagnose and Fix Betaflight Startup in Renode for ARIES V3

**Project:** Betaflight port to ARIES V3 / VEGA THEJAS32 RISC-V
platform\
**Task scope:** Fix the current bare-metal startup/entry problem so the
firmware begins execution with a valid machine state in Renode.\
**Current status:** Experimental bare-minimum port compiles and links.
This is not a completed or hardware-validated port. Several platform
drivers are stubs.\
**Primary target:** `obj/main/betaflight_ARIESV3.elf`\
**Repository path observed in the user's environment:**
`~/Desktop/betaflight`\
**Execution environment:** Ubuntu + Renode portable installation.

------------------------------------------------------------------------

## 1. Objective

Investigate and correct the firmware startup path for the existing
Betaflight `ARIESV3` build. The current ELF has `main` as its entry
point, and Renode begins executing it with an uninitialized stack
pointer. This produces writes to unmapped addresses and eventually an
attempt to execute code at address `0x00000000`, after which Renode
aborts.

The agent must determine the actual cause from the repository, linker
script, ELF metadata, disassembly, and Renode behavior; implement the
smallest correct startup fix; rebuild; and provide reproducible
validation steps and evidence.

**Do not claim that Betaflight boots fully or that the ARIES V3 port is
complete merely because the ELF builds or a few instructions execute.**
The immediate deliverable is a correct, testable startup path and a
precise account of what has and has not been validated.

## 2. Target architecture and memory map

Current build configuration reported by the user:

-   Architecture: RISC-V 32-bit.
-   ISA flags: `-march=rv32im_zicsr -mabi=ilp32 -mcmodel=medany`.
-   Toolchain prefix:
    `/home/ivan/.espressif/tools/riscv32-esp-elf/esp-13.2.0_20250707/riscv32-esp-elf/bin/riscv32-esp-elf-`
-   GCC version reported previously: 13.2.0.
-   Flash map used by the current linker script and Renode platform:
    -   Base: `0x20000000`
    -   Size: 2 MiB (`0x200000`)
    -   End, exclusive: `0x20200000`
-   SRAM map used by the current linker script and Renode platform:
    -   Base: `0x10000000`
    -   Size: 256 KiB (`0x40000`)
    -   End, exclusive: `0x10040000`

**Important:** These are the current project configuration values, not
independent proof of the physical chip's boot mapping, reset behavior,
or memory implementation. Confirm against authoritative SoC/board
documentation before claiming hardware compatibility.

## 3. Current linker script

The user provided this linker script:

``` ld
OUTPUT_ARCH("riscv")
ENTRY(main)

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x20000000, LENGTH = 2048K
    RAM   (rwx) : ORIGIN = 0x10000000, LENGTH = 256K
}

_Min_Stack_Size = 0x800;

/* Base address where configuration is stored in Flash (last 32KB of 2MB flash) */
__config_start = 0x20000000 + 2048K - 32K;
__config_end   = 0x20000000 + 2048K;

SECTIONS
{
    .text : {
        . = ALIGN(4);
        *(.text.startup*)
        *(.text*)
        *(.rodata*)
        *(.srodata*)
        . = ALIGN(4);
        _etext = .;
    } > FLASH

    .pg_registry : {
        . = ALIGN(4);
        PROVIDE_HIDDEN (__pg_registry_start = .);
        KEEP (*(.pg_registry))
        KEEP (*(SORT(.pg_registry.*)))
        PROVIDE_HIDDEN (__pg_registry_end = .);
    } > FLASH

    .pg_resetdata : {
        . = ALIGN(4);
        PROVIDE_HIDDEN (__pg_resetdata_start = .);
        KEEP (*(.pg_resetdata))
        KEEP (*(SORT(.pg_resetdata.*)))
        PROVIDE_HIDDEN (__pg_resetdata_end = .);
    } > FLASH

    .data : {
        . = ALIGN(4);
        _sdata = .;
        *(.data*)
        *(.sdata*)
        . = ALIGN(4);
        _edata = .;
    } > RAM AT > FLASH

    _sidata = LOADADDR(.data);

    .bss (NOLOAD) : {
        . = ALIGN(4);
        _sbss = .;
        *(.bss*)
        *(.sbss*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
    } > RAM

    .stack (NOLOAD) : {
        . = ALIGN(16);
        _sstack = .;
        . = . + 0x4000;
        _estack = .;
    } > RAM

    _end = .;
}
```

The current script explicitly declares `ENTRY(main)`. The stack region
is reserved as 16 KiB (`0x4000`) and exposes `_sstack` and `_estack`.
The script defines `.data` load/copy symbols and `.bss` bounds, but it
does not itself execute startup initialization.
`_Min_Stack_Size = 0x800` is defined but is not used by this shown
script to size the `.stack` region or enforce a stack minimum.

Do not replace this script blindly. First inspect the actual checked-in
file and build rules, and establish whether a startup routine exists but
is omitted from the build or whether no startup routine exists.

## 4. Relevant ELF symbol evidence

Output from `riscv32-esp-elf-nm -n obj/main/betaflight_ARIESV3.elf`
includes:

``` text
00000800 A _Min_Stack_Size
10000000 D _sdata
100018c0 D _edata
100018c0 B _sbss
1000bee4 B _ebss
1000bef0 B _sstack
1000fef0 B _estack
20000000 T main
200af700 A _sidata
```

Interpretation: - `.data` runs from `0x10000000` through `0x100018C0`
(end-exclusive). - `.bss` runs from `0x100018C0` through `0x1000BEE4`
(end-exclusive). - The reserved stack range is `0x1000BEF0` through
`0x1000FEF0`. - The ELF symbol table shows `main` at `0x20000000`. -
`_sidata` is at `0x200AF700`.

These symbols suggest the linker has allocated data, BSS, and stack
addresses, but **symbol presence does not prove that data copying, BSS
clearing, or stack setup actually occurs at runtime**.

## 5. Renode platform currently used

The user created `~/ariesv3-sim/ariesv3.repl` with:

``` repl
cpu: CPU.RiscV32 @ sysbus
    cpuType: "rv32im_zicsr"

flash: Memory.MappedMemory @ sysbus 0x20000000
    size: 0x200000

ram: Memory.MappedMemory @ sysbus 0x10000000
    size: 0x40000
```

The model currently contains only: - RISC-V CPU - mapped flash memory -
mapped RAM

It does not currently model the ARIES V3 / THEJAS32 reset sequence,
UARTs, timers, interrupt controller, SPI, I2C, PWM, or other board
peripherals. Do not infer that the platform is an accurate THEJAS32
hardware model.

Commands previously used in the Renode monitor:

``` text
mach create
machine LoadPlatformDescription @/home/ivan/ariesv3-sim/ariesv3.repl
sysbus LoadELF @/home/ivan/Desktop/betaflight/obj/main/betaflight_ARIESV3.elf
sysbus.cpu PC 0x20000000
start
```

The `sysbus.cpu PC 0x20000000` write was a manual debugging action, not
a valid substitute for correct reset/startup behavior.

## 6. Observed Renode behavior and logs

Reading the first instruction works:

``` text
sysbus ReadDoubleWord 0x20000000
0xFC010113
```

This matches the first instruction from the disassembly:

``` asm
20000000 <main>:
20000000: fc010113    add sp,sp,-64
```

The CPU reported: - `IsStarted`: `True` - `IsPaused`: `False` before
abort - `PC`: `0x20000000` - `ResetVector`: `0x1000` -
`ExecutedInstructions`: `0xA8` (168) - `EmulationState`: `Aborted` -
`IsHalted`: `False` - `MCAUSE`: `0x0` - `MTVEC`: `0x1010` - `SP`:
`0xFFFFFFC0` - `GP`: `0x0`

Key log lines:

``` text
[INFO] sysbus: Loading block of 718592 bytes length at 0x20000000.
[INFO] sysbus: Loading block of 48868 bytes length at 0x200AF700.
[INFO] sysbus: Loading block of 16396 bytes length at 0x200B0FC0.
[INFO] cpu: Setting PC value to 0x20000000.
[INFO] machine-0: Machine started.
[WARNING] sysbus: [cpu: 0x20000008] WriteDoubleWord to non existing peripheral at 0xFFFFFFFC, value 0x0.
[WARNING] sysbus: [cpu: 0x2000000C] WriteDoubleWord to non existing peripheral at 0xFFFFFFF8, value 0x0.
[WARNING] sysbus: [cpu: 0x20000010] WriteDoubleWord to non existing peripheral at 0xFFFFFFF4, value 0x0.
...
[WARNING] sysbus: [cpu: 0x20003C94] ReadDoubleWord from non existing peripheral at 0xFFFFFFBC.
...
[WARNING] sysbus: [cpu: 0x0] ReadByte from non existing peripheral at 0x0.
[ERROR] cpu: CPU abort [PC=0x0]: Trying to execute code outside RAM or ROM at 0x00000000.
[ERROR] machine-0: This machine has aborted. Emulation cannot continue until this machine is removed.
```

The first instruction decrements `sp` by 64. With `SP` initially zero,
this yields `0xFFFFFFC0`, matching the observed value. The following
stores access unmapped addresses near `0xFFFFFFFF`. The CPU later tries
to execute from address zero.

The leading diagnosis is that execution begins at `main()` with no valid
stack initialization. However, the agent must verify the entire
execution path and not assume the stack issue is the only fault. The
invalid stack may corrupt return addresses or other state, causing the
later jump to zero. The current logs do not prove all downstream
failures are caused exclusively by the stack.

The GTK/GDK popup warnings in the logs appear unrelated to firmware
execution and should not be treated as the cause of the CPU abort.

## 7. Required investigation

Before editing, inspect the real repository and build system.

### 7.1 Locate startup code and build configuration

Inspect: - `src/platform/ARIES/` recursively. - The actual ARIES linker
script path. - Make/build files that select the linker script and source
files. - Existing architecture startup code in the repository for
supported RISC-V targets, if any. - Any startup objects or C runtime
objects supplied by the selected toolchain. - The final ELF entry point,
program headers, sections, symbols, and disassembly.

Useful searches:

``` bash
cd ~/Desktop/betaflight

find src/platform/ARIES -type f

find . \( -iname '*aries*ld' -o -iname '*aries*lds' \)

grep -RInE '(_start|Reset_Handler|_estack|_sidata|_sbss|_ebss|\\.init_array)' \
    src/platform/ARIES make 2>/dev/null

riscv32-esp-elf-readelf -h -l -S obj/main/betaflight_ARIESV3.elf
riscv32-esp-elf-nm -n obj/main/betaflight_ARIESV3.elf
riscv32-esp-elf-objdump -d obj/main/betaflight_ARIESV3.elf
```

Use the actual toolchain executable paths or the configured prefix if
the commands are not on `PATH`.

### 7.2 Determine the correct entry/startup model

Decide based on evidence whether to: 1. Add a minimal project-owned
RISC-V startup entry (for example `_start` in an assembly file) and make
the linker entry point reference it; or 2. Correctly include and
configure an existing startup/runtime implementation.

Do not blindly introduce a duplicate `_start` if one already exists. Do
not assume the ESP toolchain's startup code is appropriate for THEJAS32
just because the compiler targets RISC-V.

### 7.3 Startup responsibilities

The final startup implementation must, as appropriate for this
bare-metal target:

1.  Establish a valid stack pointer from `_estack` before any C function
    executes.
2.  Initialize `gp` if required by the compiler/linker configuration.
    Follow the RISC-V toolchain's expected `norelax`/global-pointer
    initialization pattern where applicable; inspect actual
    compiler/linker flags rather than guessing.
3.  Copy `.data` bytes from its flash load address `_sidata` to
    `[ _sdata, _edata )`.
4.  Clear `.bss` bytes in `[ _sbss, _ebss )`.
5.  Respect alignment and RV32/ILP32 assumptions.
6.  Handle any required `.init_array`/C constructor initialization if
    the linked application or runtime depends on it. Verify whether such
    sections exist and whether the project requires constructors; do not
    add unnecessary generic runtime machinery without evidence.
7.  Call `main`.
8.  Define a safe behavior if `main` returns, such as a non-returning
    idle loop or a documented trap loop. Do not fall through into
    arbitrary flash bytes.
9.  Avoid relying on hardware features that have not been verified for
    THEJAS32.

Review how the linker script's `.text.startup*` pattern is used. If the
startup assembly is placed there, confirm the section is retained and
placed at the intended entry address. Check that `ENTRY(...)` points at
the actual startup symbol.

### 7.4 Memory layout validation

Validate that: - The stack region does not overlap `.data`, `.bss`, or
other allocated RAM sections. - `_estack` is within the configured RAM,
correctly aligned, and suitable as a downward-growing stack's initial
value. - `.data` has a valid flash load address and RAM execution
address. - `.bss` bounds are correct. - Flash program sections do not
overlap the reserved final 32 KiB configuration area at
`0x201F8000`--`0x201FFFFF`. - The linker reports overflow or overlap
rather than silently producing an invalid layout.

The existing symbols indicate `_estack = 0x1000FEF0`, which is within
RAM and 16-byte aligned. Do not automatically move the stack to the top
of all 256 KiB RAM without checking the linker layout and intended
reservation scheme.

### 7.5 Reset vector versus ELF entry point

The CPU's Renode `ResetVector` was `0x1000`, while the ELF entry point
was `0x20000000`. Investigate the distinction: - Renode's default CPU
reset vector may be a model default. - Setting `PC` manually to the ELF
entry bypasses reset behavior and startup assumptions. - The real SoC's
reset address and boot flow must be obtained from authoritative THEJAS32
documentation before modeling them.

For this task, it is acceptable to validate a controlled ELF-start
workflow in Renode if the actual reset vector is unknown, but label it
accurately as an ELF-entry test, not a faithful hardware reset
simulation. Do not invent a reset vector.

## 8. Implementation constraints

-   Keep changes scoped to the ARIES V3 platform/build/startup unless a
    necessary shared build change is demonstrated.
-   Preserve unrelated Betaflight code and existing stubs.
-   Do not implement unrelated UART/SPI/I2C/PWM drivers as part of this
    startup task.
-   Do not modify Betaflight flight-control algorithms to hide startup
    errors.
-   Do not change the memory map merely to silence Renode warnings
    without authoritative evidence.
-   Do not use manual Renode register writes as the final fix.
-   Do not claim a successful flight-controller boot, sensor operation,
    motor output, or hardware validation.
-   Prefer a small, reviewable patch and explain each changed file.
-   Keep naming/style consistent with nearby project code.
-   Report any uncertain assumptions explicitly.

## 9. Build and validation plan

**Renode responsibility:** The user will interact with Renode directly.
Do not assume control of their Renode monitor or GUI. Prepare exact,
ordered commands (and a script/platform file when useful), explain what
each command is expected to demonstrate, and ask the user to paste
relevant output. When output reveals a new issue, adapt the next steps
instead of repeatedly asking the user to run a fixed checklist. The
agent may inspect logs or scripts available in the repository, but must
not claim a Renode test passed unless the user provides evidence or the
agent actually runs an independent reproducible test in an environment
it controls.

### 9.1 Clean rebuild

Use the project's existing build target and configuration for `ARIESV3`.
First record the exact build command, then rebuild after the change.
Avoid changing the toolchain or reinstalling it unless a specific
problem requires it.

### 9.2 Inspect the resulting ELF

At minimum, capture:

``` bash
riscv32-esp-elf-readelf -h obj/main/betaflight_ARIESV3.elf
riscv32-esp-elf-readelf -l obj/main/betaflight_ARIESV3.elf
riscv32-esp-elf-nm -n obj/main/betaflight_ARIESV3.elf | \
    grep -E '(_start|main|_sdata|_edata|_sidata|_sbss|_ebss|_sstack|_estack)'
riscv32-esp-elf-objdump -d obj/main/betaflight_ARIESV3.elf
```

Confirm: - The ELF entry point is the startup symbol, if a startup
symbol is introduced. - The startup symbol's instructions set up the
stack before calling C. - `.data` and `.bss` initialization code is
present and reachable. - The stack and data addresses match the linker
map. - No unexpected undefined startup symbols remain. - The ELF remains
RV32/ILP32 and uses the intended ISA flags.

### 9.3 Renode test

Use a fresh Renode machine after every aborted run; Renode has
explicitly stated that an aborted machine cannot continue until it is
removed. Do not interpret a failed machine's later `Step` commands as
new evidence.

For the controlled ELF-entry test: 1. Create a new machine. 2. Load the
platform. 3. Load the rebuilt ELF. 4. Set PC manually only if required
for this controlled test and clearly record that the test bypasses
hardware reset behavior. 5. Run the CPU. 6. Observe PC progression,
register values, and logs. 7. Check that stack writes land within
`0x10000000`--`0x1003FFFF`. 8. Confirm there are no writes to
`0xFFFFFFxx` caused by the initial stack setup. 9. Determine whether the
CPU reaches `main()` through the startup call. 10. Record any later
failure caused by missing peripherals or unsupported hardware accesses
separately from startup correctness.

A startup test may use a temporary minimal test program if needed to
isolate stack/data/BSS initialization from Betaflight's
hardware-dependent initialization. If using such a test, document it
separately; it does not prove the complete Betaflight firmware runs.

### 9.4 Do not overinterpret Renode execution

The current Renode model only has CPU, flash, and RAM. Once startup
works, Betaflight may access memory-mapped hardware or execute code that
expects timers, interrupts, clocks, UARTs, SPI, I2C, PWM, or other
devices. Failures at those points should be reported as subsequent
simulation/platform gaps, not as evidence that startup remains broken
unless logs establish that connection.

## 10. Acceptance criteria

The task is complete only when the agent supplies evidence for all
applicable criteria:

### Required

-   [ ] Root cause is explained from source/build/ELF evidence.
-   [ ] A correct startup path exists and is included in the ARIES V3
    build.
-   [ ] The stack is initialized before entering C code.
-   [ ] `.data` copying and `.bss` clearing are implemented and verified
    if required by the selected runtime model.
-   [ ] `gp` handling is correct for the actual toolchain/code model, or
    the agent explains with evidence why no initialization is needed.
-   [ ] Linker entry point and startup symbol are consistent.
-   [ ] RAM/flash sections and stack boundaries are checked for overlap
    and fit.
-   [ ] The ELF rebuild succeeds.
-   [ ] Disassembly and symbol output support the claimed startup
    behavior.
-   [ ] A fresh Renode test shows a valid stack and avoids the observed
    initial writes to `0xFFFFFFFC`, `0xFFFFFFF8`, etc.
-   [ ] Results distinguish successful startup from later
    peripheral-related failures.
-   [ ] All changed files and exact reproduction commands are listed.

### Explicitly not required for this task

-   [ ] Full Betaflight operation in Renode.
-   [ ] Full THEJAS32 peripheral emulation.
-   [ ] Real-board boot or flight validation (the user does not
    currently have the board).
-   [ ] Completion of UART, SPI, I2C, timer, interrupt, IMU, receiver,
    or PWM drivers.

## 11. Agent autonomy and communication

The agent is authorized to decide what repository-side actions are
necessary to solve the startup task, rather than following a rigid
step-by-step implementation chosen in advance. It may inspect additional
files, compare working startup patterns in the repository, change the
build configuration, add a small startup source file, write tests or
diagnostic scripts, and rebuild as appropriate. Keep the work focused on
getting a valid, testable startup path for this target; do not expand
into unrelated driver implementation unless evidence shows it is
necessary to diagnose the immediate issue.

Communicate results clearly to the user. For meaningful milestones,
report: - what was inspected or changed; - the evidence that motivated
the decision; - the exact build/test commands run and their actual
outcomes; - what the user should run in Renode next, in exact command
order; - what output to paste back and what it will help determine; -
which conclusions are verified, inferred, or still unverified.

Do not claim to have executed commands in the user's environment or
interacted with the user's Renode session unless that actually occurred.
Do not hide failed attempts; explain what failed and how the next
approach differs.

## 12. Required final report from the agent

Return a concise but technically detailed report with these headings:

1.  **Root cause**
2.  **Files changed**
3.  **Startup sequence implemented**
4.  **Linker and memory validation**
5.  **Build result**
6.  **Renode validation**
7.  **Remaining blockers and limitations**
8.  **Exact commands to reproduce**

Include relevant snippets of startup assembly/C and important
ELF/trace/log output. Mark every claim as one of: - **Verified** ---
supported by command output or tests. - **Inferred** --- likely based on
evidence but not directly tested. - **Unverified** --- requires THEJAS32
documentation or physical hardware.

If the agent cannot complete the fix, it must stop at the first blocker,
explain the evidence, and propose the smallest next step rather than
pretending the task is complete.

## 13. Current state summary

What is known: - The ARIESV3 target builds and links in its current
bare-minimum form. - The ELF entry point is `main` at `0x20000000`. -
The current linker script reserves a 16 KiB stack and defines
`_estack = 0x1000FEF0`. - Renode successfully reads the first
instruction from mapped flash. - When started with `SP = 0`, the program
writes to unmapped addresses near `0xFFFFFFFF`. - Renode later aborts
after attempting to execute at address zero.

What is not yet established: - Whether an existing startup routine is
present but omitted from the build. - Whether `.data`, `.bss`, and any
constructor sections are initialized at runtime. - Whether the actual
THEJAS32 reset vector is `0x1000` or something else. - Whether the
current memory map matches the physical board. - Whether Betaflight can
proceed beyond startup after missing peripherals are addressed. -
Whether the firmware has been run on physical ARIES V3 hardware.

**Primary instruction to the agent:** inspect first, make the smallest
correct startup fix, prove it with ELF and Renode evidence, and report
limitations honestly. Do not expand the task into a complete hardware
port.
