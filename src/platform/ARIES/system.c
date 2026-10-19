/*
 * This file is part of Betaflight.
 * System timing and clock routines for ARIES v3.0 (THEJAS32 / VEGA ET1031).
 */

#include "platform.h"
#include "drivers/time.h"
#include "drivers/system.h"

uint32_t SystemCoreClock = 100000000;

FAST_CODE uint32_t getCycleCounter(void)
{
    uint32_t val;
    __asm__ __volatile__("csrr %0, mcycle" : "=r"(val));
    return val;
}

FAST_CODE timeUs_t micros(void)
{
    return (timeUs_t)(getCycleCounter() / (SystemCoreClock / 1000000));
}

timeMs_t millis(void)
{
    return micros() / 1000;
}

FAST_CODE timeUs_t microsISR(void)
{
    return micros();
}

void delayMicroseconds(uint32_t us)
{
    timeUs_t start = micros();
    while ((micros() - start) < us);
}

void delay(uint32_t ms)
{
    delayMicroseconds(ms * 1000);
}

void systemInit(void)
{
}

void systemReset(void)
{
    while (1);
}

void systemResetToBootloader(void)
{
    while (1);
}
