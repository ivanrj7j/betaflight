/*
 * This file is part of Betaflight.
 *
 * GPIO Driver for C-DAC ARIES v3.0 (THEJAS32 / VEGA ET1031 RISC-V SoC).
 * Implements address-decoded bit-shift register access from C-DAC VEGA SDK BSP.
 *
 * Citations:
 *   - PLAN/04_GPIO_EXTI_AND_HARDWARE_TIMERS.md (Microcomponents 04_A & 04_B)
 *   - knowledgebase/codebase_evaluations/07_EVAL_IO_ARIES_C_AND_DEF.md
 *   - knowledgebase/components/04_GPIO_AND_PIN_IO.md
 *   - lib/vega-sdk/vega-sdk/bsp/drivers/gpio/gpio.c
 *   - lib/vega-sdk/vega-sdk/bsp/include/gpio.h
 *   - lib/vega-sdk/vega-sdk/bsp/include/config.h
 *   - src/platform/ARIES/include/platform/bsp_thejas32.h
 */

#include "platform.h"
#include "platform/bsp_thejas32.h"

#include "drivers/io.h"
#include "drivers/io_impl.h"
#include "common/utils.h"

// Active open-drain mode tracking bitmask for all 32 pins
static uint32_t open_drain_mask = 0;

void IOInitGlobal(void)
{
    ioRec_t *ioRec = ioRecs;

    for (unsigned pin = 0; pin < DEFIO_IO_USED_COUNT; pin++) {
        ioRec->pin = pin;
        ioRec->gpio = (pin < 16) ? (gpioResource_t *)THEJAS32_GPIO_0_BASE : (gpioResource_t *)THEJAS32_GPIO_1_BASE;
        ioRec->owner = OWNER_FREE;
        ioRec->index = 0;
        ioRec++;
    }

    open_drain_mask = 0;
}

IO_t IOGetByTag(ioTag_t tag)
{
    if (DEFIO_TAG_ISEMPTY(tag)) {
        return IO_NONE;
    }

    uint8_t gpioid = DEFIO_TAG_GPIOID(tag);
    uint8_t pin = DEFIO_TAG_PIN(tag);

    if (gpioid == 0 && pin < DEFIO_IO_USED_COUNT) {
        return &ioRecs[pin];
    }

    return IO_NONE;
}

void IOConfigGPIO(IO_t io, ioConfig_t cfg)
{
    if (!io) {
        return;
    }

    uint16_t pin = IO_Pin(io);
    if (pin >= 32) {
        return;
    }

    uint8_t pin_in_bank = (uint8_t)(pin & 0x0FU);
    volatile uint16_t *dir = thejas32_gpio_dir_ptr(pin);

    uint8_t mode = cfg & 0x03;
    uint32_t prev = thejas32_atomic_enter();
    if (mode == THEJAS32_GPIO_MODE_OUTPUT_OD) {
        // shortcut: emulate open-drain via DIR RMW under MIE guard; switch if HW open-drain pad ctrl added
        open_drain_mask |= (1UL << pin);
        *dir &= ~(1U << pin_in_bank);
    } else if (mode == THEJAS32_GPIO_MODE_OUTPUT) {
        open_drain_mask &= ~(1UL << pin);
        *dir |= (1U << pin_in_bank); // 1 = Output
    } else {
        open_drain_mask &= ~(1UL << pin);
        *dir &= ~(1U << pin_in_bank); // 0 = Input
    }
    thejas32_atomic_exit(prev);

    THEJAS32_MEMORY_FENCE();
}

FAST_CODE void IOHi(IO_t io)
{
    if (!io) {
        return;
    }

    uint16_t pin = IO_Pin(io);
    if (pin >= 32) {
        return;
    }

    uint8_t pin_in_bank = (uint8_t)(pin & 0x0FU);

    if (open_drain_mask & (1UL << pin)) {
        // Open-Drain HIGH: Release the line (Direction = Input / High-Z)
        volatile uint16_t *dir = thejas32_gpio_dir_ptr(pin);
        uint32_t prev = thejas32_atomic_enter();
        *dir &= ~(1U << pin_in_bank);
        thejas32_atomic_exit(prev);
    } else {
        // Push-Pull HIGH: Single-instruction atomic address-decoded store
        volatile uint16_t *reg = thejas32_gpio_data_ptr(pin);
        *reg = (1U << pin_in_bank);
    }

    THEJAS32_MEMORY_FENCE();
}

FAST_CODE void IOLo(IO_t io)
{
    if (!io) {
        return;
    }

    uint16_t pin = IO_Pin(io);
    if (pin >= 32) {
        return;
    }

    uint8_t pin_in_bank = (uint8_t)(pin & 0x0FU);

    if (open_drain_mask & (1UL << pin)) {
        // Open-Drain LOW: Actively drive down (Direction = Output, Data = 0)
        volatile uint16_t *dir = thejas32_gpio_dir_ptr(pin);
        volatile uint16_t *reg = thejas32_gpio_data_ptr(pin);
        uint32_t prev = thejas32_atomic_enter();
        *dir |= (1U << pin_in_bank);
        *reg = 0;
        thejas32_atomic_exit(prev);
    } else {
        // Push-Pull LOW: Single-instruction atomic address-decoded store
        volatile uint16_t *reg = thejas32_gpio_data_ptr(pin);
        *reg = 0;
    }

    THEJAS32_MEMORY_FENCE();
}

void IOToggle(IO_t io)
{
    if (!io) {
        return;
    }

    if (IORead(io)) {
        IOLo(io);
    } else {
        IOHi(io);
    }
}

bool IORead(IO_t io)
{
    if (!io) {
        return false;
    }

    uint16_t pin = IO_Pin(io);
    if (pin >= 32) {
        return false;
    }

    volatile uint16_t *reg = thejas32_gpio_data_ptr(pin);

    uint16_t val = *reg;
    THEJAS32_MEMORY_FENCE();

    return (val != 0);
}

void IOWrite(IO_t io, bool hi)
{
    if (hi) {
        IOHi(io);
    } else {
        IOLo(io);
    }
}

int IO_GPIOPortIdx(IO_t io)
{
    if (!io) {
        return -1;
    }
    uint16_t pin = IO_Pin(io);
    return (pin < 16) ? 0 : 1;
}

int IO_GPIOPinIdx(IO_t io)
{
    if (!io) {
        return -1;
    }
    uint16_t pin = IO_Pin(io);
    return (pin < 16) ? pin : (pin - 16);
}

int IO_GPIO_PortSource(IO_t io)
{
    return IO_GPIOPortIdx(io);
}

int IO_GPIO_PinSource(IO_t io)
{
    return IO_GPIOPinIdx(io);
}

uint32_t IO_EXTI_Line(IO_t io)
{
    if (!io) {
        return 0;
    }
    uint16_t pin = IO_Pin(io);
    return (1UL << pin);
}
