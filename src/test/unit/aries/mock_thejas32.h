/*
 * Mock THEJAS32 Interrupt Controller & HAL primitives for host testing.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*thejas32_irq_handler_t)(void);

#define MOCK_IRQ_TABLE_SIZE 32
extern thejas32_irq_handler_t mock_irq_table[MOCK_IRQ_TABLE_SIZE];
extern uint32_t mock_irq_enable_mask;

void thejas32_interrupt_register(uint8_t irq_number, thejas32_irq_handler_t handler);
void thejas32_interrupt_enable(uint8_t irq_number);
void thejas32_interrupt_disable(uint8_t irq_number);

void mock_fire_irq(uint8_t irq_number);
bool mock_irq_is_enabled(uint8_t irq_number);
void mock_thejas32_reset(void);

#ifndef __NOP
#define __NOP() ((void)0)
#endif

#ifdef __cplusplus
}
#endif
