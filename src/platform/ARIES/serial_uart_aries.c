/*
 * This file is part of Betaflight.
 *
 * Hardware UART driver for C-DAC ARIES v3.0 (THEJAS32 / VEGA ET1031).
 * Implements UART0 (CP2102N USB-UART bridge for CLI/MSP) and UART1 (Serial RX).
 * Based on 16550-compatible UART registers from C-DAC VEGA SDK BSP.
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "platform.h"

#ifdef USE_UART

#include "build/build_config.h"
#include "common/utils.h"

#include "drivers/io.h"
#include "drivers/serial.h"
#include "drivers/serial_uart.h"
#include "drivers/serial_impl.h"
#include "drivers/serial_uart_impl.h"

#include "platform/bsp_thejas32.h"

// UART Hardware definitions table for ARIES v3.0
const uartHardware_t uartHardware[UARTDEV_COUNT] = {
#ifdef USE_UART0
    {
        .identifier = SERIAL_PORT_UART0,
        .reg = (usartResource_t *)THEJAS32_UART_BASE, // 0x10000100
        .rxPins = { { .pin = DEFIO_TAG__NONE }, },
        .txPins = { { .pin = DEFIO_TAG__NONE }, },
        .af = 0,
        .irqn = THEJAS32_IRQ_UART0,
        .txPriority = 0,
        .rxPriority = 0,
        .txBuffer = uart0TxBuffer,
        .rxBuffer = uart0RxBuffer,
        .txBufferSize = sizeof(uart0TxBuffer),
        .rxBufferSize = sizeof(uart0RxBuffer),
    },
#endif
#ifdef USE_UART1
    {
        .identifier = SERIAL_PORT_USART1,
        .reg = (usartResource_t *)(THEJAS32_UART_BASE + 0x100), // 0x10000200
        .rxPins = { { .pin = DEFIO_TAG__NONE }, },
        .txPins = { { .pin = DEFIO_TAG__NONE }, },
        .af = 0,
        .irqn = THEJAS32_IRQ_UART1,
        .txPriority = 0,
        .rxPriority = 0,
        .txBuffer = uart1TxBuffer,
        .rxBuffer = uart1RxBuffer,
        .txBufferSize = sizeof(uart1TxBuffer),
        .rxBufferSize = sizeof(uart1RxBuffer),
    },
#endif
};

// Convert hardware register pointer to THEJAS32 UART channel index (0 or 1)
static inline uint8_t uartGetIndex(const usartResource_t *reg)
{
    uint32_t addr = (uint32_t)reg;
    if (addr >= (THEJAS32_UART_BASE + 0x100)) {
        return 1;
    }
    return 0;
}

// -----------------------------------------------------------------------------
// Pin Configuration
// -----------------------------------------------------------------------------
void uartPinConfigure(const serialPinConfig_t *pSerialPinConfig)
{
    for (const uartHardware_t *hardware = uartHardware; hardware < ARRAYEND(uartHardware); hardware++) {
        const serialPortIdentifier_e identifier = hardware->identifier;
        uartDevice_t *uartdev = uartDeviceFromIdentifier(identifier);
        const int resourceIndex = serialResourceIndex(identifier);
        if (uartdev == NULL || resourceIndex < 0) {
            continue;
        }

        // On ARIES v3.0, UART0 is wired directly to the onboard CP2102N USB bridge,
        // and UART1 is wired to expansion header J2 pins 13 & 15.
        // Always bind the hardware definition so uartOpen succeeds even if pin is NONE.
        uartdev->hardware = hardware;
        uartdev->rx.pin = pSerialPinConfig->ioTagRx[resourceIndex];
        uartdev->tx.pin = pSerialPinConfig->ioTagTx[resourceIndex];
    }
}

// -----------------------------------------------------------------------------
// Baud Rate & Line Control Configuration
// -----------------------------------------------------------------------------
static void thejas32_uart_set_baudrate(uint8_t ch, uint32_t baudRate)
{
    if (baudRate == 0) {
        baudRate = 115200;
    }

    // Formula from VEGA SDK: Divisor = Input_Clock / (Baud_rate * 16)
    // At 100 MHz SystemCoreClock: 100,000,000 / (115200 * 16) = 54 (0x0036)
    uint32_t divisor = SystemCoreClock / (baudRate * 16);
    if (divisor == 0) {
        divisor = 1;
    }

    // Set DLAB (Divisor Latch Access Bit) = 1 (Bit 7 of UART_LCR)
    THEJAS32_UART(ch).UART_LCR = 0x83;
    THEJAS32_UART(ch).UART_DR = divisor & 0xFF;        // LSB
    THEJAS32_UART(ch).UART_IE = (divisor >> 8) & 0xFF;  // MSB

    // Clear DLAB: 8 data bits, 1 stop bit, no parity (0x03)
    THEJAS32_UART(ch).UART_LCR = 0x03;

    // Reset and enable FIFOs
    THEJAS32_UART(ch).UART_IIR_FCR = 0x07; // Enable FIFO, reset RX & TX FIFOs
    __asm__ __volatile__ ("fence");
}

void uartReconfigure(uartPort_t *uartPort)
{
    uint8_t ch = uartGetIndex(uartPort->USARTx);

    thejas32_uart_set_baudrate(ch, uartPort->port.baudRate);

    // Stop bits & parity
    uint8_t lcr = 0x03; // Default: 8 bits, 1 stop bit, no parity
    if (uartPort->port.options & SERIAL_STOPBITS_2) {
        lcr |= 0x04; // 2 stop bits
    }
    if (uartPort->port.options & SERIAL_PARITY_EVEN) {
        lcr |= 0x18; // Even parity enable
    }
    THEJAS32_UART(ch).UART_LCR = lcr;

    // Enable RX interrupt (bit 0 of UART_IE) if in RX mode
    if (uartPort->port.mode & MODE_RX) {
        THEJAS32_UART(ch).UART_IE |= 0x01;
    } else {
        THEJAS32_UART(ch).UART_IE &= ~0x01;
    }

    __asm__ __volatile__ ("fence");
}

// -----------------------------------------------------------------------------
// Interrupt Handling
// -----------------------------------------------------------------------------
void uartEnableTxInterrupt(uartPort_t *uartPort)
{
    uint8_t ch = uartGetIndex(uartPort->USARTx);

    // If there is data waiting to be sent, enable THRE interrupt (bit 1 of UART_IE)
    if (uartPort->port.txBufferTail != uartPort->port.txBufferHead) {
        THEJAS32_UART(ch).UART_IE |= 0x02;
        __asm__ __volatile__ ("fence");

        // If transmitter is currently empty, prime the transmit loop immediately
        if (THEJAS32_UART(ch).UART_LSR & 0x20) {
            uint8_t byte = uartPort->port.txBuffer[uartPort->port.txBufferTail];
            THEJAS32_UART(ch).UART_DR = byte;
            uartPort->port.txBufferTail = (uartPort->port.txBufferTail + 1) % uartPort->port.txBufferSize;
            __asm__ __volatile__ ("fence");
        }
    }
}

FAST_IRQ_HANDLER void uartIrqHandler(uartPort_t *s)
{
    uint8_t ch = uartGetIndex(s->USARTx);

    // Read Line Status Register
    uint32_t lsr = THEJAS32_UART(ch).UART_LSR;

    // 1. Check for incoming data (LSR Bit 0: Data Ready)
    while (lsr & 0x01) {
        uint8_t rxChar = THEJAS32_UART(ch).UART_DR & 0xFF;
        if (s->port.rxCallback) {
            s->port.rxCallback(rxChar, s->port.rxCallbackData);
        } else {
            s->port.rxBuffer[s->port.rxBufferHead] = rxChar;
            s->port.rxBufferHead = (s->port.rxBufferHead + 1) % s->port.rxBufferSize;
        }
        lsr = THEJAS32_UART(ch).UART_LSR;
    }

    // 2. Check if transmitter holding register is empty (LSR Bit 5: THRE)
    if (lsr & 0x20) {
        if (s->port.txBufferTail != s->port.txBufferHead) {
            uint8_t txChar = s->port.txBuffer[s->port.txBufferTail];
            THEJAS32_UART(ch).UART_DR = txChar;
            s->port.txBufferTail = (s->port.txBufferTail + 1) % s->port.txBufferSize;
            __asm__ __volatile__ ("fence");
        } else {
            // No more data to transmit: disable TX interrupt
            THEJAS32_UART(ch).UART_IE &= ~0x02;
            __asm__ __volatile__ ("fence");
        }
    }
}

// ISR Callback wrappers for THEJAS32 interrupt controller
static void thejas32_uart0_isr(void)
{
    uartIrqHandler(&uartDevice[UARTDEV_0].port);
}

#ifdef USE_UART1
static void thejas32_uart1_isr(void)
{
    uartIrqHandler(&uartDevice[UARTDEV_1].port);
}
#endif

// -----------------------------------------------------------------------------
// Driver Initialization & Open
// -----------------------------------------------------------------------------
uartPort_t *serialUART(uartDevice_t *uartdev, uint32_t baudRate, portMode_e mode, portOptions_e options)
{
    UNUSED(options);

    uartPort_t *s = &uartdev->port;
    const uartHardware_t *hardware = uartdev->hardware;
    if (!hardware) {
        return NULL;
    }

    uint8_t ch = uartGetIndex(hardware->reg);

    // Initialize hardware registers
    thejas32_uart_set_baudrate(ch, baudRate);

    // Register with THEJAS32 interrupt controller
    if (ch == 0) {
        thejas32_interrupt_register(THEJAS32_IRQ_UART0, thejas32_uart0_isr);
        thejas32_interrupt_enable(THEJAS32_IRQ_UART0);
    }
#ifdef USE_UART1
    else if (ch == 1) {
        thejas32_interrupt_register(THEJAS32_IRQ_UART1, thejas32_uart1_isr);
        thejas32_interrupt_enable(THEJAS32_IRQ_UART1);
    }
#endif

    // Setup buffer pointers
    s->port.rxBuffer = hardware->rxBuffer;
    s->port.txBuffer = hardware->txBuffer;
    s->port.rxBufferSize = hardware->rxBufferSize;
    s->port.txBufferSize = hardware->txBufferSize;
    s->USARTx = hardware->reg;

    s->port.vTable = uartVTable;

    // If RX mode requested, enable RX interrupt
    if (mode & MODE_RX) {
        THEJAS32_UART(ch).UART_IE |= 0x01;
    }

    return s;
}

void uartConfigureExternalPinInversion(uartPort_t *uartPort)
{
    UNUSED(uartPort);
}

void uartTryStartTxDMA(uartPort_t *s)
{
    UNUSED(s);
}

void uartConfigureDma(uartDevice_t *uartdev)
{
    UNUSED(uartdev);
}

#endif // USE_UART
