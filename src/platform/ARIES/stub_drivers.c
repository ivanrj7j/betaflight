/*
 * This file is part of Betaflight.
 *
 * Driver shims, libatomic RV32IM emulators, and subsystem isolation stubs
 * for C-DAC ARIES v3.0 (THEJAS32 / VEGA ET1031 RISC-V RV32IM @ 100 MHz).
 *
 * Citations & Evaluations:
 *   - knowledgebase/codebase_evaluations/12_EVAL_STUB_DRIVERS_C.md
 *   - PLAN/01_BOOT_LINKER_AND_CORE_PRIMITIVES.md (Microcomponent 01_C)
 *   - PLAN/06_SENSORS_RC_TELEMETRY_AND_SCHEDULER.md (Microcomponent 06_D)
 */

#include "platform.h"
#include "common/utils.h"

#include "drivers/io.h"
#include "drivers/dma.h"
#include "drivers/bus_spi.h"
#include "drivers/timer.h"
#include "drivers/exti.h"
#include "drivers/pwm_output.h"
#include "drivers/servo_impl.h"
#include "drivers/adc.h"
#include "drivers/serial.h"
#include "drivers/persistent.h"
#include "sensors/esc_sensor.h"

/*
 * =============================================================================
 * RV32IM State-Preserving Software Atomics Emulation
 * Silicon Ground Truth 5: Atomics via CSR mstatus.MIE
 * Solves 12_EVAL_STUB_DRIVERS_C.md (Score 4.0/10 for unconditional __enable_irq).
 * =============================================================================
 */

#ifdef UNIT_TEST
#define ARIES_ATOMIC_FN(name) aries_test_##name
#else
#define ARIES_ATOMIC_FN(name) name
#endif

unsigned int ARIES_ATOMIC_FN(__atomic_fetch_add_4)(volatile void *ptr, unsigned int val, int memorder)
{
    UNUSED(memorder);
    uint32_t prev = thejas32_atomic_enter();
    unsigned int old = *(volatile unsigned int *)ptr;
    *(volatile unsigned int *)ptr = old + val;
    thejas32_atomic_exit(prev);
    return old;
}

unsigned int ARIES_ATOMIC_FN(__atomic_fetch_sub_4)(volatile void *ptr, unsigned int val, int memorder)
{
    UNUSED(memorder);
    uint32_t prev = thejas32_atomic_enter();
    unsigned int old = *(volatile unsigned int *)ptr;
    *(volatile unsigned int *)ptr = old - val;
    thejas32_atomic_exit(prev);
    return old;
}

unsigned int ARIES_ATOMIC_FN(__atomic_fetch_and_4)(volatile void *ptr, unsigned int val, int memorder)
{
    UNUSED(memorder);
    uint32_t prev = thejas32_atomic_enter();
    unsigned int old = *(volatile unsigned int *)ptr;
    *(volatile unsigned int *)ptr = old & val;
    thejas32_atomic_exit(prev);
    return old;
}

unsigned int ARIES_ATOMIC_FN(__atomic_fetch_or_4)(volatile void *ptr, unsigned int val, int memorder)
{
    UNUSED(memorder);
    uint32_t prev = thejas32_atomic_enter();
    unsigned int old = *(volatile unsigned int *)ptr;
    *(volatile unsigned int *)ptr = old | val;
    thejas32_atomic_exit(prev);
    return old;
}

unsigned int ARIES_ATOMIC_FN(__atomic_exchange_4)(volatile void *ptr, unsigned int val, int memorder)
{
    UNUSED(memorder);
    uint32_t prev = thejas32_atomic_enter();
    unsigned int old = *(volatile unsigned int *)ptr;
    *(volatile unsigned int *)ptr = val;
    thejas32_atomic_exit(prev);
    return old;
}

bool ARIES_ATOMIC_FN(__atomic_compare_exchange_4)(volatile void *ptr, void *expected, unsigned int desired,
                                 bool weak, int success_memorder, int failure_memorder)
{
    UNUSED(weak);
    UNUSED(success_memorder);
    UNUSED(failure_memorder);
    uint32_t prev = thejas32_atomic_enter();
    unsigned int current = *(volatile unsigned int *)ptr;
    unsigned int exp_val = *(unsigned int *)expected;

    if (current == exp_val) {
        *(volatile unsigned int *)ptr = desired;
        thejas32_atomic_exit(prev);
        return true;
    } else {
        *(unsigned int *)expected = current;
        thejas32_atomic_exit(prev);
        return false;
    }
}

unsigned int ARIES_ATOMIC_FN(__atomic_load_4)(const volatile void *ptr, int memorder)
{
    UNUSED(memorder);
    uint32_t prev = thejas32_atomic_enter();
    unsigned int val = *(const volatile unsigned int *)ptr;
    thejas32_atomic_exit(prev);
    return val;
}

void ARIES_ATOMIC_FN(__atomic_store_4)(volatile void *ptr, unsigned int val, int memorder)
{
    UNUSED(memorder);
    uint32_t prev = thejas32_atomic_enter();
    *(volatile unsigned int *)ptr = val;
    thejas32_atomic_exit(prev);
}

// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
// Persistent Object Storage (Warm-reboot survival in internal SRAM)
// ----------------------------------------------------------------------------
#define PERSISTENT_OBJECT_MAGIC_VALUE (('B' << 24) | ('e' << 16) | ('f' << 8) | ('1' << 0))
// shortcut: .noinit SRAM survives warm soft resets only; lost on cold power-off until MDP_SPI_3 flash driver lands
static uint32_t persistentObjects[16] __attribute__((section(".noinit")));

void persistentObjectInit(void)
{
    if (persistentObjects[PERSISTENT_OBJECT_MAGIC] != PERSISTENT_OBJECT_MAGIC_VALUE) {
        for (size_t i = 0; i < ARRAYLEN(persistentObjects); i++) {
            persistentObjects[i] = 0U;
        }
        persistentObjects[PERSISTENT_OBJECT_MAGIC] = PERSISTENT_OBJECT_MAGIC_VALUE;
    }
}

uint32_t persistentObjectRead(persistentObjectId_e id)
{
    return ((size_t)id < ARRAYLEN(persistentObjects)) ? persistentObjects[id] : 0;
}

void persistentObjectWrite(persistentObjectId_e id, uint32_t value)
{
    if ((size_t)id < ARRAYLEN(persistentObjects)) {
        persistentObjects[id] = value;
    }
}

// ----------------------------------------------------------------------------
// IO / GPIO Helper Stubs
// ----------------------------------------------------------------------------
void unusedPinsInit(void)
{
}

void debugInit(void)
{
}

// ----------------------------------------------------------------------------
// Debug Pin Stubs (debug_pin.h compliance for oscilloscope / profiling hooks)
// ----------------------------------------------------------------------------
void dbgPinHi(int index)
{
    UNUSED(index);
}

void dbgPinLo(int index)
{
    UNUSED(index);
}

// ----------------------------------------------------------------------------
// DMA Driver Stubs (Allows linking without hardware DMA channels)
// ----------------------------------------------------------------------------
dmaChannelDescriptor_t dmaDescriptors[1];

int dmaGetHandlerCount(void)
{
    return 0;
}

int dmaGetDeviceNumber(dmaIdentifier_e identifier)
{
    UNUSED(identifier);
    return 0;
}

int dmaGetDeviceIndex(dmaIdentifier_e identifier)
{
    UNUSED(identifier);
    return 0;
}

const char *dmaGetDisplayString(void)
{
    return "NONE";
}

uint32_t dmaGetDataLength(dmaResource_t *ref)
{
    UNUSED(ref);
    return 0;
}

// ----------------------------------------------------------------------------
// Hardware Timer Driver Stubs
// ----------------------------------------------------------------------------
void timerInit(void)
{
}

void timerChannelOverflowHandlerInit(timerOvrHandlerRec_t *self, timerOvrHandlerCallback *fn)
{
    UNUSED(self);
    UNUSED(fn);
}

void timerConfigUpdateCallback(const timerHardware_t *timHw, timerOvrHandlerRec_t *updateCallback)
{
    UNUSED(timHw);
    UNUSED(updateCallback);
}

void timerReconfigureTimeBase(const timerHardware_t *timHw, uint16_t period, uint32_t hz)
{
    UNUSED(timHw);
    UNUSED(period);
    UNUSED(hz);
}

void timerSetPeriod(const timerHardware_t *timHw, uint32_t period)
{
    UNUSED(timHw);
    UNUSED(period);
}

void timerSetCounter(const timerHardware_t *timHw, uint32_t counter)
{
    UNUSED(timHw);
    UNUSED(counter);
}

void timerDisable(const timerHardware_t *timHw)
{
    UNUSED(timHw);
}

void timerEnable(const timerHardware_t *timHw)
{
    UNUSED(timHw);
}

void timerEnableInterrupt(const timerHardware_t *timHw)
{
    UNUSED(timHw);
}

// ----------------------------------------------------------------------------
// Motor & ESC Stubs (Active implementation in pwm_motor_aries.c in Phase 16)
// ----------------------------------------------------------------------------
bool useDshotTelemetry = false;

void servoDevInit(const servoDevConfig_t *servoDevConfig)
{
    UNUSED(servoDevConfig);
}

void servoWrite(uint8_t index, float value)
{
    UNUSED(index);
    UNUSED(value);
}

float erpmToRpm(uint32_t erpm)
{
    UNUSED(erpm);
    return 0.0f;
}

float getDshotRpmAverage(void)
{
    return 0.0f;
}

escSensorData_t *getEscSensorData(uint8_t motorNumber)
{
    UNUSED(motorNumber);
    return NULL;
}

// ----------------------------------------------------------------------------
// ADC Voltage Stub (Safe only while USE_ADC is #undef'd -> VOLTAGE_METER_NONE)
// ----------------------------------------------------------------------------
// shortcut: adcGetValue returns 0 while USE_ADC is #undef'd in target.h; implement U9 I2C2 / 0x10001000 before enabling USE_ADC
uint16_t adcGetValue(adcSource_e source)
{
    UNUSED(source);
    return 0;
}

void expressLrsTimerEnableIRQs(const struct timerHardware_s *timer)
{
    UNUSED(timer);
}

#if !defined(UNIT_TEST) && !defined(USE_POSITION_HOLD) && !defined(USE_ALTITUDE_HOLD)
// shortcut: minimal nav/altitude stubs while GPS, baro, position hold, and alt hold remain disabled on ARIESV3
void positionInit(void) {}
int32_t getEstimatedAltitudeCm(void) { return 0; }
void autopilotInit(void) {}
bool isBelowLandingAltitude(void) { return false; }
bool autopilotYawControlActive(void) { return false; }
float autopilotGetYawRate(void) { return 0.0f; }
#endif
