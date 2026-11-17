# Hardware Inspection Log: `src/platform/STM32/system_stm32f4xx.c`

## 1. File Purpose & Role in Architecture
* **File Path**: `src/platform/STM32/system_stm32f4xx.c`
* **Subsystem**: Platform-Specific System Reset, Hardware Clock Tree Initialization, Bootloader Branching, and Peripheral Bus Clock Activation.
* **Key Exports**:
  * `systemInit()`: Master platform hardware entry point called right after reset.
  * `systemReset()`: Triggers clean hardware system reset via NVIC.
  * `systemResetToBootloader()`: Triggers reboot into ROM bootloader via persistent RAM flags.
  * `checkForBootLoaderRequest()`: Early-boot check that intercepts reset to redirect control to ROM DFU.
  * `enableGPIOPowerUsageAndNoiseReductions()`: Bulk activation of peripheral bus clocks (AHB1/2, APB1/2).

---

## 2. Hardware Challenges STM32 Developers Faced & How They Overcame Them

### Challenge 1: Remembering Bootloader Mode Across Hard Resets
* **The Problem**: When the user clicks "Activate Bootloader" in Betaflight Configurator (via `MSP_REBOOT` or CLI `dfu`), the system must enter the manufacturer ROM bootloader. However, an MCU reset (`NVIC_SystemReset()`) resets CPU registers and clears normal `.bss`/`.data` variables.
* **How It Was Overcome**:
  1. Developers store a magic value in a preserved memory location (backup SRAM or uninitialized RAM buffer tracked by `persistentObjectWrite()`):
     ```c
     persistentObjectWrite(PERSISTENT_OBJECT_RESET_REASON, RESET_BOOTLOADER_REQUEST_ROM);
     __disable_irq();
     NVIC_SystemReset();
     ```
  2. Inside `checkForBootLoaderRequest()` (which executes before standard C variable initialization), the system checks for this persistent token:
     ```c
     if (bootloaderRequest == RESET_BOOTLOADER_REQUEST_ROM) {
         persistentObjectWrite(PERSISTENT_OBJECT_RESET_REASON, RESET_NONE);
         extern isrVector_t system_isr_vector_table_base;
         __set_MSP(system_isr_vector_table_base.stackEnd);
         system_isr_vector_table_base.resetHandler();
         while (1);
     }
     ```
     This manually sets the Main Stack Pointer (`__set_MSP`) to the ROM stack and jumps directly to the ST ROM Reset Handler.

---

### Challenge 2: Peripheral Bus Clock Gating Faults
* **The Problem**: In STM32 architectures, almost all peripheral blocks (timers, UARTs, SPI, I2C, ADC) have their clocks disabled by default after reset. Attempting to read or write any peripheral register while its clock gate is off triggers an immediate bus error or `HardFault`.
* **How It Was Overcome**:
  `enableGPIOPowerUsageAndNoiseReductions()` systematically turns on clock distribution across all peripheral buses:
  * `RCC_AHB1Periph_DMA1`, `DMA2`, `SRAM1`, `SRAM2`
  * `RCC_APB1Periph_TIM2..TIM7`, `SPI2`, `SPI3`, `USART2..5`, `I2C1..3`
  * `RCC_APB2Periph_TIM1`, `TIM8`, `USART1`, `USART6`, `ADC1..3`, `SPI1`, `SYSCFG`

---

### Challenge 3: Interrupt Vector Table Relocation (`VTOR`)
* **The Problem**: On ARM Cortex-M, the vector table defaults to address `0x00000000`. In Betaflight, when firmware is flashed or booted into RAM/custom offsets, hardware interrupts jump to invalid memory locations unless the Vector Table Offset Register (`VTOR`) is updated.
* **How It Was Overcome**:
  ```c
  extern uint8_t isr_vector_table_base;
  NVIC_SetVectorTable((uint32_t)&isr_vector_table_base, 0x0);
  ```
  The linker script exports the symbol `isr_vector_table_base`, and `systemInit()` re-points `SCB->VTOR` to this address.

---

### Challenge 4: USB Host Enumeration Stale State
* **The Problem**: If the microcontroller resets while connected to a PC USB port, the PC host thinks the previous USB session is still active, causing enumeration failure.
* **How It Was Overcome**:
  `systemInit()` explicitly forces USB PHY reset:
  ```c
  RCC_AHB2PeriphClockCmd(RCC_AHB2Periph_OTG_FS, DISABLE);
  ```
  This drops the USB DP pull-up resistor, forcing the host PC to detect a disconnect and initiate clean re-enumeration.

---

## 3. Direct Porting Blueprint for ARIES v3.0 (THEJAS32 / VEGA ET1031)

1. **Bootloader Reset Mechanism**:
   * On ARIES v3.0, the bootloader is located at ROM address `0x00010000` (from the Programmer's Manual).
   * To implement `systemResetToBootloader()`, we store the reset token and perform a direct jump to ROM base `0x00010000`:
     ```c
     void systemResetToBootloader(bootloaderRequestType_e requestType) {
         void (*bootloader_entry)(void) = (void (*)(void))0x00010000;
         __asm__ volatile ("csrci mstatus, 8"); // disable machine interrupts
         bootloader_entry();
         while(1);
     }
     ```
2. **Interrupt Vector Base**:
   * Instead of ARM `NVIC_SetVectorTable()`, RISC-V uses the `mtvec` (Machine Trap-Vector Base Address) CSR:
     ```c
     extern void trap_entry(void);
     __asm__ volatile ("csrw mtvec, %0" : : "r" (&trap_entry));
     ```
3. **No USB OTG Disconnect Needed**:
   * Because ARIES v3.0 communicates over hardware `UART0` through the CP2102N bridge, USB enumeration is managed externally by the Silicon Labs IC. The PC USB connection never drops during MCU resets, which makes rebooting much faster and eliminates USB re-enumeration hangs!
