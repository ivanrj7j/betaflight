/*
 * This file is part of Betaflight.
 *
 * Motor PWM actuator driver for C-DAC ARIES v3.0 (THEJAS32 / VEGA ET1031).
 * Maps 4 quadcopter motors to Header J2 & J1:
 *   Motor 1 (Rear Right)  : PA1 (J2_5)
 *   Motor 2 (Front Right) : PA2 (J2_3)
 *   Motor 3 (Rear Left)   : PA3 (J2_1)
 *   Motor 4 (Front Left)  : PA4 (J1_10)
 *
 * Uses hardware Timer 1 (0x10000A14, IRQ 8) for non-blocking sub-microsecond pulse timing.
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

#include "platform.h"
#include "platform/bsp_thejas32.h"

#include "common/maths.h"
#include "common/utils.h"

#include "drivers/io.h"
#include "drivers/io_impl.h"
#include "drivers/motor.h"
#include "drivers/motor_types.h"
#include "drivers/pwm_output.h"
#include "drivers/time.h"

#include "pg/motor.h"

#define ARIES_MAX_MOTORS 4

// Default pin assignments on ARIES v3.0 headers
static const ioTag_t defaultMotorTags[ARIES_MAX_MOTORS] = {
    DEFIO_TAG__PA1, // Motor 1 (J2_5)
    DEFIO_TAG__PA2, // Motor 2 (J2_3)
    DEFIO_TAG__PA3, // Motor 3 (J2_1)
    DEFIO_TAG__PA4  // Motor 4 (J1_10)
};

static uint32_t ariesMotorPulseCycles[ARIES_MAX_MOTORS];
static motorProtocolTypes_e activeMotorProtocol = MOTOR_PROTOCOL_PWM;

typedef struct {
    uint8_t index;
    uint32_t pulseCycles;
} motorSort_t;

static volatile motorSort_t sortedMotors[ARIES_MAX_MOTORS];
static volatile uint8_t sortedMotorCount = 0;
static volatile uint8_t currentMotorIdx = 0;

// -----------------------------------------------------------------------------
// Hardware Timer 1 Interrupt Service Routine (IRQ 8)
// -----------------------------------------------------------------------------
void thejas32_pwm_timer1_isr(void)
{
    // Acknowledge interrupt flag in hardware timer (RSK-03)
    (void)THEJAS32_TIMER(1).EOI;

    while (currentMotorIdx < sortedMotorCount) {
        uint8_t motorNum = sortedMotors[currentMotorIdx].index;
        if (motorNum < pwmMotorCount && pwmMotors[motorNum].io) {
            IOLo(pwmMotors[motorNum].io);
        }
        currentMotorIdx++;

        if (currentMotorIdx < sortedMotorCount) {
            uint32_t delta = sortedMotors[currentMotorIdx].pulseCycles - sortedMotors[currentMotorIdx - 1].pulseCycles;
            if (delta < 50) {
                // If delta is less than 0.5 µs (50 clock cycles at 100 MHz), drop simultaneously
                continue;
            } else {
                // Arm Timer 1 for next motor deadline
                THEJAS32_TIMER(1).Control = 0;
                THEJAS32_TIMER(1).LoadCount = delta;
                THEJAS32_TIMER(1).Control = 0x03; // User-defined periodic, interrupt unmasked
                __asm__ volatile ("fence" ::: "memory");
                return;
            }
        }
    }

    // All active motor pulses complete: disable Timer 1
    THEJAS32_TIMER(1).Control = 0;
    __asm__ volatile ("fence" ::: "memory");
}

// -----------------------------------------------------------------------------
// Motor Device VTable Methods
// -----------------------------------------------------------------------------

static void pwmMotorPostInit(void)
{
}

static float pwmConvertFromExternal(uint16_t externalValue)
{
    return (float)externalValue;
}

static uint16_t pwmConvertToExternal(float motorValue)
{
    return (uint16_t)motorValue;
}

void pwmShutdownPulsesForAllMotors(void)
{
    THEJAS32_TIMER(1).Control = 0;
    for (uint8_t i = 0; i < pwmMotorCount && i < ARIES_MAX_MOTORS; i++) {
        if (pwmMotors[i].io) {
            IOLo(pwmMotors[i].io);
        }
    }
    __asm__ volatile ("fence" ::: "memory");
}

static bool pwmEnableMotors(void)
{
    return pwmMotorCount > 0;
}

static bool pwmIsMotorEnabled(unsigned index)
{
    return index < pwmMotorCount && pwmMotors[index].enabled;
}

static IO_t pwmGetMotorIO(unsigned index)
{
    if (index >= pwmMotorCount) {
        return IO_NONE;
    }
    return pwmMotors[index].io;
}

static void pwmDisableMotors(void)
{
    pwmShutdownPulsesForAllMotors();
}

static void pwmWriteMotor(uint8_t index, float value)
{
    if (index >= pwmMotorCount || index >= ARIES_MAX_MOTORS || !pwmMotors[index].enabled) {
        return;
    }

    uint32_t cycles;
    if (activeMotorProtocol == MOTOR_PROTOCOL_ONESHOT125) {
        // OneShot125: 125 µs (12,500 cycles) to 250 µs (25,000 cycles)
        float pulseUs = 125.0f + (constrainf(value, 1000.0f, 2000.0f) - 1000.0f) * 0.125f;
        cycles = (uint32_t)(pulseUs * 100.0f);
    } else {
        // Standard PWM: 1000 µs (100,000 cycles) to 2000 µs (200,000 cycles)
        float pulseUs = constrainf(value, 1000.0f, 2000.0f);
        cycles = (uint32_t)(pulseUs * 100.0f);
    }

    ariesMotorPulseCycles[index] = cycles;
}

static void pwmWriteMotorInt(uint8_t index, uint16_t value)
{
    pwmWriteMotor(index, (float)value);
}

static void pwmCompleteMotorUpdate(void)
{
    uint8_t count = 0;
    for (uint8_t i = 0; i < pwmMotorCount && i < ARIES_MAX_MOTORS; i++) {
        if (pwmMotors[i].enabled && pwmMotors[i].io) {
            sortedMotors[count].index = i;
            sortedMotors[count].pulseCycles = ariesMotorPulseCycles[i];
            count++;
        }
    }

    if (count == 0) {
        return;
    }

    // Sort active motors by pulse width (ascending)
    for (uint8_t i = 0; i < count - 1; i++) {
        for (uint8_t j = i + 1; j < count; j++) {
            if (sortedMotors[i].pulseCycles > sortedMotors[j].pulseCycles) {
                motorSort_t tmp = sortedMotors[i];
                sortedMotors[i] = sortedMotors[j];
                sortedMotors[j] = tmp;
            }
        }
    }

    // Set all active motor GPIO pins HIGH
    for (uint8_t i = 0; i < count; i++) {
        IOHi(pwmMotors[sortedMotors[i].index].io);
    }

    sortedMotorCount = count;
    currentMotorIdx = 0;

    // Load Timer 1 with the first motor deadline
    uint32_t firstCycles = sortedMotors[0].pulseCycles;
    if (firstCycles < 100) {
        firstCycles = 100;
    }

    THEJAS32_TIMER(1).Control = 0;
    THEJAS32_TIMER(1).LoadCount = firstCycles;
    THEJAS32_TIMER(1).Control = 0x03; // User-defined periodic, interrupt unmasked
    __asm__ volatile ("fence" ::: "memory");
}

static void pwmReleaseMotorIO(unsigned index)
{
    UNUSED(index);
}

static bool pwmReinstateMotorIO(unsigned index)
{
    UNUSED(index);
    return true;
}

static const motorVTable_t motorPwmVTable = {
    .postInit = pwmMotorPostInit,
    .convertExternalToMotor = pwmConvertFromExternal,
    .convertMotorToExternal = pwmConvertToExternal,
    .enable = pwmEnableMotors,
    .disable = pwmDisableMotors,
    .isMotorEnabled = pwmIsMotorEnabled,
    .telemetryWait = NULL,
    .decodeTelemetry = NULL,
    .updateInit = NULL,
    .write = pwmWriteMotor,
    .writeInt = pwmWriteMotorInt,
    .updateComplete = pwmCompleteMotorUpdate,
    .shutdown = pwmShutdownPulsesForAllMotors,
    .isMotorIdle = NULL,
    .getMotorIO = pwmGetMotorIO,
    .releaseMotorIO = pwmReleaseMotorIO,
    .reinstateMotorIO = pwmReinstateMotorIO,
    .requestTelemetry = NULL,
};

// -----------------------------------------------------------------------------
// Device Initialization
// -----------------------------------------------------------------------------

bool motorPwmDevInit(motorDevice_t *device, const motorDevConfig_t *motorDevConfig, uint16_t idlePulse)
{
    UNUSED(idlePulse);

    if (!device || !motorDevConfig) {
        return false;
    }

    device->vTable = &motorPwmVTable;
    pwmMotorCount = device->count;
    if (pwmMotorCount > ARIES_MAX_MOTORS || pwmMotorCount == 0) {
        pwmMotorCount = ARIES_MAX_MOTORS;
    }
    device->count = pwmMotorCount;

    activeMotorProtocol = motorDevConfig->motorProtocol;

    // Register Timer 1 interrupt on IRQ 8 for non-blocking pulse completion
    thejas32_interrupt_register(THEJAS32_IRQ_TIMER1, thejas32_pwm_timer1_isr);
    thejas32_interrupt_enable(THEJAS32_IRQ_TIMER1);

    THEJAS32_TIMER(1).Control = 0;

    for (uint8_t i = 0; i < pwmMotorCount; i++) {
        ioTag_t tag = motorDevConfig->ioTags[i];
        if (!tag) {
            tag = defaultMotorTags[i];
        }

        IO_t io = IOGetByTag(tag);
        if (io) {
            IOInit(io, OWNER_MOTOR, RESOURCE_INDEX(i));
            IOConfigGPIO(io, IOCFG_OUT_PP);
            IOLo(io);
            pwmMotors[i].io = io;
            pwmMotors[i].enabled = true;
        }

        // Initialize default idle pulse (1000 µs standard = 100,000 cycles; 125 µs OneShot = 12,500 cycles)
        ariesMotorPulseCycles[i] = (activeMotorProtocol == MOTOR_PROTOCOL_ONESHOT125) ? 12500 : 100000;
    }

    return true;
}

bool dshotPwmDevInit(motorDevice_t *device, const motorDevConfig_t *motorConfig)
{
    UNUSED(device);
    UNUSED(motorConfig);
    return false;
}
