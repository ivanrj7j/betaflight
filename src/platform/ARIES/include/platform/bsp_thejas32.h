/*
 * This file is part of Betaflight.
 *
 * Platform register mappings and peripheral base addresses for C-DAC ARIES v3.0
 * (THEJAS32 SoC / VEGA ET1031 RISC-V RV32IM @ 100 MHz).
 *
 * Citations:
 *   - lib/vega-sdk/vega-sdk/bsp/drivers/interrupt/interrupt.c
 *   - lib/vega-sdk/vega-sdk/bsp/include/encoding.h
 *   - knowledgebase/components/09_BUILD_LINKER_AND_MEMORY.md
 *   - knowledgebase/components/03_SERIAL_UART_AND_MSP.md
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Memory Map Boundaries (Silicon Ground Truth 2: In-SRAM Execution)
 * Total Internal SRAM: 256 KB (0x00200000 - 0x0023FFFF)
 * Bootloader Reserved Partition: 8 KB (0x00200000 - 0x00201FFF)
 * Usable Application SRAM: 248 KB (0x00202000 - 0x0023FFFF)
 */
#define THEJAS32_SRAM_BASE          0x00200000UL
#define THEJAS32_SRAM_ORIGIN        0x00202000UL
#define THEJAS32_SRAM_CEILING       0x0023FFFFUL

/* Peripheral APB/MMIO Base Addresses */
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
uint32_t timer0Micros(void);
uint64_t getCycleCounter64(void);

// -----------------------------------------------------------------------------
// SPI Controller Registers
// Base: 0x10000600 (SPI0/SPI1), 0x10200100 (SPI2/SPI3)
// -----------------------------------------------------------------------------
typedef struct {
    volatile uint32_t Control;      // 0x00: Packed bitfield / control word
    volatile uint32_t Status;       // 0x04: Status reg (Busy, Overrun, RxComplete, TxHoldEmpty)
    volatile uint32_t Baudrate;     // 0x08: Baud frequency divider
    volatile uint32_t TxData;       // 0x0C: Transmit data
    volatile uint32_t RxData;       // 0x10: Receive data
} thejas32_spi_reg_t;

#ifdef UNIT_TEST
extern thejas32_intr_ctrl_t thejas32_host_intr_ctrl;
extern thejas32_uart_reg_t  thejas32_host_uart[3];
extern thejas32_timer_reg_t thejas32_host_timer[3];
extern thejas32_spi_reg_t   thejas32_host_spi[4];
extern uint16_t            thejas32_host_gpio_dir[2];
extern uint16_t            thejas32_host_gpio_data[2][16];
extern uint32_t            thejas32_host_fence_count;

#undef THEJAS32_INTR_CTRL
#undef THEJAS32_UART
#undef THEJAS32_TIMER
#define THEJAS32_INTR_CTRL (thejas32_host_intr_ctrl)
#define THEJAS32_UART(n)   (thejas32_host_uart[(n)])
#define THEJAS32_TIMER(n)  (thejas32_host_timer[(n)])
#define THEJAS32_SPI(n)    (thejas32_host_spi[(n)])

#define THEJAS32_MEMORY_FENCE() do { thejas32_host_fence_count++; __atomic_thread_fence(__ATOMIC_SEQ_CST); } while (0)

static inline volatile uint16_t *thejas32_gpio_dir_ptr(uint16_t pin)
{
    return (volatile uint16_t *)&thejas32_host_gpio_dir[(pin < 16U) ? 0U : 1U];
}

static inline volatile uint16_t *thejas32_gpio_data_ptr(uint16_t pin)
{
    return (volatile uint16_t *)&thejas32_host_gpio_data[(pin < 16U) ? 0U : 1U][pin & 0x0FU];
}

static inline thejas32_spi_reg_t *thejas32_spi_from_instance(const void *instance)
{
    uintptr_t addr = (uintptr_t)instance;
    if (addr == THEJAS32_SPI_0_1_BASE || addr == 0U) {
        return &thejas32_host_spi[0];
    }
    if (addr == (THEJAS32_SPI_0_1_BASE + 0x100U)) {
        return &thejas32_host_spi[1];
    }
    if (addr == THEJAS32_SPI_2_3_BASE) {
        return &thejas32_host_spi[2];
    }
    if (addr == (THEJAS32_SPI_2_3_BASE + 0x100U)) {
        return &thejas32_host_spi[3];
    }
    return &thejas32_host_spi[0];
}

#define SPI1 ((thejas32_spi_reg_t *)&thejas32_host_spi[0])
#else
#define THEJAS32_SPI(n) (*((thejas32_spi_reg_t *)(((n) < 2 ? THEJAS32_SPI_0_1_BASE : THEJAS32_SPI_2_3_BASE) + (((n) % 2) * 0x100))))

// Standard Betaflight SPI peripheral instance definition for Header J1 SPI0
#define SPI1 ((thejas32_spi_reg_t *)THEJAS32_SPI_0_1_BASE)

#define THEJAS32_MEMORY_FENCE() __asm__ volatile ("fence" : : : "memory")

__attribute__((always_inline)) static inline volatile uint16_t *thejas32_gpio_dir_ptr(uint16_t pin)
{
    uint32_t base = (pin < 16U) ? THEJAS32_GPIO_0_BASE : THEJAS32_GPIO_1_BASE;
    return (volatile uint16_t *)(base + 0x40000UL);
}

__attribute__((always_inline)) static inline volatile uint16_t *thejas32_gpio_data_ptr(uint16_t pin)
{
    uint32_t base = (pin < 16U) ? THEJAS32_GPIO_0_BASE : THEJAS32_GPIO_1_BASE;
    uint8_t pin_in_bank = (uint8_t)(pin & 0x0FU);
    return (volatile uint16_t *)(base + ((1U << pin_in_bank) << 2));
}

__attribute__((always_inline)) static inline thejas32_spi_reg_t *thejas32_spi_from_instance(const void *instance)
{
    return (instance == NULL) ? (thejas32_spi_reg_t *)THEJAS32_SPI_0_1_BASE : (thejas32_spi_reg_t *)instance;
}
#endif

__attribute__((always_inline)) static inline uint32_t thejas32_uart_read_lsr(uint8_t ch)
{
    uint32_t lsr = THEJAS32_UART(ch).UART_LSR;
#ifdef UNIT_TEST
    // 16550 reading LSR clears latched OE/PE/FE/BI error flags (bits 4:1)
    THEJAS32_UART(ch).UART_LSR = lsr & ~0x1EUL;
#endif
    return lsr;
}

__attribute__((always_inline)) static inline uint8_t thejas32_uart_read_rx(uint8_t ch)
{
    uint8_t dr = (uint8_t)(THEJAS32_UART(ch).UART_DR & 0xFFU);
#ifdef UNIT_TEST
    // 16550 non-FIFO 1-byte mode clears Data Ready (bit 0) upon reading RBR
    THEJAS32_UART(ch).UART_LSR &= ~0x01UL;
#endif
    return dr;
}

#ifdef __cplusplus
}
#endif
