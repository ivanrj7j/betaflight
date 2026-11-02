/*
 * Common test helper functions and mathematical validation utilities.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void fill_sequential(uint8_t *buf, size_t len);
bool verify_sequential(const uint8_t *buf, size_t len, uint8_t start);

void fill_canary(uint32_t *buf, size_t words);
size_t count_intact_canaries(const uint32_t *buf, size_t words);

static inline uintptr_t gpio_pin_address(uintptr_t base, uint8_t pin_in_bank)
{
    return base + ((1U << pin_in_bank) << 2);
}

static inline uintptr_t gpio_dir_address(uintptr_t base)
{
    return base + 0x40000;
}

static inline uint32_t uart_baud_divisor(uint32_t clock_hz, uint32_t baud)
{
    if (baud == 0) {
        baud = 115200;
    }
    uint32_t div = clock_hz / (baud * 16);
    return (div == 0) ? 1 : div;
}

#ifdef __cplusplus
}
#endif
