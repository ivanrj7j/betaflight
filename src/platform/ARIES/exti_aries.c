/*
 * This file is part of Betaflight.
 *
 * External Interrupt (EXTI) Driver for C-DAC ARIES v3.0 (THEJAS32 / VEGA ET1031).
 * Routes GPIO interrupts through the THEJAS32 Interrupt Controller (IRQ 10 & IRQ 11).
 */

#include "platform.h"
#include <string.h>

#include "drivers/exti.h"
#include "drivers/io.h"
#include "drivers/io_impl.h"
#include "platform/bsp_thejas32.h"
#include "common/utils.h"

typedef struct {
    extiCallbackRec_t *handler;
    extiTrigger_t trigger;
    bool enabled;
} extiChannel_t;

static extiChannel_t extiChannels[32];

FAST_IRQ_HANDLER void thejas32_gpio0_isr(void)
{
    // Bank 0 covers pins 0 to 15
    for (int pin = 0; pin < 16; pin++) {
        if (extiChannels[pin].enabled && extiChannels[pin].handler && extiChannels[pin].handler->fn) {
            extiChannels[pin].handler->fn(extiChannels[pin].handler);
        }
    }
}

FAST_IRQ_HANDLER void thejas32_gpio1_isr(void)
{
    // Bank 1 covers pins 16 to 31
    for (int pin = 16; pin < 32; pin++) {
        if (extiChannels[pin].enabled && extiChannels[pin].handler && extiChannels[pin].handler->fn) {
            extiChannels[pin].handler->fn(extiChannels[pin].handler);
        }
    }
}

void EXTIInit(void)
{
    memset(extiChannels, 0, sizeof(extiChannels));

    // Register Bank 0 (IRQ 10) and Bank 1 (IRQ 11) ISRs in the interrupt controller table
    thejas32_interrupt_register(THEJAS32_IRQ_GPIO0, thejas32_gpio0_isr);
    thejas32_interrupt_register(THEJAS32_IRQ_GPIO1, thejas32_gpio1_isr);
}

void EXTIHandlerInit(extiCallbackRec_t *cb, extiHandlerCallback *fn)
{
    if (cb) {
        cb->fn = fn;
    }
}

void EXTIConfig(IO_t io, extiCallbackRec_t *cb, int irqPriority, ioConfig_t config, extiTrigger_t trigger)
{
    if (!io) {
        return;
    }

    UNUSED(irqPriority);
    uint16_t pin = IO_Pin(io);
    if (pin >= 32) {
        return;
    }

    // Configure GPIO as Input
    IOConfigGPIO(io, config);

    extiChannels[pin].handler = cb;
    extiChannels[pin].trigger = trigger;
    extiChannels[pin].enabled = true;

    // Enable corresponding GPIO bank in interrupt controller
    if (pin < 16) {
        thejas32_interrupt_enable(THEJAS32_IRQ_GPIO0);
    } else {
        thejas32_interrupt_enable(THEJAS32_IRQ_GPIO1);
    }
}

void EXTIRelease(IO_t io)
{
    if (!io) {
        return;
    }

    uint16_t pin = IO_Pin(io);
    if (pin >= 32) {
        return;
    }

    EXTIDisable(io);
    extiChannels[pin].handler = NULL;
}

void EXTIEnable(IO_t io)
{
    if (!io) {
        return;
    }

    uint16_t pin = IO_Pin(io);
    if (pin >= 32) {
        return;
    }

    extiChannels[pin].enabled = true;
    if (pin < 16) {
        thejas32_interrupt_enable(THEJAS32_IRQ_GPIO0);
    } else {
        thejas32_interrupt_enable(THEJAS32_IRQ_GPIO1);
    }
}

void EXTIDisable(IO_t io)
{
    if (!io) {
        return;
    }

    uint16_t pin = IO_Pin(io);
    if (pin >= 32) {
        return;
    }

    extiChannels[pin].enabled = false;
}

uint32_t IO_EXTI_Line(IO_t io)
{
    if (!io) {
        return 0;
    }
    return 1U << IO_Pin(io);
}
