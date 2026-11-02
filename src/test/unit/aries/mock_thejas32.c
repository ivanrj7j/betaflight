/*
 * Mock THEJAS32 Interrupt Controller & HAL implementation.
 */

#include "mock_thejas32.h"
#include <string.h>

thejas32_irq_handler_t mock_irq_table[MOCK_IRQ_TABLE_SIZE];
uint32_t mock_irq_enable_mask = 0;

void mock_thejas32_reset(void)
{
    memset(mock_irq_table, 0, sizeof(mock_irq_table));
    mock_irq_enable_mask = 0;
}

__attribute__((weak)) void thejas32_interrupt_register(uint8_t irq_number, thejas32_irq_handler_t handler)
{
    if (irq_number < MOCK_IRQ_TABLE_SIZE) {
        mock_irq_table[irq_number] = handler;
    }
}

__attribute__((weak)) void thejas32_interrupt_enable(uint8_t irq_number)
{
    if (irq_number < MOCK_IRQ_TABLE_SIZE) {
        mock_irq_enable_mask |= (1UL << irq_number);
    }
}

__attribute__((weak)) void thejas32_interrupt_disable(uint8_t irq_number)
{
    if (irq_number < MOCK_IRQ_TABLE_SIZE) {
        mock_irq_enable_mask &= ~(1UL << irq_number);
    }
}

void mock_fire_irq(uint8_t irq_number)
{
    if (irq_number < MOCK_IRQ_TABLE_SIZE) {
        if ((mock_irq_enable_mask & (1UL << irq_number)) && mock_irq_table[irq_number]) {
            mock_irq_table[irq_number]();
        }
    }
}

bool mock_irq_is_enabled(uint8_t irq_number)
{
    if (irq_number < MOCK_IRQ_TABLE_SIZE) {
        return (mock_irq_enable_mask & (1UL << irq_number)) != 0;
    }
    return false;
}
