/*
 * Host-compilable unit test implementation for ARIES v3.0 Motor PWM Actuator Driver.
 * Mirrors src/platform/ARIES/pwm_motor_aries.c.
 */

#include "aries_motor_testable.h"
#include "aries_gpio_testable.h"
#include "aries/mock_thejas32.h"
#include <string.h>

test_pwmMotor_t test_pwmMotors[TEST_ARIES_MAX_MOTORS];
uint8_t test_pwmMotorCount = 0;
uint32_t test_ariesMotorPulseCycles[TEST_ARIES_MAX_MOTORS];
test_motorProtocolTypes_e test_activeMotorProtocol = MOTOR_PROTOCOL_PWM;

volatile test_motorSort_t test_sortedMotors[TEST_ARIES_MAX_MOTORS];
volatile uint8_t test_sortedMotorCount = 0;
volatile uint8_t test_currentMotorIdx = 0;

test_thejas32_timer1_reg_t test_mock_timer1;

static inline float test_constrainf(float amt, float low, float high)
{
    if (amt < low) return low;
    if (amt > high) return high;
    return amt;
}

void test_motor_reset(void)
{
    test_IOInitGlobal();
    mock_thejas32_reset();

    memset(&test_mock_timer1, 0, sizeof(test_mock_timer1));
    test_mock_timer1.Control = 0;

    test_pwmMotorCount = TEST_ARIES_MAX_MOTORS;
    test_activeMotorProtocol = MOTOR_PROTOCOL_PWM;
    test_sortedMotorCount = 0;
    test_currentMotorIdx = 0;

    for (uint8_t i = 0; i < TEST_ARIES_MAX_MOTORS; i++) {
        // Assign pins PA1, PA2, PA3, PA4 (indices 1, 2, 3, 4)
        test_pwmMotors[i].io = (void *)test_IOGetByPin(i + 1);
        test_pwmMotors[i].enabled = true;
        test_ariesMotorPulseCycles[i] = 100000; // 1000 µs standard
        if (test_pwmMotors[i].io) {
            test_IOLo((test_ioRec_t *)test_pwmMotors[i].io);
        }
    }
}

bool test_motor_is_pin_high(uint8_t motorIndex)
{
    if (motorIndex < TEST_ARIES_MAX_MOTORS && test_pwmMotors[motorIndex].io) {
        return test_IORead((test_ioRec_t *)test_pwmMotors[motorIndex].io);
    }
    return false;
}

void test_thejas32_pwm_timer1_isr(void)
{
    // Acknowledge interrupt flag in hardware timer
    (void)test_mock_timer1.EOI;

    while (test_currentMotorIdx < test_sortedMotorCount) {
        uint8_t motorNum = test_sortedMotors[test_currentMotorIdx].index;
        if (motorNum < test_pwmMotorCount && test_pwmMotors[motorNum].io) {
            test_IOLo((test_ioRec_t *)test_pwmMotors[motorNum].io);
        }
        test_currentMotorIdx++;

        if (test_currentMotorIdx < test_sortedMotorCount) {
            uint32_t delta = test_sortedMotors[test_currentMotorIdx].pulseCycles -
                             test_sortedMotors[test_currentMotorIdx - 1].pulseCycles;
            if (delta < 50) {
                // If delta is less than 0.5 µs (50 clock cycles at 100 MHz), drop simultaneously
                continue;
            } else {
                // Arm Timer 1 for next motor deadline
                test_mock_timer1.Control = 0;
                test_mock_timer1.LoadCount = delta;
                test_mock_timer1.Control = 0x03; // User-defined periodic, interrupt unmasked
                return;
            }
        }
    }

    // All active motor pulses complete: disable Timer 1
    test_mock_timer1.Control = 0;
}

void test_pwmShutdownPulsesForAllMotors(void)
{
    test_mock_timer1.Control = 0;
    for (uint8_t i = 0; i < test_pwmMotorCount && i < TEST_ARIES_MAX_MOTORS; i++) {
        if (test_pwmMotors[i].io) {
            test_IOLo((test_ioRec_t *)test_pwmMotors[i].io);
        }
    }
}

void test_pwmWriteMotor(uint8_t index, float value)
{
    if (index >= test_pwmMotorCount || index >= TEST_ARIES_MAX_MOTORS || !test_pwmMotors[index].enabled) {
        return;
    }

    uint32_t cycles;
    if (test_activeMotorProtocol == MOTOR_PROTOCOL_ONESHOT125) {
        // OneShot125: 125 µs (12,500 cycles) to 250 µs (25,000 cycles)
        float pulseUs = 125.0f + (test_constrainf(value, 1000.0f, 2000.0f) - 1000.0f) * 0.125f;
        cycles = (uint32_t)(pulseUs * 100.0f);
    } else {
        // Standard PWM: 1000 µs (100,000 cycles) to 2000 µs (200,000 cycles)
        float pulseUs = test_constrainf(value, 1000.0f, 2000.0f);
        cycles = (uint32_t)(pulseUs * 100.0f);
    }

    test_ariesMotorPulseCycles[index] = cycles;
}

void test_pwmWriteMotorInt(uint8_t index, uint16_t value)
{
    test_pwmWriteMotor(index, (float)value);
}

void test_pwmCompleteMotorUpdate(void)
{
    uint8_t count = 0;
    for (uint8_t i = 0; i < test_pwmMotorCount && i < TEST_ARIES_MAX_MOTORS; i++) {
        if (test_pwmMotors[i].enabled && test_pwmMotors[i].io) {
            test_sortedMotors[count].index = i;
            test_sortedMotors[count].pulseCycles = test_ariesMotorPulseCycles[i];
            count++;
        }
    }

    if (count == 0) {
        return;
    }

    // Sort active motors by pulse width (ascending)
    for (uint8_t i = 0; i < count - 1; i++) {
        for (uint8_t j = i + 1; j < count; j++) {
            if (test_sortedMotors[i].pulseCycles > test_sortedMotors[j].pulseCycles) {
                test_motorSort_t tmp = test_sortedMotors[i];
                test_sortedMotors[i] = test_sortedMotors[j];
                test_sortedMotors[j] = tmp;
            }
        }
    }

    // Set all active motor GPIO pins HIGH
    for (uint8_t i = 0; i < count; i++) {
        test_IOHi((test_ioRec_t *)test_pwmMotors[test_sortedMotors[i].index].io);
    }

    test_sortedMotorCount = count;
    test_currentMotorIdx = 0;

    // Load Timer 1 with the first motor deadline
    uint32_t firstCycles = test_sortedMotors[0].pulseCycles;
    if (firstCycles < 100) {
        firstCycles = 100;
    }

    test_mock_timer1.Control = 0;
    test_mock_timer1.LoadCount = firstCycles;
    test_mock_timer1.Control = 0x03; // User-defined periodic, interrupt unmasked
}

bool test_motorPwmDevInit(uint8_t count, test_motorProtocolTypes_e protocol)
{
    test_pwmMotorCount = count;
    if (test_pwmMotorCount > TEST_ARIES_MAX_MOTORS || test_pwmMotorCount == 0) {
        test_pwmMotorCount = TEST_ARIES_MAX_MOTORS;
    }

    test_activeMotorProtocol = protocol;

    // Register Timer 1 interrupt on IRQ 8 for non-blocking pulse completion
    thejas32_interrupt_register(TEST_THEJAS32_IRQ_TIMER1, test_thejas32_pwm_timer1_isr);
    thejas32_interrupt_enable(TEST_THEJAS32_IRQ_TIMER1);

    test_mock_timer1.Control = 0;

    for (uint8_t i = 0; i < test_pwmMotorCount; i++) {
        test_ioRec_t *io = test_IOGetByPin(i + 1);
        if (io) {
            test_IOConfigGPIO(io, THEJAS32_GPIO_MODE_OUTPUT);
            test_IOLo(io);
            test_pwmMotors[i].io = io;
            test_pwmMotors[i].enabled = true;
        }

        test_ariesMotorPulseCycles[i] = (test_activeMotorProtocol == MOTOR_PROTOCOL_ONESHOT125) ? 12500 : 100000;
    }

    return true;
}
