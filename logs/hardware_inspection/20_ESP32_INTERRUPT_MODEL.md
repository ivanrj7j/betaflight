# Hardware Inspection Log: `src/platform/ESP32/interrupt_esp32.c`

## 1. File Purpose & Role in Architecture
* **File Path**: `src/platform/ESP32/interrupt_esp32.c`
* **Subsystem**: Dynamic Interrupt Matrix Routing, ISR Table Registration, and Interrupt Masking on Non-ARM Architectures.
* **Consulted Log Comparison**:
  * [03_STM32_STARTUP_ASSEMBLY.md](03_STM32_STARTUP_ASSEMBLY.md) (STM32 Fixed hardware NVIC vector table, `.weak` handler aliasing)
  * [04_STM32_FAULT_HANDLERS.md](04_STM32_FAULT_HANDLERS.md) (STM32 HardFault / Exception trap models)

---

## 2. Similarities & Differences: STM32 vs ESP32

| Dimension | STM32 Implementation ([Logs 03, 04](03_STM32_STARTUP_ASSEMBLY.md)) | ESP32 Implementation (`interrupt_esp32.c`) | Porting Implication for ARIES v3.0 (THEJAS32) |
| :--- | :--- | :--- | :--- |
| **Interrupt Controller Silicon** | Hardware **NVIC** (Nested Vectored Interrupt Controller). | **Interrupt Matrix** routing peripheral sources to internal CPU interrupt lines. | **RISC-V PLIC** (Platform-Level Interrupt Controller) multiplexing peripheral IRQs into core `MEIP`. |
| **Vector Table Binding** | Static assembly table (`g_pfnVectors`) with compile-time weak symbols. | Dynamic software registration table using function pointers (`esp32IntrRegister()`). | **Adopt Dynamic Registration**: A central RISC-V PLIC dispatcher calls registered function pointers based on claimed IRQ ID. |
| **Interrupt Clearing** | Hardware NVIC automatically clears active status when ISR completes. | Peripheral status cleared in driver; CPU interrupt unmasked via `ets_isr_unmask()`. | PLIC requires explicit **Claim & Complete handshake** (read claim register $\rightarrow$ run ISR $\rightarrow$ write claim register). |

---

## 3. Deep Architectural Analysis: Dynamic Interrupt Routing vs Fixed NVIC

On STM32, every peripheral has a hardwired vector position in silicon (e.g. USART1 is always IRQ 37). The hardware automatically decodes the vector and branches to the function pointer.

On ESP32 and RISC-V architectures, peripheral interrupt signals are not hardcoded to fixed CPU vectors:
* The chip provides an interrupt crossbar or PLIC.
* A software registration API (`esp32IntrRegister`) attaches the ISR handler to the table at runtime.
* When the CPU traps, the central dispatcher claims the interrupt ID, looks up the handler function pointer in an array, calls it, and acks the interrupt.

---

## 4. Porting Blueprint for ARIES v3.0 (THEJAS32 / VEGA ET1031)

1. **PLIC Driver Architecture**:
   * We will implement a lightweight PLIC manager for ARIES v3.0 inspired by `interrupt_esp32.c`:
     ```c
     typedef void (*plic_handler_t)(void *arg);
     static plic_handler_t plic_handlers[THEJAS32_IRQ_COUNT];
     static void *plic_args[THEJAS32_IRQ_COUNT];

     void thejas32_register_irq(uint32_t irq_id, plic_handler_t handler, void *arg) {
         plic_handlers[irq_id] = handler;
         plic_args[irq_id] = arg;
         thejas32_plic_enable(irq_id);
     }
     ```
2. **Central Trap Entry**:
   * In `trap_entry.S` (RISC-V assembly):
     * Save registers `x1..x31`.
     * Check `mcause`:
       * If bit 31 is 1 and cause == 11 (Machine External Interrupt):
         * Read PLIC Claim register (`uint32_t irq = PLIC_CLAIM`).
         * Execute `plic_handlers[irq](plic_args[irq])`.
         * Write back `PLIC_CLAIM = irq`.
       * If bit 31 is 1 and cause == 7 (Machine Timer Interrupt):
         * Call `sysTick_handler()`.
     * Restore registers `x1..x31`, execute `mret`.
   * This provides an interrupt handling architecture that perfectly decouples drivers from assembly!
