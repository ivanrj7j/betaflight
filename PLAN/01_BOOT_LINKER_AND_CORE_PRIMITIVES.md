# Domain Plan 01: Boot, Linker & Silicon Core Runtime Architecture

> **DOMAIN:** 01 — Boot, Memory & Core Runtime  
> **TARGET SILICON:** C-DAC THEJAS32 SoC (VEGA ET1031 RISC-V RV32IM @ 100 MHz, 256 KB SRAM, No XIP)  
> **ACTIVE BRANCH:** `rebuild`  
> **KNOWLEDGE BASE CITATIONS:**
> - Evaluations: [`01_EVAL_STARTUP_S.md`](file:///home/ivan/Desktop/betaflight/knowledgebase/codebase_evaluations/01_EVAL_STARTUP_S.md), [`02_EVAL_LINKER_ARIES_V3_LD.md`](file:///home/ivan/Desktop/betaflight/knowledgebase/codebase_evaluations/02_EVAL_LINKER_ARIES_V3_LD.md), [`04_EVAL_PLATFORM_H.md`](file:///home/ivan/Desktop/betaflight/knowledgebase/codebase_evaluations/04_EVAL_PLATFORM_H.md), [`05_EVAL_SYSTEM_C.md`](file:///home/ivan/Desktop/betaflight/knowledgebase/codebase_evaluations/05_EVAL_SYSTEM_C.md)
> - Component Blueprints: [`01_SYSTEM_AND_CLOCKS.md`](file:///home/ivan/Desktop/betaflight/knowledgebase/components/01_SYSTEM_AND_CLOCKS.md), [`02_STARTUP_BOOT_AND_EXCEPTIONS.md`](file:///home/ivan/Desktop/betaflight/knowledgebase/components/02_STARTUP_BOOT_AND_EXCEPTIONS.md), [`09_BUILD_LINKER_AND_MEMORY.md`](file:///home/ivan/Desktop/betaflight/knowledgebase/components/09_BUILD_LINKER_AND_MEMORY.md)
> - Hardware Inspections: [`01_STM32_COMMON_SYSTEM.md`](file:///home/ivan/Desktop/betaflight/logs/hardware_inspection/01_STM32_COMMON_SYSTEM.md), [`03_STM32_STARTUP_ASSEMBLY.md`](file:///home/ivan/Desktop/betaflight/logs/hardware_inspection/03_STM32_STARTUP_ASSEMBLY.md), [`04_STM32_FAULT_HANDLERS.md`](file:///home/ivan/Desktop/betaflight/logs/hardware_inspection/04_STM32_FAULT_HANDLERS.md), [`12_STM32_LINKER_AND_SECTIONS.md`](file:///home/ivan/Desktop/betaflight/logs/hardware_inspection/12_STM32_LINKER_AND_SECTIONS.md), [`17_ESP32_SYSTEM_AND_TIMER.md`](file:///home/ivan/Desktop/betaflight/logs/hardware_inspection/17_ESP32_SYSTEM_AND_TIMER.md)
> - Source Files: [`startup.S`](file:///home/ivan/Desktop/betaflight/src/platform/ARIES/startup.S), [`aries_v3.ld`](file:///home/ivan/Desktop/betaflight/src/platform/ARIES/linker/aries_v3.ld), [`platform.h`](file:///home/ivan/Desktop/betaflight/src/platform/ARIES/platform.h), [`system.c`](file:///home/ivan/Desktop/betaflight/src/platform/ARIES/system.c)
> - C-DAC Baseline: [`crt.S`](file:///home/ivan/Desktop/betaflight/lib/vega-sdk/vega-sdk/bsp/common/crt.S), [`link1.lds`](file:///home/ivan/Desktop/betaflight/lib/vega-bsp/thejas32/link1.lds), [`encoding.h`](file:///home/ivan/Desktop/betaflight/lib/vega-sdk/vega-sdk/bsp/include/encoding.h), [`timer.c`](file:///home/ivan/Desktop/betaflight/lib/vega-sdk/vega-sdk/bsp/drivers/timer/timer.c)

---

## 1. Microcomponent 01_A: Machine Startup, GPR Sanitization & Trap Vector (`startup.S`)

### Why (Architectural Necessity & Silicon Ground Truth)
1. **Cold-Boot Register Indeterminism:** On physical RISC-V cores upon power-on reset, general-purpose registers (`x1`–`x31`) power up with random bit patterns. If uninitialized registers are accessed prior to C runtime variable zeroing, undefined conditional branches and simulator warnings occur.
2. **Missing Trap Frame `x2` (`sp`) Preservation Bug:** In the previous port ([`01_EVAL_STARTUP_S.md`](file:///home/ivan/Desktop/betaflight/knowledgebase/codebase_evaluations/01_EVAL_STARTUP_S.md)), `trap_entry` decremented `sp` by 128 bytes and saved registers `x1` and `x3`–`x31`, but explicitly skipped register `x2` (`sp`). When an exception occurred, `system.c`'s `handle_trap()` received a corrupted stack pointer in argument `a2`, making stack backtraces impossible.
3. **256-Byte Trap Vector Base Alignment:** In the RISC-V privileged architecture, the Machine Trap Vector Base Address register (`mtvec`) in Direct Mode requires 4-byte alignment, and in Vectored Mode requires 256-byte alignment (`.align 8`). Without strict alignment, hardware masks lower bits, branching traps into unexecutable memory.
4. **LMA == VMA In-SRAM Data Optimization:** Because THEJAS32 boots directly from internal SRAM, the Load Memory Address (LMA) equals the Virtual Memory Address (VMA). Executing an unoptimized copy loop that writes `.data` over itself is redundant; bypassing the copy when `_sdata == _sidata` saves boot cycles and preserves initialized memory integrity.

### How (Detailed Execution Flow & Algorithmic State)
1. **Reset Entry Point:**
   - Anchor `_start` in section `.text.startup` at `ORIGIN(RAM)` (`0x00202000`).
   - Force `.align 8` (256-byte boundary) for the trap vector and `.align 2` for instruction streams.
2. **Complete 31-GPR Zeroing:**
   - Sequence 31 explicit immediate loads (`li x1, 0`, `li x2, 0`, ..., `li x31, 0`) to wipe all caller-saved and callee-saved registers to absolute zero.
3. **Global & Stack Pointer Setup:**
   - Load `sp` with `_estack` (top of SRAM at `0x0023FFFF`).
   - Configure global pointer `gp` with linker relaxations disabled:
     `.option push; .option norelax; la gp, __global_pointer$; .option pop;`
4. **Machine Trap Setup (`mtvec`):**
   - Load address of `trap_entry` into `a0`. Set Direct Mode (Mode bits = 0).
   - Write to CSR: `csrw mtvec, a0`.
5. **BSS Clearing & Data LMA Check:**
   - Load `_sbss` and `_ebss`. Loop word-by-word storing `x0` (`sw x0, 0(t0); addi t0, t0, 4`) until `t0 >= t1`.
   - Compare `_sidata` and `_sdata`. If equal (`beq t0, t1, copy_done`), bypass `.data` copying entirely.
6. **Handover to Flight Core:**
   - Execute `tail main`. If `main()` ever terminates, fall through into a fail-safe low-power halt loop (`wfi; j .`).
7. **Robust Context Preservation (`trap_entry`):**
   - Allocate 128 bytes on stack (`addi sp, sp, -128`).
   - Save `x1` at `0(sp)`.
   - Calculate pre-trap stack pointer (`addi t0, sp, 128`) and save `t0` at `4(sp)` (`x2`/`sp` slot).
   - Save `x3` through `x31` in sequential 4-byte offsets (`8(sp)` to `120(sp)`).
   - Read CSRs: `csrr t0, mcause`, `csrr t1, mepc`, `csrr t2, mstatus`.
   - Pass context frame pointer in `a0` (`mv a0, sp`) and jump to `thejas32_trap_handler`.
   - Upon return, restore `mstatus` and `mepc`, restore `x1` and `x3`–`x31`, restore `sp` (`addi sp, sp, 128`), and execute `mret`.

### Expected Inputs
* Hardware power-on-reset or active-low pulse on `PUSH_RESETN` (Pin 43 / IC `U8`).
* Initial Program Counter: `PC = 0x00202000`.
* Trapping conditions: Synchronous CPU faults (`mcause` 2: illegal instruction, 5: load access fault, 7: store access fault) or machine external interrupts (`mcause` bit 31 set, cause 11).

### Expected Results
* All 31 GPRs zeroed prior to `sp` assignment.
* Clean jump into `main()` with zero compressed RVC instructions executed.
* Accurate, non-corrupted pre-trap stack pointer available to exception handlers.

---

## 2. Microcomponent 01_B: Linker Script Architecture & SRAM Budget Enforcement (`aries_v3.ld`)

### Why (Architectural Necessity & Silicon Ground Truth)
1. **The Strict 248 KB SRAM Execution Limit:**
   - Physical THEJAS32 silicon contains 256 KB of SRAM spanning `0x00200000` to `0x0023FFFF`. The C-DAC on-chip ROM bootloader reserves the first 8 KB (`0x00200000`–`0x00201FFF`) for vector tables, XMODEM transfer buffers, and stack.
   - User code starts at **`0x00202000`**, leaving exactly **$248\text{ KB}$** ($253{,}952\text{ bytes}$) for all code, read-only data, BSS, Parameter Groups, and execution stack.
2. **Preventing the .init Precedence Trap:**
   - In [`02_EVAL_LINKER_ARIES_V3_LD.md`](file:///home/ivan/Desktop/betaflight/knowledgebase/codebase_evaluations/02_EVAL_LINKER_ARIES_V3_LD.md), placing `.init` before `.text.startup` in the linker script meant that if any library introduced a `.init` section, it usurped address `0x00202000`, displacing `_start` and causing CPU boot failures.
3. **Parameter Group Discovery Anchors (`.pg_registry`):**
   - Betaflight uses a specialized linker section to auto-discover configuration modules. If `__pg_registry_start` or `__pg_registry_end` are stripped or misaligned, the Parameter Group registry fails to initialize on boot.
4. **Compile-Time Memory Boundary Assertion:**
   - Without an explicit assertion, binary growth beyond 248 KB silently wraps or corrupts peripheral memory spaces.

### How (Detailed Memory Mapping & Linker Directives)
1. **Memory Region Declaration:**
   ```ld
   MEMORY
   {
       RAM (rwx) : ORIGIN = 0x00202000, LENGTH = 248K
   }
   ```
2. **Guaranteed Reset Vector Placement:**
   - In `SECTIONS`, ensure `.text` is the very first output block, and explicitly place `KEEP(*(.text.startup*))` as the first input item:
     ```ld
     .text : {
         . = ALIGN(4);
         _stext = .;
         KEEP(*(.text.startup*))
         KEEP(*(SORT_NONE(.init)))
         *(.text .text.*)
         _etext = .;
     } > RAM
     ```
3. **Dedicated Read-Only Section:**
   - Isolate `.rodata` from `.text` to enable clean memory profiling:
     ```ld
     .rodata : {
         . = ALIGN(4);
         _srodata = .;
         *(.rodata .rodata.*)
         *(.srodata .srodata.*)
         _erodata = .;
     } > RAM
     ```
4. **Parameter Group Registry Preservation:**
   - Anchor the table with `KEEP()` and 4-byte alignment:
     ```ld
     . = ALIGN(4);
     __pg_registry_start = .;
     KEEP(*(SORT(.pg_registry)))
     __pg_registry_end = .;
     . = ALIGN(4);
     __pg_resetdata_start = .;
     KEEP(*(SORT(.pg_resetdata)))
     __pg_resetdata_end = .;
     ```
5. **BSS & In-RAM EEPROM Reservation:**
   - Place `.data` and `.bss` sequentially. Include `eepromData[8192]` allocated by `#define CONFIG_IN_RAM`.
   - Align `_edata` and `_ebss` to 4-byte boundaries to match assembly word loops.
6. **Dedicated Stack & Boundary Assertion:**
   - Reserve 8 KB (`0x2000` bytes) for execution stack:
     ```ld
     .stack : {
         . = ALIGN(16);
         _sstack = .;
         . = . + 0x2000;
         _estack = .;
     } > RAM
     ```
   - Enforce the compile-time guardrail:
     ```ld
     ASSERT(_estack <= (ORIGIN(RAM) + LENGTH(RAM)), "FATAL: Total firmware image + stack exceeds 248 KB physical SRAM!")
     ```

### Expected Inputs
* Input object files from GCC cross-compilation (`-ffunction-sections`, `-fdata-sections`).
* Linker flags: `-Wl,--gc-sections -nostdlib -T aries_v3.ld`.

### Expected Results
* `_start` strictly anchored at `0x00202000`.
* Total memory footprint ($\text{text} + \text{rodata} + \text{data} + \text{bss} + \text{stack}$) verified $\le 253{,}952\text{ bytes}$.
* Build immediately errors out if total size exceeds 248 KB.

---

## 3. Microcomponent 01_C: Atomic Operations & Platform Primitives (`platform.h` & `stub_drivers.c`)

### Why (Architectural Necessity & Silicon Ground Truth)
1. **The Phantom `BASEPRI` Catastrophe:**
   - In [`04_EVAL_PLATFORM_H.md`](file:///home/ivan/Desktop/betaflight/knowledgebase/codebase_evaluations/04_EVAL_PLATFORM_H.md), `platform.h` previously defined `__set_BASEPRI(basePri) { (void)basePri; }`. Because RISC-V lacks ARM NVIC `BASEPRI` registers, every `ATOMIC_BLOCK()` critical section in Betaflight executed with interrupts fully unmasked.
   - High-rate Timer 1 and UART RX interrupts fired mid-way through scheduler task updates and circular ring buffer writes, causing intermittent memory corruption and task hangs.
2. **RV32IM Software Atomic Emulation:**
   - THEJAS32 implements the integer multiply/divide extensions (`RV32IM`), but omits the Atomic ('A') extension (`lr.w`/`sc.w`). Standard GCC atomic built-ins (such as `__atomic_fetch_add_4`) emit unresolved symbols unless provided by a target platform shim.
3. **Interrupt Nesting State Preservation:**
   - In `stub_drivers.c`, `__atomic_fetch_add_4` unconditionally executed `__enable_irq()` on exit. When invoked inside an active interrupt service routine, it prematurely unmasked global interrupts before the ISR completed, breaking interrupt prioritization.

### How (Detailed Implementation & Register Sequences)
1. **M-Mode CSR Interrupt Control Architecture:**
   - In the RISC-V Machine Status register (`mstatus`), Machine Interrupt Enable (`MIE`) is Bit 3 (`0x00000008`).
   - In `platform.h`, define custom platform atomic primitives hook:
     `#define PLATFORM_CUSTOM_BASEPRI_NB`
2. **Atomic Enter Sequence (`csrrci`):**
   - Atomically capture prior `mstatus` and clear bit 3 in a single instruction:
     ```c
     static inline uint32_t thejas32_atomic_enter(void) {
         uint32_t prev_mstatus;
         __asm__ volatile ("csrrci %0, mstatus, 8" : "=r"(prev_mstatus) :: "memory");
         return prev_mstatus;
     }
     ```
3. **Atomic Exit Sequence (`csrs`):**
   - Restore global interrupts only if they were enabled prior to entering the critical block:
     ```c
     static inline void thejas32_atomic_exit(uint32_t prev_mstatus) {
         if (prev_mstatus & 8) {
             __asm__ volatile ("csrs mstatus, 8" ::: "memory");
         }
     }
     ```
4. **State-Preserving `__atomic_fetch_add_4` (`stub_drivers.c`):**
   ```c
   unsigned int __atomic_fetch_add_4(volatile void *ptr, unsigned int val, int memorder) {
       uint32_t prev = thejas32_atomic_enter();
       unsigned int old = *(volatile unsigned int *)ptr;
       *(volatile unsigned int *)ptr = old + val;
       thejas32_atomic_exit(prev);
       return old;
   }
   ```
5. **CMSIS Intrinsic Compatibility:**
   - Map `__REV(x)` to `__builtin_bswap32(x)`.
   - Map `__REV16(x)` to `__builtin_bswap16(x)`.
   - Map `__NOP()` to `__asm__ volatile ("nop")`.
   - Map `__WFI()` to `__asm__ volatile ("wfi")`.

### Expected Inputs
* Active `mstatus` CSR containing current interrupt state.
* Memory pointers and addend operands passed to atomic operations.

### Expected Results
* `ATOMIC_BLOCK()` executes with 100% atomicity on single-core RV32IM.
* Zero undefined symbol errors for GCC atomic built-ins.
* Global interrupts remain masked if atomic operations execute within an active ISR.

---

## 4. Microcomponent 01_D: System Timebase, Microsecond Clocks & Monotonicity (`system.c`)

### Why (Architectural Necessity & Silicon Ground Truth)
1. **The 42.95-Second 32-Bit Rollover:**
   - At 100 MHz clock speed, a 32-bit cycle register rolls over after $(2^{32}-1) / 100\text{ MHz} \approx 42.949\text{ seconds}$. In earlier tests ([`04_RETROSPECTIVE_TEST_LOGS.md`](file:///home/ivan/Desktop/betaflight/knowledgebase/retrospective_logs/04_RETROSPECTIVE_TEST_LOGS.md)), scheduler tasks suffered huge negative time deltas at second 43, causing PID divergence and watchdog lockup.
2. **Avoiding 64-Bit Software Division (`__udivdi3`):**
   - Converting a 64-bit cycle count to microseconds via `cycles / 100ULL` invokes `__udivdi3` from `libgcc.a`, taking $150\text{--}200$ clock cycles. Calling `micros()` dozens of times per millisecond consumes $> 15\%$ of total CPU budget.
3. **Pure 32-Bit Memory Operations:**
   - Toolchain `libc` memset/memcpy routines contain 16-bit compressed RVC instructions that crash the ET1031 core. Bare-metal uncompressed memory routines must be compiled with loop distribution disabled.
4. **Soft-Reboot Loop Hang:**
   - `systemReset()` previously executed `while (1);`, requiring a physical board power-cycle after saving settings in the CLI.

### How (Detailed Implementation & Algorithms)
1. **Atomic 64-Bit Cycle Read (`getCycleCounter64`):**
   - Execute double-read loop of `mcycleh` and `mcycle` until upper word stabilizes:
     ```c
     uint32_t h1, l, h2;
     do {
         __asm__ volatile ("csrr %0, mcycleh" : "=r"(h1));
         __asm__ volatile ("csrr %0, mcycle"  : "=r"(l));
         __asm__ volatile ("csrr %0, mcycleh" : "=r"(h2));
     } while (h1 != h2);
     return (((uint64_t)h1) << 32) | l;
     ```
2. **Modular Reciprocal Division for Microseconds:**
   - Compute `micros()` without `__udivdi3` by splitting the 64-bit cycle into high and low words, multiplying by the pre-computed reciprocal of 100 ($2^{32} / 100 = 42{,}949{,}673$), reducing execution time to $< 25$ cycles.
3. **Hardware Timer 0 Interrupt & EOI Acknowledgment:**
   - Configure Synopsys DW_apb_timers Hardware Timer 0 (`0x10000A00`) as a periodic down-counter for background ticks.
   - In `thejas32_timer0_isr`, acknowledge the interrupt by reading `THEJAS32_TIMER(0).EOI`, clearing the hardware interrupt latch.
4. **Deterministic Soft-Reboot Vector Jump:**
   - In `systemReset()`, disable interrupts, reset Timer 0/1 registers, flush UART0 FIFOs, and execute an explicit register jump to `_start`:
     ```c
     void systemReset(void) {
         __disable_irq();
         __asm__ volatile ("la sp, _estack; la t0, 0x00202000; jr t0;");
         while (1);
     }
     ```
5. **Non-RVC Word-Aligned Memory Operations:**
   - Implement `memset`, `memcpy`, and `memmove` with `#pragma GCC optimize ("no-tree-loop-distribute-patterns")` to prevent GCC from re-substituting loops with recursive calls to multilib routines.

### Expected Inputs
* 100.000 MHz crystal oscillator on pin 44 (`CLK`).
* RISC-V 64-bit CSR cycle counters `mcycle` and `mcycleh`.
* CLI `save` or `reboot` command triggers.

### Expected Results
* Monotonic microsecond timebase valid across $> 5,800$ years with zero rollover jitter.
* Zero calls to `__udivdi3` in the disassembled binary.
* CLI `save` command cleanly re-executes `_start` and prints the boot banner in $< 50\text{ ms}$.

---

## 5. Domain 1 Verification & Quality Gate Checklist

- [ ] **Opcode Inspection:** `riscv32-esp-elf-objdump -d obj/main/betaflight_ARIESV3.elf | grep -E "^\s*[0-9a-f]+:\s+[0-9a-f]{4}\s+"` strictly returns **0**.
- [ ] **SRAM Bounds Check:** `riscv32-esp-elf-size` reports total bytes ($\text{text} + \text{rodata} + \text{data} + \text{bss}$) $\le 253{,}952\text{ bytes}$.
- [ ] **Section Placement Check:** `_start` strictly located at `0x00202000` in the `.map` file; `.pg_registry` preserved.
- [ ] **Atomic Safety Check:** `csrrci` and `csrs` present in disassembled `thejas32_atomic_enter/exit`.
- [ ] **Monotonic Soak Test:** CLI `status` uptime verified monotonically increasing past 60 seconds without timing glitches.
