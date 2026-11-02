/*
 * Testable interface for ARIES v3.0 Address-Decoded GPIO Driver.
 * Host unit test shim mirroring src/platform/ARIES/io_aries.c.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define THEJAS32_GPIO_MODE_INPUT     0
#define THEJAS32_GPIO_MODE_OUTPUT    1
#define THEJAS32_GPIO_MODE_OUTPUT_OD 2

typedef struct {
    uint16_t pin;
    uintptr_t gpio_base;
} test_ioRec_t;

typedef test_ioRec_t* test_IO_t;

// Testable GPIO Driver APIs
void test_IOInitGlobal(void);
test_IO_t test_IOGetByPin(uint16_t pin);
void test_IOConfigGPIO(test_IO_t io, uint8_t mode);
void test_IOHi(test_IO_t io);
void test_IOLo(test_IO_t io);
void test_IOToggle(test_IO_t io);
bool test_IORead(test_IO_t io);
void test_IOWrite(test_IO_t io, bool hi);
int test_IO_GPIOPortIdx(test_IO_t io);
int test_IO_GPIOPinIdx(test_IO_t io);

// Inspection helpers for MMIO memory
uint16_t test_gpio_read_data_reg(uint8_t bank, uint8_t pin_in_bank);
uint16_t test_gpio_read_dir_reg(uint8_t bank);
void test_gpio_write_data_reg(uint8_t bank, uint8_t pin_in_bank, uint16_t val);

#ifdef __cplusplus
}
#endif
