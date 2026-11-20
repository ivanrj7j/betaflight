/*
 * This file is part of Betaflight.
 *
 * Hardware UART driver for C-DAC ARIES v3.0 (THEJAS32 / VEGA ET1031 RISC-V RV32IM @ 100 MHz).
 * Implements 16550 APB UART0 (CP2102N USB bridge on PA30/PA31) and UART1 (Serial RX on PA17/PA16).
 * Based on 16550-compatible UART IP from C-DAC VEGA SDK BSP.
 *
 * Citations:
 *   - PLAN/03_SERIAL_COMMUNICATION_MSP_AND_CLI.md (Microcomponents 03_A & 03_B)
 *   - knowledgebase/codebase_evaluations/06_EVAL_SERIAL_UART_ARIES_C.md (Score: 9.5/10 RX IRQ, 9.0/10 Divisor 54)
 *   - knowledgebase/components/03_SERIAL_UART_AND_MSP.md
 *   - lib/vega-sdk/vega-sdk/bsp/drivers/uart/uart.c
 *   - lib/vega-sdk/vega-sdk/bsp/include/uart.h
 *   - src/platform/ARIES/include/platform/bsp_thejas32.h
 *   - src/platform/ARIES/target/ARIESV3/target.h
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

// -----------------------------------------------------------------------------
// Hardware UART Resource Table for C-DAC ARIES v3.0
// -----------------------------------------------------------------------------
const uartHardware_t uartHardware[UARTDEV_COUNT] = {
#ifdef USE_UART0
    {
        .identifier = SERIAL_PORT_UART0,
        .reg = (usartResource_t *)THEJAS32_UART_BASE, // 0x10000100 (CP2102N USB Bridge)
        .rxPins = { { .pin = DEFIO_TAG_E(PA31) }, },
        .txPins = { { .pin = DEFIO_TAG_E(PA30) }, },
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
        .reg = (usartResource_t *)(THEJAS32_UART_BASE + 0x100), // 0x10000200 (Header J2 Serial RX)
        .rxPins = { { .pin = DEFIO_TAG_E(PA17) }, },
        .txPins = { { .pin = DEFIO_TAG_E(PA16) }, },
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
    uint32_t addr = (uint32_t)(uintptr_t)reg;
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

        // On ARIES v3.0, UART0 is wired directly to onboard CP2102N USB bridge,
        // and UART1 is wired to expansion header pins PA16/PA17.
        // Always bind the hardware definition so uartOpen succeeds even if pin tag is 0.
        uartdev->hardware = hardware;
        uartdev->rx.pin = pSerialPinConfig->ioTagRx[resourceIndex];
        uartdev->tx.pin = pSerialPinConfig->ioTagTx[resourceIndex];
        if (!uartdev->rx.pin && hardware->rxPins[0].pin) {
            uartdev->rx.pin = hardware->rxPins[0].pin;
        }
        if (!uartdev->tx.pin && hardware->txPins[0].pin) {
            uartdev->tx.pin = hardware->txPins[0].pin;
        }
    }
}

// -----------------------------------------------------------------------------
// Baud Rate & Line Control Configuration (16550 IP DLAB Latching)
// -----------------------------------------------------------------------------
// Note on High-Speed Baud Rates (100 MHz APB Clock Limitation):
// The 16550 APB UART IP uses integer division: Divisor = (100MHz + (baud * 8)) / (baud * 16).
// - 115,200 baud: Divisor 54 -> 115,740.74 bps (+0.47% error, optimal for USB MSP/CLI)
// - 230,400 baud: Divisor 27 -> 231,481.48 bps (+0.47% error, recommended for high-speed logging)
// - 420,000 baud: Divisor 15 -> 416,666.67 bps (-0.79% error, optimal for CRSF/ELRS receiver)
// - 921,600 baud: Divisor  7 -> 892,857.14 bps (-3.12% error, EXCEEDS +/- 2.5% UART tolerance!)
// Warning: 921,600 baud will cause framing errors on 100 MHz APB clock without fractional baud hardware.
// Use 230,400 baud or 420,000 baud for high-speed serial links.
static void thejas32_uart_set_baudrate_ex(uint8_t ch, uint32_t baudRate, bool resetFifo)
{
    if (baudRate == 0) {
        baudRate = 115200;
    }

    // Formula from 16550 IP & VEGA SDK:
    // Divisor = (Clock + (Baud * 8)) / (Baud * 16) [Rounds to nearest integer]
    // For 115,200 baud @ 100 MHz APB: Divisor = 54 (+0.469% error)
    // For 420,000 baud @ 100 MHz APB: Divisor = 15 (-0.794% error)
    uint32_t divisor = (SystemCoreClock + (baudRate * 8U)) / (baudRate * 16U);
    if (divisor == 0) {
        divisor = 1;
    }

    // Step 1: Set DLAB (Divisor Latch Access Bit) = 1 (Bit 7 of UART_LCR)
    THEJAS32_UART(ch).UART_LCR = 0x83; // DLAB=1, 8 data bits
    __asm__ volatile ("fence" : : : "memory");

    // Step 2: Write Divisor Latches (DLL in UART_DR, DLM in UART_IE)
    THEJAS32_UART(ch).UART_DR = divisor & 0xFF;        // DLL (LSB)
    THEJAS32_UART(ch).UART_IE = (divisor >> 8) & 0xFF; // DLM (MSB)
    __asm__ volatile ("fence" : : : "memory");

    // Step 3: Clear DLAB and lock 8N1 framing (0x03)
    THEJAS32_UART(ch).UART_LCR = 0x03; // 8 data bits, 1 stop bit, no parity
    __asm__ volatile ("fence" : : : "memory");

    // Step 4: Configure 16-byte hardware FIFOs (FCR = 0x07 on cold reset, 0x01 on reconfigure)
    THEJAS32_UART(ch).UART_IIR_FCR = resetFifo ? 0x07 : 0x01;
    __asm__ volatile ("fence" : : : "memory");
}

static inline void thejas32_uart_set_baudrate(uint8_t ch, uint32_t baudRate)
{
    thejas32_uart_set_baudrate_ex(ch, baudRate, true);
}

#ifndef SERIAL_PARITY_ODD
#define SERIAL_PARITY_ODD (2 << 2) // Optional extension for odd parity checking
#endif

void uartReconfigure(uartPort_t *uartPort)
{
    uint8_t ch = uartGetIndex(uartPort->USARTx);

    thejas32_uart_set_baudrate_ex(ch, uartPort->port.baudRate, false);

    // Configure stop bits & parity in LCR
    uint8_t lcr = 0x03; // Default: 8 data bits, 1 stop bit, no parity
    if (uartPort->port.options & SERIAL_STOPBITS_2) {
        lcr |= 0x04; // 2 stop bits
    }
    if (uartPort->port.options & SERIAL_PARITY_EVEN) {
        lcr |= 0x18; // Even parity enable (PEN=1, EPS=1)
    }
    if (uartPort->port.options & SERIAL_PARITY_ODD) {
        lcr |= 0x08; // Odd parity enable (PEN=1, EPS=0)
    }
    THEJAS32_UART(ch).UART_LCR = lcr;

    // Ground Truth 8: Enable RX-only interrupt (bit 0 of UART_IE).
    // TX interrupt (bit 1) must remain strictly 0 to prevent level-triggered THRE storms!
    if (uartPort->port.mode & MODE_RX) {
        THEJAS32_UART(ch).UART_IE = 0x01;
    } else {
        THEJAS32_UART(ch).UART_IE = 0x00;
    }

    __asm__ volatile ("fence" : : : "memory");
}

// -----------------------------------------------------------------------------
// Bounded Synchronous FIFO Transmit Drain (Anti-Starvation Engine)
// -----------------------------------------------------------------------------
void uartEnableTxInterrupt(uartPort_t *uartPort)
{
    if (!uartPort || !uartPort->USARTx || !uartPort->port.txBuffer || uartPort->port.txBufferSize == 0) {
        return;
    }

    uint8_t ch = uartGetIndex(uartPort->USARTx);

    // TX interrupts are permanently disabled to prevent level-triggered THRE interrupt storms.
    // Instead of an unbounded while loop that could stall the 1.0 kHz flight loop for > 17 ms,
    // drain up to 16 bytes (the hardware FIFO depth) while LSR THRE is set, taking < 2.0 us bus cycles.
    uint8_t bytesWritten = 0;
    while ((uartPort->port.txBufferTail != uartPort->port.txBufferHead) &&
           (THEJAS32_UART(ch).UART_LSR & 0x20) &&
           (bytesWritten < 16)) {
        THEJAS32_UART(ch).UART_DR = uartPort->port.txBuffer[uartPort->port.txBufferTail];
        uartPort->port.txBufferTail = (uartPort->port.txBufferTail + 1) % uartPort->port.txBufferSize;
        bytesWritten++;
        __asm__ volatile ("fence" : : : "memory");
    }
}

// Periodic / polled transmit queue drain hook for background tasks and scheduler
void thejas32_uart_poll_tx(void)
{
    for (int dev = 0; dev < UARTDEV_COUNT; dev++) {
        uartPort_t *s = &uartDevice[dev].port;
        if (s->USARTx && s->port.txBuffer && (s->port.txBufferTail != s->port.txBufferHead)) {
            uartEnableTxInterrupt(s);
        }
    }
}

// -----------------------------------------------------------------------------
// Interrupt Handling (RX-Only Ring Buffer Ingestion & ORE Recovery)
// -----------------------------------------------------------------------------
FAST_IRQ_HANDLER void uartIrqHandler(uartPort_t *s)
{
    if (!s || !s->USARTx || !s->port.rxBuffer || s->port.rxBufferSize == 0) {
        return;
    }

    uint8_t ch = uartGetIndex(s->USARTx);

    // Reading IIR acknowledges active interrupt condition
    volatile uint32_t iir = THEJAS32_UART(ch).UART_IIR_FCR;
    (void)iir;

    // Reading Line Status Register (LSR) automatically clears Overrun Error (ORE),
    // Parity Error (PE), and Framing Error (FE) flags on 16550 IP.
    uint32_t lsr = THEJAS32_UART(ch).UART_LSR;

    // Check Data Ready (LSR Bit 0) and ingest all available FIFO bytes
    while (lsr & 0x01) {
        uint8_t rxChar = THEJAS32_UART(ch).UART_DR & 0xFF;
        if (s->port.rxCallback) {
            s->port.rxCallback(rxChar, s->port.rxCallbackData);
        } else {
            // RX ring buffer head/tail collision guard prevents corrupting unread data
            uint16_t nextHead = (s->port.rxBufferHead + 1) % s->port.rxBufferSize;
            if (nextHead != s->port.rxBufferTail) {
                s->port.rxBuffer[s->port.rxBufferHead] = rxChar;
                s->port.rxBufferHead = nextHead;
            }
        }
        lsr = THEJAS32_UART(ch).UART_LSR;
    }

    // Opportunistically drain TX FIFO during interrupt service if transmitter ready
    if ((THEJAS32_UART(ch).UART_LSR & 0x20) && (s->port.txBufferTail != s->port.txBufferHead)) {
        uartEnableTxInterrupt(s);
    }

    __asm__ volatile ("fence" : : : "memory");
}

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

    // Initialize baud rate and 16-byte hardware FIFOs
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

    // Use read-only uartVTable located in .rodata (eliminates mutable RAM function pointers)
    s->port.vTable = uartVTable;

    // Ensure pin bindings so uartCanWrite always returns true
    if (!uartdev->tx.pin && hardware->txPins[0].pin) {
        uartdev->tx.pin = hardware->txPins[0].pin;
    }
    if (!uartdev->rx.pin && hardware->rxPins[0].pin) {
        uartdev->rx.pin = hardware->rxPins[0].pin;
    }

    // Enable RX interrupt only (bit 0 of UART_IE); bit 1 (TX) stays 0
    if (mode & MODE_RX) {
        THEJAS32_UART(ch).UART_IE = 0x01;
    } else {
        THEJAS32_UART(ch).UART_IE = 0x00;
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
