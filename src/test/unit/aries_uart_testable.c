/*
 * Host-testable implementation of ARIES v3.0 UART 16550 Driver logic.
 * Mirrors src/platform/ARIES/serial_uart_aries.c using modular mock infrastructure.
 */

#include "aries_uart_testable.h"
#include "aries/mock_mmio.h"
#include <string.h>

#define THEJAS32_UART_BASE 0x10000100UL

// Layout matching thejas32_uart_reg_t
typedef struct {
    volatile uint32_t UART_DR;
    volatile uint32_t UART_IE;
    volatile uint32_t UART_IIR_FCR;
    volatile uint32_t UART_LCR;
    volatile uint32_t Dummy;
    volatile uint32_t UART_LSR;
} thejas32_mock_uart_reg_t;

#define MOCK_UART(ch) (*((thejas32_mock_uart_reg_t *)&mock_periph_mem[0x100 + ((ch) * 0x100)]))

uint32_t SystemCoreClock = 100000000;

uint8_t test_uartGetIndex(uint32_t reg_addr)
{
    if (reg_addr >= (THEJAS32_UART_BASE + 0x100)) {
        return 1;
    }
    return 0;
}

void test_thejas32_uart_set_baudrate(uint8_t ch, uint32_t baudRate)
{
    if (baudRate == 0) {
        baudRate = 115200;
    }

    uint32_t divisor = SystemCoreClock / (baudRate * 16);
    if (divisor == 0) {
        divisor = 1;
    }

    // Set DLAB = 1 (Bit 7 of UART_LCR)
    MOCK_UART(ch).UART_LCR = 0x83;
    MOCK_UART(ch).UART_DR = divisor & 0xFF;        // LSB
    MOCK_UART(ch).UART_IE = (divisor >> 8) & 0xFF;  // MSB

    // Clear DLAB: 8 data bits, 1 stop bit, no parity (0x03)
    MOCK_UART(ch).UART_LCR = 0x03;

    // Reset and enable FIFOs
    MOCK_UART(ch).UART_IIR_FCR = 0x07;
}

void test_uartReconfigure(test_uart_port_t *uartPort)
{
    uint8_t ch = test_uartGetIndex(uartPort->reg_base);

    test_thejas32_uart_set_baudrate(ch, uartPort->port.baudRate);

    uint8_t lcr = 0x03; // Default 8N1
    if (uartPort->port.options & TEST_SERIAL_STOPBITS_2) {
        lcr |= 0x04;
    }
    if (uartPort->port.options & TEST_SERIAL_PARITY_EVEN) {
        lcr |= 0x18;
    }
    MOCK_UART(ch).UART_LCR = lcr;

    if (uartPort->port.mode & TEST_MODE_RX) {
        MOCK_UART(ch).UART_IE |= UART_IE_RDA;
    } else {
        MOCK_UART(ch).UART_IE &= ~UART_IE_RDA;
    }
}

void test_uartEnableTxInterrupt(test_uart_port_t *uartPort)
{
    uint8_t ch = test_uartGetIndex(uartPort->reg_base);

    if (uartPort->port.txBufferTail != uartPort->port.txBufferHead) {
        MOCK_UART(ch).UART_IE |= UART_IE_THRE;

        if (MOCK_UART(ch).UART_LSR & UART_LSR_THRE) {
            uint8_t byte = uartPort->port.txBuffer[uartPort->port.txBufferTail];
            MOCK_UART(ch).UART_DR = byte;
            uartPort->port.txBufferTail = (uartPort->port.txBufferTail + 1) % uartPort->port.txBufferSize;
        }
    }
}

void test_uartIrqHandler(test_uart_port_t *s)
{
    uint8_t ch = test_uartGetIndex(s->reg_base);
    uint32_t lsr = MOCK_UART(ch).UART_LSR;

    // 1. Process RX
    while (lsr & UART_LSR_DR) {
        uint8_t rxChar = (uint8_t)(MOCK_UART(ch).UART_DR & 0xFF);
        // On 16550 hardware, reading DR pops the character from RX FIFO and clears DR flag
        MOCK_UART(ch).UART_LSR &= ~UART_LSR_DR;
        if (s->port.rxCallback) {
            s->port.rxCallback(rxChar, s->port.rxCallbackData);
        } else {
            s->port.rxBuffer[s->port.rxBufferHead] = rxChar;
            s->port.rxBufferHead = (s->port.rxBufferHead + 1) % s->port.rxBufferSize;
        }
        lsr = MOCK_UART(ch).UART_LSR;
    }

    // 2. Process TX
    if (lsr & UART_LSR_THRE) {
        if (s->port.txBufferTail != s->port.txBufferHead) {
            uint8_t txChar = s->port.txBuffer[s->port.txBufferTail];
            MOCK_UART(ch).UART_DR = txChar;
            s->port.txBufferTail = (s->port.txBufferTail + 1) % s->port.txBufferSize;
        } else {
            MOCK_UART(ch).UART_IE &= ~UART_IE_THRE;
        }
    }
}

uint32_t test_uart_read_reg(uint8_t ch, uint32_t offset)
{
    volatile uint32_t *p = (volatile uint32_t *)&mock_periph_mem[0x100 + ((ch) * 0x100) + offset];
    return *p;
}

void test_uart_write_reg(uint8_t ch, uint32_t offset, uint32_t val)
{
    volatile uint32_t *p = (volatile uint32_t *)&mock_periph_mem[0x100 + ((ch) * 0x100) + offset];
    *p = val;
}
