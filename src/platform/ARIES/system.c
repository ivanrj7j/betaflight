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

static uint32_t usTicks = 100;
static float usTicksInv = 0.01f;

void cycleCounterInit(void)
{
    usTicks = SystemCoreClock / 1000000;
    usTicksInv = 1e6f / SystemCoreClock;
}

FAST_CODE int32_t clockCyclesToMicros(int32_t clockCycles)
{
    return clockCycles / usTicks;
}

FAST_CODE float clockCyclesToMicrosf(int32_t clockCycles)
{
    return clockCycles * usTicksInv;
}

FAST_CODE int32_t clockCyclesTo10thMicros(int32_t clockCycles)
{
    return 10 * clockCycles / (int32_t)usTicks;
}

FAST_CODE int32_t clockCyclesTo100thMicros(int32_t clockCycles)
{
    return 100 * clockCycles / (int32_t)usTicks;
}

FAST_CODE uint32_t clockMicrosToCycles(uint32_t micros)
{
    return micros * usTicks;
}

bool isMPUSoftReset(void)
{
    return false;
}

void systemInit(void)
{
    cycleCounterInit();
}

void systemReset(void)
{
    while (1);
}

void systemResetToBootloader(bootloaderRequestType_e requestType)
{
    (void)requestType;
    while (1);
}

void indicateFailure(failureMode_e mode, int repeatCount)
{
    (void)mode;
    (void)repeatCount;
}

void failureMode(failureMode_e mode)
{
    (void)mode;
    while (1);
}

