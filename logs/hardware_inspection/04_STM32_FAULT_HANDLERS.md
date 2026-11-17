# Hardware Inspection Log: `src/platform/common/stm32/fault_handlers.c`

## 1. File Purpose & Role in Architecture
* **File Path**: `src/platform/common/stm32/fault_handlers.c`
* **Subsystem**: Low-Level Hardware Exception Catching, Fault Register Preservation, and Post-Mortem Crash Diagnostics.
* **Key Components**:
  * `MemManage_Handler`, `BusFault_Handler`, `UsageFault_Handler`, `HardFault_Handler`.
  * `faultCaptureBuf[8]`: Persistent 8-word memory structure retaining post-crash CPU state.
  * Naked assembly capture macro `FAULT_HANDLER_ASM(FLAG)`.
  * Visual LED heartbeat toggle loop for panic notification.

---

## 2. Hardware Challenges STM32 Developers Faced & How They Overcame Them

### Challenge 1: C Compiler Prologues Destroying Stacked Fault Registers
* **The Problem**: When a Cortex-M CPU traps on a fault, hardware pushes an exception stack frame containing `r0-r3`, `r12`, `lr`, `pc`, and `xPSR`. If the handler function is compiled as normal C, the compiler emits instructions like `push {r4, lr}` and allocates local stack space. This immediately modifies the stack pointer (`sp`), clobbers registers, and makes it impossible to locate the true address of the instruction that caused the crash.
* **The Additional Complication (Dual Stack Pointer MSP vs PSP)**: Betaflight runs tasks on either the Main Stack Pointer (`MSP`) or Process Stack Pointer (`PSP`). The handler must know which stack was active when the fault struck.
* **How It Was Overcome**:
  1. Handlers are marked with `__attribute__((naked, used))` to suppress all compiler prologues.
  2. In assembly, bit 2 of Link Register (`lr`) is tested (`tst lr, #4`) to dynamically grab the correct stack pointer:
     ```assembly
     tst   lr, #4
     ite   eq
     mrseq r0, msp
     mrsne r0, psp
     ```
  3. The stacked Program Counter (`[r0, #24]`) and stacked Link Register (`[r0, #20]`) are extracted directly from the frame and written into `faultCaptureBuf`.

---

### Challenge 2: Thumb-2 Literal Pool Distance Exceeded (`.ltorg`)
* **The Problem**: Placing four assembly handlers in the same file caused the PC-relative load `ldr r3, =0xE000ED28` to exceed the maximum 4096-byte branch range because GCC grouped literal constants at the end of the file.
* **How It Was Overcome**:
  The developers added an explicit `.ltorg` directive inside each macro instantiation to force the assembler to dump literal values immediately after each individual handler.

---

### Challenge 3: Extracting Multi-Register Fault Diagnostics
* **The Problem**: On ARM, identifying why a fault occurred requires reading multiple disjoint registers from the System Control Block (SCB):
  * `CFSR` (`0xE000ED28`): Configurable Fault Status (Usage, Bus, MemManage).
  * `HFSR` (`0xE000ED2C`): HardFault Status (escalation indicators).
  * `BFAR` (`0xE000ED38`): Bus Fault Address Register.
  * `MMFAR` (`0xE000ED34`): MemManage Fault Address Register.
* **How It Was Overcome**:
  The naked handler reads all four registers and packs them alongside the PC, LR, and SP into a fixed 8-word debug buffer `faultCaptureBuf`, which can be inspected over SWD/JTAG or printed to CLI after reboot.

---

## 3. Direct Porting Blueprint for ARIES v3.0 (THEJAS32 / VEGA ET1031)

1. **RISC-V Machine Mode Trap Simplicity**:
   * On RISC-V RV32IM, hardware fault handling is far cleaner than ARM.
   * There are no disjoint `CFSR`, `HFSR`, `BFAR`, or `MMFAR` registers. The entire fault context is exposed in three core Machine CSRs:
     * `mepc`: The exact Program Counter of the instruction that faulted.
     * `mcause`: The exact reason (Code 0=Misaligned Fetch, 2=Illegal Instruction, 4=Misaligned Load, 5=Load Fault, 6=Misaligned Store, 7=Store Fault).
     * `mtval` (or `mbadaddr`): The exact invalid memory address that caused the fault!
2. **Implementation on ARIES v3.0**:
   ```c
   volatile uint32_t riscv_fault_context[4]; // [0]=mcause, [1]=mepc, [2]=mtval, [3]=sp

   void handle_trap(uint32_t mcause, uint32_t mepc, uint32_t mtval, uint32_t sp) {
       riscv_fault_context[0] = mcause;
       riscv_fault_context[1] = mepc;
       riscv_fault_context[2] = mtval;
       riscv_fault_context[3] = sp;

       // Toggle Heartbeat / RGB Red LED (GPIO24 on ARIES v3.0) to signal panic
       while (1) {
           gpio_toggle(LED_RED_GPIO);
           for (volatile int i = 0; i < 1000000; i++);
       }
   }
   ```
3. **No Dual-Stack Complication**: In M-mode on VEGA ET1031, there is a single stack pointer (`sp` / `x2`), eliminating the `MSP` vs `PSP` stack selection logic entirely.
