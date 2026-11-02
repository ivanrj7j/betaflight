/*
 * Mock MMIO memory shadow and tracking engine implementation.
 */

#include "mock_mmio.h"
#include <string.h>

uint8_t mock_periph_mem[MOCK_PERIPH_SIZE];
uint8_t mock_gpio0_mem[MOCK_GPIO0_SIZE];
uint8_t mock_gpio1_mem[MOCK_GPIO1_SIZE];
uint8_t mock_flash_mem[MOCK_FLASH_SIZE];
uint8_t mock_intctrl_mem[MOCK_INTCTRL_SIZE];

mmio_access_t mock_mmio_log[MOCK_MMIO_LOG_SIZE];
uint32_t mock_mmio_log_count = 0;

void mock_mmio_reset(void)
{
    memset(mock_periph_mem, 0, sizeof(mock_periph_mem));
    memset(mock_gpio0_mem, 0, sizeof(mock_gpio0_mem));
    memset(mock_gpio1_mem, 0, sizeof(mock_gpio1_mem));
    memset(mock_flash_mem, 0xFF, sizeof(mock_flash_mem)); // Flash erased is 0xFF
    memset(mock_intctrl_mem, 0, sizeof(mock_intctrl_mem));
    mock_mmio_log_count = 0;
}

void mock_mmio_reset_pattern(uint8_t *region, size_t size, uint8_t pattern)
{
    if (region && size > 0) {
        memset(region, pattern, size);
    }
}

void mock_mmio_log_reset(void)
{
    mock_mmio_log_count = 0;
}

void mock_mmio_log_write(uint32_t address, uint32_t value)
{
    if (mock_mmio_log_count < MOCK_MMIO_LOG_SIZE) {
        mock_mmio_log[mock_mmio_log_count].address = address;
        mock_mmio_log[mock_mmio_log_count].value = value;
        mock_mmio_log[mock_mmio_log_count].is_write = true;
        mock_mmio_log_count++;
    }
}

void mock_mmio_log_read(uint32_t address, uint32_t value)
{
    if (mock_mmio_log_count < MOCK_MMIO_LOG_SIZE) {
        mock_mmio_log[mock_mmio_log_count].address = address;
        mock_mmio_log[mock_mmio_log_count].value = value;
        mock_mmio_log[mock_mmio_log_count].is_write = false;
        mock_mmio_log_count++;
    }
}

mmio_access_t *mock_mmio_find_write(uint32_t address)
{
    for (uint32_t i = 0; i < mock_mmio_log_count; i++) {
        if (mock_mmio_log[i].address == address && mock_mmio_log[i].is_write) {
            return &mock_mmio_log[i];
        }
    }
    return NULL;
}

uint32_t mock_mmio_count_writes_to(uint32_t address)
{
    uint32_t count = 0;
    for (uint32_t i = 0; i < mock_mmio_log_count; i++) {
        if (mock_mmio_log[i].address == address && mock_mmio_log[i].is_write) {
            count++;
        }
    }
    return count;
}
