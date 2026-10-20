/*
 * This file is part of Betaflight.
 *
 * Platform definitions and register mapping for C-DAC ARIES v3.0
 * (THEJAS32 / VEGA ET1031 RISC-V SoC).
 * Derived from C-DAC VEGA SDK BSP.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "common/time.h"

#define THEJAS32_RAM_BASE           0x10000000UL
#define THEJAS32_FLASH_BASE         0x20000000UL

// Peripheral Base Addresses
#define THEJAS32_UART_BASE          0x10000100UL
#define THEJAS32_SPI_0_1_BASE       0x10000600UL
#define THEJAS32_SPI_2_3_BASE       0x10200100UL
#define THEJAS32_I2C_BASE           0x10000800UL
#define THEJAS32_TIMER_BASE         0x10000A00UL
#define THEJAS32_TIMER_RAW_INTR     0x10000AA8UL
#define THEJAS32_ADC_BASE           0x10001000UL
#define THEJAS32_GPIO_0_BASE        0x10080000UL
#define THEJAS32_GPIO_1_BASE        0x10180000UL
#define THEJAS32_INTR_CTRL_BASE     0x20010000UL

// -----------------------------------------------------------------------------
// Interrupt Controller Registers (Base: 0x20010000)
// -----------------------------------------------------------------------------
typedef struct {
    volatile uint32_t RAW_INTR;     // 0x00: Raw interrupt status
    volatile uint32_t dummy0;       // 0x04
    volatile uint32_t INTR_EN;      // 0x08: Interrupt enable bitmask
    volatile uint32_t dummy1;       // 0x0C
    volatile uint32_t INTR_STATUS;  // 0x10: Active interrupt status
} thejas32_intr_ctrl_t;

#define THEJAS32_INTR_CTRL (*((thejas32_intr_ctrl_t *)THEJAS32_INTR_CTRL_BASE))

// Interrupt numbers for 32-bit THEJAS32
#define THEJAS32_IRQ_UART0          0
#define THEJAS32_IRQ_UART1          1
#define THEJAS32_IRQ_UART2          2
#define THEJAS32_IRQ_SPI0           3
#define THEJAS32_IRQ_SPI1           4
#define THEJAS32_IRQ_I2C0           5
#define THEJAS32_IRQ_I2C1           6
#define THEJAS32_IRQ_TIMER0         7
#define THEJAS32_IRQ_TIMER1         8
#define THEJAS32_IRQ_TIMER2         9
#define THEJAS32_IRQ_GPIO0          10
#define THEJAS32_IRQ_GPIO1          11

typedef void (*thejas32_irq_handler_t)(void);
void thejas32_interrupt_register(uint8_t irq_number, thejas32_irq_handler_t handler);
void thejas32_interrupt_enable(uint8_t irq_number);
void thejas32_interrupt_disable(uint8_t irq_number);

// -----------------------------------------------------------------------------
// UART Registers (16550 compatible, Base: 0x10000100 + (0x100 * n))
// -----------------------------------------------------------------------------
typedef struct {
    volatile uint32_t UART_DR;      // 0x00: Data reg (or Divisor Latch LSB when DLAB=1)
    volatile uint32_t UART_IE;      // 0x04: Interrupt enable (or Divisor Latch MSB when DLAB=1)
    volatile uint32_t UART_IIR_FCR; // 0x08: Interrupt ID reg / FIFO control reg
    volatile uint32_t UART_LCR;     // 0x0C: Line control reg (Bit 7: DLAB, Bits 1:0: Data len)
    volatile uint32_t Dummy;        // 0x10
    volatile uint32_t UART_LSR;     // 0x14: Line status reg (Bit 0: DR, Bit 5: THRE)
} thejas32_uart_reg_t;

#define THEJAS32_UART(n) (*((thejas32_uart_reg_t *)(THEJAS32_UART_BASE + ((n) * 0x100))))

// -----------------------------------------------------------------------------
// Hardware Timer Registers (Base: 0x10000A00 + (0x14 * n))
// -----------------------------------------------------------------------------
typedef struct {
    volatile uint32_t LoadCount;    // 0x00: Reload value
    volatile uint32_t CurrentValue; // 0x04: Current down-counter value
    volatile uint32_t Control;      // 0x08: Control reg (Bit 0: Enable, Bit 1: Mode, Bit 2: Mask)
    volatile uint32_t EOI;          // 0x0C: Read to clear interrupt
    volatile uint32_t IntrStatus;   // 0x10: Interrupt status bit
} thejas32_timer_reg_t;

#define THEJAS32_TIMER(n) (*((thejas32_timer_reg_t *)(THEJAS32_TIMER_BASE + ((n) * 0x14))))

// Hardware Timer functions
void thejas32_timer_init(void);
void thejas32_timer0_isr(void);
uint32_t timer0GetCurrentValue(void);
uint32_t timer0GetOverflows(void);
timeUs_t timer0Micros(void);
uint64_t getCycleCounter64(void);

