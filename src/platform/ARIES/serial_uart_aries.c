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

// Convert hardware register pointer to THEJAS32 UART channel index (0, 1, or -1 if invalid)
static inline int8_t uartGetIndex(const usartResource_t *reg)
{
    uintptr_t addr = (uintptr_t)reg;
    if (addr == THEJAS32_UART_BASE) {
        return 0;
    }
    if (addr == (THEJAS32_UART_BASE + 0x100U)) {
        return 1;
    }
    return -1;
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

#ifndef SERIAL_PARITY_ODD
#define SERIAL_PARITY_ODD (2 << 2) // Optional extension for odd parity checking
#endif

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
static inline uint32_t thejas32_uart_clamp_baudrate(uint32_t baudRate)
{
    if (baudRate == 0) {
        return 115200U;
    }
    // shortcut: baud > 460800 clamped to 420000 on 100 MHz APB (-0.79% vs -3.12% @ 921600), upgrade if fractional baud HW exists
    if (baudRate > 460800U) {
        return 420000U;
    }
    return baudRate;
}

static void thejas32_uart_set_baudrate_ex(uint8_t ch, uint32_t baudRate, portOptions_e options, bool resetFifo)
{
    baudRate = thejas32_uart_clamp_baudrate(baudRate);

    // Formula from 16550 IP & VEGA SDK:
    // Divisor = (Clock + (Baud * 8)) / (Baud * 16) [Rounds to nearest integer]
    // For 115,200 baud @ 100 MHz APB: Divisor = 54 (+0.469% error)
    // For 420,000 baud @ 100 MHz APB: Divisor = 15 (-0.794% error)
    uint32_t divisor = (SystemCoreClock + (baudRate * 8U)) / (baudRate * 16U);
    if (divisor == 0) {
        divisor = 1;
    }

    // Configure stop bits & parity in LCR (Finding 1: Dynamic framing for SBUS 8E2 / standard 8N1)
    uint8_t lcr = 0x03; // Default: 8 data bits, 1 stop bit, no parity
    if (options & SERIAL_STOPBITS_2) {
        lcr |= 0x04; // 2 stop bits (STB = 1)
    }
    if (options & SERIAL_PARITY_EVEN) {
        lcr |= 0x18; // Even parity enable (PEN = 1, EPS = 1)
    }
    if (options & SERIAL_PARITY_ODD) {
        lcr |= 0x08; // Odd parity enable (PEN = 1, EPS = 0)
    }

    // Step 1: Set DLAB (Divisor Latch Access Bit) = 1 (Bit 7 of UART_LCR) while maintaining framing
    THEJAS32_UART(ch).UART_LCR = 0x80 | lcr;
    THEJAS32_MEMORY_FENCE();

    // Step 2: Write Divisor Latches (DLL in UART_DR, DLM in UART_IE)
    THEJAS32_UART(ch).UART_DR = divisor & 0xFF;        // DLL (LSB)
    THEJAS32_UART(ch).UART_IE = (divisor >> 8) & 0xFF; // DLM (MSB)
    THEJAS32_MEMORY_FENCE();

    // Step 3: Clear DLAB and lock framing bits
    THEJAS32_UART(ch).UART_LCR = lcr;
    THEJAS32_MEMORY_FENCE();

    // shortcut: 1-byte UART register mode (FCR=0x00, no HW FIFO), upgrade when DW_apb_uart FIFO erratum is fixed in silicon
    if (resetFifo) {
        THEJAS32_UART(ch).UART_IIR_FCR = 0x00;
        THEJAS32_MEMORY_FENCE();
    }
}

static inline void thejas32_uart_set_baudrate(uint8_t ch, uint32_t baudRate, portOptions_e options)
{
    thejas32_uart_set_baudrate_ex(ch, baudRate, options, true);
}

void uartReconfigure(uartPort_t *uartPort)
{
    if (!uartPort || !uartPort->USARTx) {
        return;
    }
    int8_t ch = uartGetIndex(uartPort->USARTx);
    if (ch < 0) {
        return;
    }
    uartPort->port.baudRate = thejas32_uart_clamp_baudrate(uartPort->port.baudRate);

    thejas32_uart_set_baudrate_ex((uint8_t)ch, uartPort->port.baudRate, uartPort->port.options, false);

    // Ground Truth 8: Enable RX-only interrupt (bit 0 of UART_IE) after uartOpen() initializes rxCallback and buffers.
    // TX interrupt (bit 1) must remain strictly 0 to prevent level-triggered THRE storms.
    const uint8_t irq = (ch == 0) ? THEJAS32_IRQ_UART0 : THEJAS32_IRQ_UART1;
    if (uartPort->port.mode & MODE_RX) {
        THEJAS32_UART(ch).UART_IE = 0x01;
        thejas32_interrupt_enable(irq);
    } else {
        THEJAS32_UART(ch).UART_IE = 0x00;
        thejas32_interrupt_disable(irq);
    }

    THEJAS32_MEMORY_FENCE();
}

// -----------------------------------------------------------------------------
// Bounded Synchronous Transmit Drain (Anti-Starvation Engine)
// -----------------------------------------------------------------------------
void uartEnableTxInterrupt(uartPort_t *uartPort)
{
    if (!uartPort || !uartPort->USARTx || !uartPort->port.txBuffer || uartPort->port.txBufferSize == 0) {
        return;
    }

    int8_t ch = uartGetIndex(uartPort->USARTx);
    if (ch < 0) {
        return;
    }

    // shortcut: non-FIFO mode (FCR=0x00) holds only 1-2 bytes before THRE clears (~100-200 B/s @ 100 Hz poll), upgrade to 1 kHz TX poll or DMA if saturated
    uint8_t bytesWritten = 0;
    while ((uartPort->port.txBufferTail != uartPort->port.txBufferHead) &&
           (THEJAS32_UART(ch).UART_LSR & 0x20) &&
           (bytesWritten < 16)) {
        THEJAS32_UART(ch).UART_DR = uartPort->port.txBuffer[uartPort->port.txBufferTail];
        uartPort->port.txBufferTail = (uartPort->port.txBufferTail + 1) % uartPort->port.txBufferSize;
        bytesWritten++;
        THEJAS32_MEMORY_FENCE();
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

// Periodic / polled receive drain hook: enters uartIrqHandler() directly so LSR FE/PE flags are never wiped before inspection
void thejas32_uart_poll_rx(void)
{
    for (int dev = 0; dev < UARTDEV_COUNT; dev++) {
        uartPort_t *s = &uartDevice[dev].port;
        if (s->USARTx && s->port.rxBuffer && s->port.rxBufferSize > 0) {
            uint32_t prev = thejas32_atomic_enter();
            uartIrqHandler(s);
            thejas32_atomic_exit(prev);
        }
    }
}

void uartPollRx(void)
{
    thejas32_uart_poll_rx();
}

void uartPollTx(void)
{
    thejas32_uart_poll_tx();
}

// -----------------------------------------------------------------------------
// Interrupt Handling (RX-Only Ring Buffer Ingestion & ORE Recovery)
// -----------------------------------------------------------------------------
FAST_IRQ_HANDLER void uartIrqHandler(uartPort_t *s)
{
    if (!s || !s->USARTx || !s->port.rxBuffer || s->port.rxBufferSize == 0) {
        return;
    }

    int8_t ch = uartGetIndex(s->USARTx);
    if (ch < 0) {
        return;
    }

    // Reading Line Status Register (LSR) automatically clears Overrun Error (ORE),
    // Parity Error (PE), and Framing Error (FE) flags on 16550 IP.
    uint32_t lsr = thejas32_uart_read_lsr((uint8_t)ch);

    // Check Data Ready (LSR Bit 0) and ingest up to 32 bytes per pass (prevents lockup on break condition)
    uint8_t rxLimit = 32U;
    while ((lsr & 0x01) && rxLimit--) {
        uint8_t rxChar = thejas32_uart_read_rx((uint8_t)ch);
        // Reject corrupted framing/parity bytes (e.g. inverted wire or floating line break)
        if ((lsr & 0x0CU) == 0U) {
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
        }
        lsr = thejas32_uart_read_lsr((uint8_t)ch);
    }

    // Opportunistically drain TX FIFO during interrupt service if transmitter ready
    if ((THEJAS32_UART(ch).UART_LSR & 0x20) && (s->port.txBufferTail != s->port.txBufferHead)) {
        uartEnableTxInterrupt(s);
    }

    THEJAS32_MEMORY_FENCE();
}

static void thejas32_uart0_isr(void)
{
    uartIrqHandler(&uartDevice[UARTDEV_0].port);
}

#ifdef USE_UART1
void thejas32_uart1_isr(void)
{
    uartIrqHandler(&uartDevice[UARTDEV_1].port);
}
#endif

// -----------------------------------------------------------------------------
// Driver Initialization & Open
// -----------------------------------------------------------------------------
uartPort_t *serialUART(uartDevice_t *uartdev, uint32_t baudRate, portMode_e mode, portOptions_e options)
{
    if (!uartdev || !uartdev->hardware) {
        return NULL;
    }

    uartPort_t *s = &uartdev->port;
    const uartHardware_t *hardware = uartdev->hardware;
    int8_t ch = uartGetIndex(hardware->reg);
    if (ch < 0) {
        return NULL;
    }

    // Save port state (clamped to silicon tolerance limits)
    s->port.options = options;
    s->port.mode = mode;
    s->port.baudRate = thejas32_uart_clamp_baudrate(baudRate);

    // Keep UART RX IRQ masked until uartOpen() sets rxCallback/ring pointers and calls uartReconfigure()
    THEJAS32_UART(ch).UART_IE = 0x00;

    // Initialize baud rate and line framing (parity / stop bits)
    thejas32_uart_set_baudrate((uint8_t)ch, baudRate, options);

    // Register ISR handler with THEJAS32 interrupt controller (IRQ remains masked until uartReconfigure)
    if (ch == 0) {
        thejas32_interrupt_disable(THEJAS32_IRQ_UART0);
        thejas32_interrupt_register(THEJAS32_IRQ_UART0, thejas32_uart0_isr);
    }
#ifdef USE_UART1
    else if (ch == 1) {
        thejas32_interrupt_disable(THEJAS32_IRQ_UART1);
        thejas32_interrupt_register(THEJAS32_IRQ_UART1, thejas32_uart1_isr);
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

    THEJAS32_MEMORY_FENCE();
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

void uartPlatformPollRx(uartPort_t *s)
{
    if (s && s->USARTx && s->port.rxBuffer && s->port.rxBufferSize > 0) {
        if (s->port.rxBufferHead == s->port.rxBufferTail) {
            uint32_t prev = thejas32_atomic_enter();
            uartIrqHandler(s);
            thejas32_atomic_exit(prev);
        }
    }
}

#endif // USE_UART
