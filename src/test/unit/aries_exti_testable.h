/*
 * Testable interface for ARIES v3.0 External Interrupt (EXTI) Driver.
 * Host unit test shim mirroring src/platform/ARIES/exti_aries.c.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "aries_gpio_testable.h"

#ifdef __cplusplus
extern "C" {
#endif

#define THEJAS32_IRQ_GPIO0 10
#define THEJAS32_IRQ_GPIO1 11

typedef struct test_extiCallbackRec_s {
    void (*fn)(struct test_extiCallbackRec_s *self);
    void *userData;
} test_extiCallbackRec_t;

typedef enum {
    TEST_EXTI_TRIGGER_RISING  = 1,
    TEST_EXTI_TRIGGER_FALLING = 2,
    TEST_EXTI_TRIGGER_BOTH    = 3
} test_extiTrigger_t;

void test_EXTIInit(void);
void test_EXTIHandlerInit(test_extiCallbackRec_t *cb, void (*fn)(test_extiCallbackRec_t *));
void test_EXTIConfig(test_IO_t io, test_extiCallbackRec_t *cb, int irqPriority, uint8_t config, test_extiTrigger_t trigger);
void test_EXTIRelease(test_IO_t io);
void test_EXTIEnable(test_IO_t io);
void test_EXTIDisable(test_IO_t io);
uint32_t test_IO_EXTI_Line(test_IO_t io);

void test_thejas32_gpio0_isr(void);
void test_thejas32_gpio1_isr(void);

// Inspection helpers
bool test_exti_is_channel_enabled(uint16_t pin);
test_extiCallbackRec_t *test_exti_get_channel_handler(uint16_t pin);

#ifdef __cplusplus
}
#endif
