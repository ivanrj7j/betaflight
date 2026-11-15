/*
 * Testable interface for ARIES v3.0 System Core & Timebase logic.
 * Host unit test shim mirroring src/platform/ARIES/system.c.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "common/time.h"

#ifdef __cplusplus
extern "C" {
#endif

extern uint32_t SystemCoreClock;

// Cycle counter and time conversions
void cycleCounterInit(void);
timeUs_t micros(void);
timeMs_t millis(void);
timeUs_t microsISR(void);
int32_t clockCyclesToMicros(int32_t clockCycles);
float clockCyclesToMicrosf(int32_t clockCycles);
int32_t clockCyclesTo10thMicros(int32_t clockCycles);
int32_t clockCyclesTo100thMicros(int32_t clockCycles);
uint32_t clockMicrosToCycles(uint32_t us);

// Hardware Timer 0
void thejas32_timer_init(void);
void thejas32_timer0_isr(void);
uint32_t timer0GetCurrentValue(void);
uint32_t timer0GetOverflows(void);
timeUs_t timer0Micros(void);

// Interrupt Controller & Trap Handling
typedef void (*thejas32_irq_handler_t)(void);
extern thejas32_irq_handler_t interrupt_table[32];
void thejas32_interrupt_register(uint8_t irq_number, thejas32_irq_handler_t handler);
void thejas32_interrupt_enable(uint8_t irq_number);
void thejas32_interrupt_disable(uint8_t irq_number);
void test_enable_irq(void);
void test_disable_irq(void);
uint32_t handle_trap(uint32_t mcause, uint32_t mepc, uint32_t *sp);

// Bare-metal memory routines for C-DAC THEJAS32 (VEGA ET1031)
void *thejas32_memset(void *dest, int c, size_t n);
void *thejas32_memcpy(void *dest, const void *src, size_t n);
void *thejas32_memmove(void *dest, const void *src, size_t n);

// Early UART routines
void test_early_uart_putc_timeout(char c, uint32_t mock_lsr_val, uint32_t *cycles_spun);
void test_early_uart_print_hex(uint32_t val, char *out_str);

// Test diagnostic hooks
int test_system_get_panic_count(void);
uint32_t test_system_get_last_panic_mcause(void);
void test_system_reset_panic(void);
void test_system_reset_state(void);

#ifdef __cplusplus
}
#endif
