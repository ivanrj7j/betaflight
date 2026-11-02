/*
 * Host-testable implementation of ARIES v3.0 External Interrupt (EXTI) Driver logic.
 * Mirrors src/platform/ARIES/exti_aries.c using modular mock infrastructure.
 */

#include "aries_exti_testable.h"
#include "aries/mock_thejas32.h"
#include <string.h>

typedef struct {
    test_extiCallbackRec_t *handler;
    test_extiTrigger_t trigger;
    bool enabled;
} extiChannel_t;

static extiChannel_t extiChannels[32];

void test_thejas32_gpio0_isr(void)
{
    for (int pin = 0; pin < 16; pin++) {
        if (extiChannels[pin].enabled && extiChannels[pin].handler && extiChannels[pin].handler->fn) {
            extiChannels[pin].handler->fn(extiChannels[pin].handler);
        }
    }
}

void test_thejas32_gpio1_isr(void)
{
    for (int pin = 16; pin < 32; pin++) {
        if (extiChannels[pin].enabled && extiChannels[pin].handler && extiChannels[pin].handler->fn) {
            extiChannels[pin].handler->fn(extiChannels[pin].handler);
        }
    }
}

void test_EXTIInit(void)
{
    memset(extiChannels, 0, sizeof(extiChannels));

    thejas32_interrupt_register(THEJAS32_IRQ_GPIO0, test_thejas32_gpio0_isr);
    thejas32_interrupt_register(THEJAS32_IRQ_GPIO1, test_thejas32_gpio1_isr);
}

void test_EXTIHandlerInit(test_extiCallbackRec_t *cb, void (*fn)(test_extiCallbackRec_t *))
{
    if (cb) {
        cb->fn = fn;
    }
}

void test_EXTIConfig(test_IO_t io, test_extiCallbackRec_t *cb, int irqPriority, uint8_t config, test_extiTrigger_t trigger)
{
    if (!io || io->pin >= 32) {
        return;
    }

    (void)irqPriority;
    uint16_t pin = io->pin;

    test_IOConfigGPIO(io, config);

    extiChannels[pin].handler = cb;
    extiChannels[pin].trigger = trigger;
    extiChannels[pin].enabled = true;

    if (pin < 16) {
        thejas32_interrupt_enable(THEJAS32_IRQ_GPIO0);
    } else {
        thejas32_interrupt_enable(THEJAS32_IRQ_GPIO1);
    }
}

void test_EXTIRelease(test_IO_t io)
{
    if (!io || io->pin >= 32) {
        return;
    }

    uint16_t pin = io->pin;
    test_EXTIDisable(io);
    extiChannels[pin].handler = NULL;
}

void test_EXTIEnable(test_IO_t io)
{
    if (!io || io->pin >= 32) {
        return;
    }

    uint16_t pin = io->pin;
    extiChannels[pin].enabled = true;
    if (pin < 16) {
        thejas32_interrupt_enable(THEJAS32_IRQ_GPIO0);
    } else {
        thejas32_interrupt_enable(THEJAS32_IRQ_GPIO1);
    }
}

void test_EXTIDisable(test_IO_t io)
{
    if (!io || io->pin >= 32) {
        return;
    }

    uint16_t pin = io->pin;
    extiChannels[pin].enabled = false;
}

uint32_t test_IO_EXTI_Line(test_IO_t io)
{
    if (!io) {
        return 0;
    }
    return 1U << io->pin;
}

bool test_exti_is_channel_enabled(uint16_t pin)
{
    if (pin < 32) {
        return extiChannels[pin].enabled;
    }
    return false;
}

test_extiCallbackRec_t *test_exti_get_channel_handler(uint16_t pin)
{
    if (pin < 32) {
        return extiChannels[pin].handler;
    }
    return NULL;
}
