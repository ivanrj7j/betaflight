/*
 * Testable interface for ARIES v3.0 Motor PWM Actuation and Timer 1 Pulse Scheduler.
 * Host unit test shim mirroring src/platform/ARIES/pwm_motor_aries.c.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TEST_ARIES_MAX_MOTORS 4
#define TEST_THEJAS32_IRQ_TIMER1 8

typedef enum {
    MOTOR_PROTOCOL_PWM = 0,
    MOTOR_PROTOCOL_ONESHOT125,
    MOTOR_PROTOCOL_ONESHOT42,
    MOTOR_PROTOCOL_MULTISHOT,
    MOTOR_PROTOCOL_BRUSHED,
    MOTOR_PROTOCOL_DSHOT150,
    MOTOR_PROTOCOL_DSHOT300,
    MOTOR_PROTOCOL_DSHOT600,
    MOTOR_PROTOCOL_PROSHOT1000
} test_motorProtocolTypes_e;

typedef struct {
    uint8_t index;
    uint32_t pulseCycles;
} test_motorSort_t;

typedef struct {
    void *io;
    bool enabled;
} test_pwmMotor_t;

typedef struct {
    volatile uint32_t LoadCount;
    volatile uint32_t CurrentValue;
    volatile uint32_t Control;
    volatile uint32_t EOI;
    volatile uint32_t IntStatus;
} test_thejas32_timer1_reg_t;

// Test motor state
extern test_pwmMotor_t test_pwmMotors[TEST_ARIES_MAX_MOTORS];
extern uint8_t test_pwmMotorCount;
extern uint32_t test_ariesMotorPulseCycles[TEST_ARIES_MAX_MOTORS];
extern test_motorProtocolTypes_e test_activeMotorProtocol;
extern volatile test_motorSort_t test_sortedMotors[TEST_ARIES_MAX_MOTORS];
extern volatile uint8_t test_sortedMotorCount;
extern volatile uint8_t test_currentMotorIdx;
extern test_thejas32_timer1_reg_t test_mock_timer1;

// Device driver methods under test
void test_pwmWriteMotor(uint8_t index, float value);
void test_pwmWriteMotorInt(uint8_t index, uint16_t value);
void test_pwmCompleteMotorUpdate(void);
void test_thejas32_pwm_timer1_isr(void);
void test_pwmShutdownPulsesForAllMotors(void);
bool test_motorPwmDevInit(uint8_t count, test_motorProtocolTypes_e protocol);

void test_motor_reset(void);
bool test_motor_is_pin_high(uint8_t motorIndex);

#ifdef __cplusplus
}
#endif
