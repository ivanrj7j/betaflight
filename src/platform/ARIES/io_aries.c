/*
 * This file is part of Betaflight.
 *
 * GPIO Driver for C-DAC ARIES v3.0 (THEJAS32 / VEGA ET1031 RISC-V SoC).
 * Implements address-decoded bit-shift register access from C-DAC VEGA SDK BSP.
 */

#include "platform.h"
#include "platform/bsp_thejas32.h"

#include "drivers/io.h"
#include "drivers/io_impl.h"
#include "common/utils.h"

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

    uint32_t base = (pin < 16) ? THEJAS32_GPIO_0_BASE : THEJAS32_GPIO_1_BASE;
    uint8_t pin_in_bank = (pin < 16) ? pin : (pin - 16);
    volatile uint16_t *dir = (volatile uint16_t *)(base + 0x40000);

    uint8_t mode = cfg & 0x03;
    if (mode == THEJAS32_GPIO_MODE_OUTPUT || mode == THEJAS32_GPIO_MODE_OUTPUT_OD) {
        *dir |= (1U << pin_in_bank); // 1 = Output
    } else {
        *dir &= ~(1U << pin_in_bank); // 0 = Input
    }

    __asm__ __volatile__("fence");
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

    uint32_t base = (pin < 16) ? THEJAS32_GPIO_0_BASE : THEJAS32_GPIO_1_BASE;
    uint8_t pin_in_bank = (pin < 16) ? pin : (pin - 16);
    volatile uint16_t *reg = (volatile uint16_t *)(base + ((1U << pin_in_bank) << 2));

    *reg = (1U << pin_in_bank);
    __asm__ __volatile__("fence");
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

    uint32_t base = (pin < 16) ? THEJAS32_GPIO_0_BASE : THEJAS32_GPIO_1_BASE;
    uint8_t pin_in_bank = (pin < 16) ? pin : (pin - 16);
    volatile uint16_t *reg = (volatile uint16_t *)(base + ((1U << pin_in_bank) << 2));

    *reg = 0;
    __asm__ __volatile__("fence");
}

void IOToggle(IO_t io)
{
    if (!io) {
        return;
    }

    uint16_t pin = IO_Pin(io);
    if (pin >= 32) {
        return;
    }

    uint32_t base = (pin < 16) ? THEJAS32_GPIO_0_BASE : THEJAS32_GPIO_1_BASE;
    uint8_t pin_in_bank = (pin < 16) ? pin : (pin - 16);
    volatile uint16_t *reg = (volatile uint16_t *)(base + ((1U << pin_in_bank) << 2));

    if (*reg) {
        *reg = 0;
    } else {
        *reg = (1U << pin_in_bank);
    }
    __asm__ __volatile__("fence");
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

    uint32_t base = (pin < 16) ? THEJAS32_GPIO_0_BASE : THEJAS32_GPIO_1_BASE;
    uint8_t pin_in_bank = (pin < 16) ? pin : (pin - 16);
    volatile uint16_t *reg = (volatile uint16_t *)(base + ((1U << pin_in_bank) << 2));

    return (*reg != 0);
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
