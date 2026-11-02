/*
 * Host-testable implementation of ARIES v3.0 Address-Decoded GPIO Driver logic.
 * Mirrors src/platform/ARIES/io_aries.c using modular mock infrastructure.
 */

#include "aries_gpio_testable.h"
#include "aries/mock_mmio.h"
#include <string.h>

static test_ioRec_t test_ioRecs[32];

void test_IOInitGlobal(void)
{
    for (unsigned pin = 0; pin < 32; pin++) {
        test_ioRecs[pin].pin = pin;
        test_ioRecs[pin].gpio_base = (pin < 16) ? (uintptr_t)mock_gpio0_mem : (uintptr_t)mock_gpio1_mem;
    }
}

test_IO_t test_IOGetByPin(uint16_t pin)
{
    if (pin < 32) {
        return &test_ioRecs[pin];
    }
    return NULL;
}

void test_IOConfigGPIO(test_IO_t io, uint8_t mode)
{
    if (!io || io->pin >= 32) {
        return;
    }

    uint16_t pin = io->pin;
    uintptr_t base = (pin < 16) ? (uintptr_t)mock_gpio0_mem : (uintptr_t)mock_gpio1_mem;
    uint8_t pin_in_bank = (pin < 16) ? pin : (pin - 16);
    volatile uint16_t *dir = (volatile uint16_t *)(base + 0x40000);

    if (mode == THEJAS32_GPIO_MODE_OUTPUT || mode == THEJAS32_GPIO_MODE_OUTPUT_OD) {
        *dir |= (1U << pin_in_bank);
    } else {
        *dir &= ~(1U << pin_in_bank);
    }
}

void test_IOHi(test_IO_t io)
{
    if (!io || io->pin >= 32) {
        return;
    }

    uint16_t pin = io->pin;
    uintptr_t base = (pin < 16) ? (uintptr_t)mock_gpio0_mem : (uintptr_t)mock_gpio1_mem;
    uint8_t pin_in_bank = (pin < 16) ? pin : (pin - 16);
    volatile uint16_t *reg = (volatile uint16_t *)(base + ((1U << pin_in_bank) << 2));

    *reg = (1U << pin_in_bank);
}

void test_IOLo(test_IO_t io)
{
    if (!io || io->pin >= 32) {
        return;
    }

    uint16_t pin = io->pin;
    uintptr_t base = (pin < 16) ? (uintptr_t)mock_gpio0_mem : (uintptr_t)mock_gpio1_mem;
    uint8_t pin_in_bank = (pin < 16) ? pin : (pin - 16);
    volatile uint16_t *reg = (volatile uint16_t *)(base + ((1U << pin_in_bank) << 2));

    *reg = 0;
}

void test_IOToggle(test_IO_t io)
{
    if (!io || io->pin >= 32) {
        return;
    }

    uint16_t pin = io->pin;
    uintptr_t base = (pin < 16) ? (uintptr_t)mock_gpio0_mem : (uintptr_t)mock_gpio1_mem;
    uint8_t pin_in_bank = (pin < 16) ? pin : (pin - 16);
    volatile uint16_t *reg = (volatile uint16_t *)(base + ((1U << pin_in_bank) << 2));

    if (*reg) {
        *reg = 0;
    } else {
        *reg = (1U << pin_in_bank);
    }
}

bool test_IORead(test_IO_t io)
{
    if (!io || io->pin >= 32) {
        return false;
    }

    uint16_t pin = io->pin;
    uintptr_t base = (pin < 16) ? (uintptr_t)mock_gpio0_mem : (uintptr_t)mock_gpio1_mem;
    uint8_t pin_in_bank = (pin < 16) ? pin : (pin - 16);
    volatile uint16_t *reg = (volatile uint16_t *)(base + ((1U << pin_in_bank) << 2));

    return (*reg != 0);
}

void test_IOWrite(test_IO_t io, bool hi)
{
    if (hi) {
        test_IOHi(io);
    } else {
        test_IOLo(io);
    }
}

int test_IO_GPIOPortIdx(test_IO_t io)
{
    if (!io) {
        return -1;
    }
    return (io->pin < 16) ? 0 : 1;
}

int test_IO_GPIOPinIdx(test_IO_t io)
{
    if (!io) {
        return -1;
    }
    return (io->pin < 16) ? io->pin : (io->pin - 16);
}

uint16_t test_gpio_read_data_reg(uint8_t bank, uint8_t pin_in_bank)
{
    uintptr_t base = (bank == 0) ? (uintptr_t)mock_gpio0_mem : (uintptr_t)mock_gpio1_mem;
    volatile uint16_t *reg = (volatile uint16_t *)(base + ((1U << pin_in_bank) << 2));
    return *reg;
}

uint16_t test_gpio_read_dir_reg(uint8_t bank)
{
    uintptr_t base = (bank == 0) ? (uintptr_t)mock_gpio0_mem : (uintptr_t)mock_gpio1_mem;
    volatile uint16_t *dir = (volatile uint16_t *)(base + 0x40000);
    return *dir;
}

void test_gpio_write_data_reg(uint8_t bank, uint8_t pin_in_bank, uint16_t val)
{
    uintptr_t base = (bank == 0) ? (uintptr_t)mock_gpio0_mem : (uintptr_t)mock_gpio1_mem;
    volatile uint16_t *reg = (volatile uint16_t *)(base + ((1U << pin_in_bank) << 2));
    *reg = val;
}
