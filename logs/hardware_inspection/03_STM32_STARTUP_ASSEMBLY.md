# Hardware Inspection Log: `src/platform/STM32/startup/startup_stm32f411xe.s`

## 1. File Purpose & Role in Architecture
* **File Path**: `src/platform/STM32/startup/startup_stm32f411xe.s`
* **Subsystem**: Low-Level Assembly Reset Handler, Memory Section Initialization (.data/.bss), Stack Watermarking, Coprocessor Activation, and Interrupt Vector Table.
* **Key Components**:
  * `Reset_Handler`: CPU reset entry point.
  * `.data` copy from Flash (`_sidata`) to SRAM (`_sdata`).
  * `.bss` zero initialization (`_sbss` to `_ebss`).
  * Stack/Heap watermarking (`0xA5A5A5A5`).
  * `g_pfnVectors`: 85-entry ARM Cortex-M4 vector table with `.weak` aliasing to `Default_Handler`.

---

## 2. Hardware Challenges STM32 Developers Faced & How They Overcame Them

### Challenge 1: Early Boot Interception for DFU
* **The Problem**: Standard CMSIS startup templates unconditionally initialize RAM and jump to `main()`. If a user issued a DFU reboot request, going through the full C runtime initialization and peripheral clock configuration creates delay and can corrupt preserved bootloader argument registers.
* **How It Was Overcome**:
  Betaflight developers inserted pre-C hooks at the very top of `Reset_Handler`:
  ```assembly
  Reset_Handler:
    bl persistentObjectInit
    bl checkForBootLoaderRequest
  ```
  This checks for ROM DFU requests before even copying `.data` or clearing `.bss`, allowing an instantaneous branch to ST ROM bootloader.

---

### Challenge 2: Runtime Stack Overflow Detection (Stack Watermarking)
* **The Problem**: Stack overflows in embedded systems cause silent memory corruption when stack frames collide with the top of `.bss`. Debugging which task caused the overflow is difficult without instrumentation.
* **How It Was Overcome**:
  The startup code fills the entire unallocated RAM region between `_heap_stack_begin` and `_heap_stack_end` with the distinct 32-bit signature `0xA5A5A5A5`:
  ```assembly
  MarkHeapStack:
    movs r3, 0xa5a5a5a5
    str  r3, [r2], #4
  LoopMarkHeapStack:
    ldr  r3, = _heap_stack_end
    cmp  r2, r3
    bcc  MarkHeapStack
  ```
  Betaflight's scheduler and CLI `tasks` command can count the remaining `0xA5A5A5A5` words to report the exact stack watermark and alert the user to near-overflow conditions.

---

### Challenge 3: ARM Cortex-M4 Hardware FPU Fault on Reset
* **The Problem**: Although Cortex-M4F chips have a hardware single-precision FPU, ARM silicon boots with Coprocessors CP10 and CP11 disabled. Any floating-point instruction executed while CP10/CP11 are off triggers an immediate `NOCP` UsageFault.
* **How It Was Overcome**:
  ```assembly
  /* FPU settings */
  ldr r0, =0xE000ED88 /* CPACR register */
  ldr r1, [r0]
  orr r1, r1, #(0xF << 20) /* Full access to CP10 & CP11 */
  str r1, [r0]
  ```

---

### Challenge 4: Decoupled ISR Overriding via Weak Aliases
* **The Problem**: Hardcoding driver ISR names in assembly requires changing startup files whenever a peripheral driver changes.
* **How It Was Overcome**:
  Every peripheral interrupt in `g_pfnVectors` is declared `.weak` and points to `Default_Handler`. When a C driver defines `void USART1_IRQHandler(void)`, the linker automatically binds the hardware vector to the driver without modifying the vector table.

---

## 3. Direct Porting Blueprint for ARIES v3.0 (THEJAS32 / VEGA ET1031)

1. **Adopt Stack Watermarking in `crt.S`**:
   * In our RISC-V startup code, we should implement the identical `0xA5A5A5A5` loop filling RAM between `_ebss` and the initial Stack Pointer (`sp`).
   * This ensures Betaflight's built-in stack profiling and crash handler will immediately function on ARIES v3.0.
2. **RISC-V Vector / Trap Model**:
   * In RISC-V Machine Mode, `mtvec` can operate in **Direct Mode** (Mode 0: single trap entry `trap_handler`) or **Vectored Mode** (Mode 1: vector table base + 4 * cause).
   * For the initial ARIES v3.0 port, **Direct Mode** is simpler and safer: all traps jump to `trap_entry`, which saves caller registers, inspects `mcause`, and dispatches to the PLIC / Timer handler.
3. **No FPU Initialization**:
   * As confirmed, the VEGA ET1031 is RV32IM (no FPU coprocessor registers to enable).
