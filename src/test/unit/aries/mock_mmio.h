/*
 * Mock MMIO memory shadow and tracking engine for THEJAS32 unit testing.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MOCK_PERIPH_SIZE    0x200000   // 2 MB covers 0x10000000–0x101FFFFF
#define MOCK_GPIO0_SIZE     0x100000   // 1 MB covers 0x10080000–0x1017FFFF
#define MOCK_GPIO1_SIZE     0x100000   // 1 MB covers 0x10180000–0x1027FFFF
#define MOCK_FLASH_SIZE     0x200000   // 2 MB covers 0x20000000–0x201FFFFF
#define MOCK_INTCTRL_SIZE   0x100      // Covers 0x20010000–0x200100FF

extern uint8_t mock_periph_mem[MOCK_PERIPH_SIZE];
extern uint8_t mock_gpio0_mem[MOCK_GPIO0_SIZE];
extern uint8_t mock_gpio1_mem[MOCK_GPIO1_SIZE];
extern uint8_t mock_flash_mem[MOCK_FLASH_SIZE];
extern uint8_t mock_intctrl_mem[MOCK_INTCTRL_SIZE];

void mock_mmio_reset(void);
void mock_mmio_reset_pattern(uint8_t *region, size_t size, uint8_t pattern);

typedef struct {
    uint32_t address;
    uint32_t value;
    bool     is_write;
} mmio_access_t;

#define MOCK_MMIO_LOG_SIZE 1024
extern mmio_access_t mock_mmio_log[MOCK_MMIO_LOG_SIZE];
extern uint32_t mock_mmio_log_count;

void mock_mmio_log_reset(void);
void mock_mmio_log_write(uint32_t address, uint32_t value);
void mock_mmio_log_read(uint32_t address, uint32_t value);
mmio_access_t *mock_mmio_find_write(uint32_t address);
uint32_t mock_mmio_count_writes_to(uint32_t address);

#ifdef __cplusplus
}
#endif
