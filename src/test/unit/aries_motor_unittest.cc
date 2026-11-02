/*
 * Unit tests for Betaflight ARIES v3.0 Motor PWM Actuation and Timer 1 Pulse Scheduler.
 * Test Suite: Module T7 (Phase 6)
 */

#include "unittest_macros.h"
#include "gtest/gtest.h"

extern "C" {
    #include "aries_motor_testable.h"
    #include "aries_gpio_testable.h"
    #include "aries/mock_thejas32.h"
}

class AriesMotorTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_motor_reset();
    }
};

// T7-01: Standard PWM 1000 µs sets 100,000 cycles
TEST_F(AriesMotorTest, StandardPWM1000us) {
    test_activeMotorProtocol = MOTOR_PROTOCOL_PWM;
    test_pwmWriteMotor(0, 1000.0f);
    EXPECT_EQ(test_ariesMotorPulseCycles[0], 100000U);
}

// T7-02: Standard PWM 2000 µs sets 200,000 cycles
TEST_F(AriesMotorTest, StandardPWM2000us) {
    test_activeMotorProtocol = MOTOR_PROTOCOL_PWM;
    test_pwmWriteMotor(0, 2000.0f);
    EXPECT_EQ(test_ariesMotorPulseCycles[0], 200000U);
}

// T7-03: Standard PWM 1500 µs sets 150,000 cycles
TEST_F(AriesMotorTest, StandardPWM1500us) {
    test_activeMotorProtocol = MOTOR_PROTOCOL_PWM;
    test_pwmWriteMotor(0, 1500.0f);
    EXPECT_EQ(test_ariesMotorPulseCycles[0], 150000U);
}

// T7-04: OneShot125 1000 sets 12,500 cycles (125 µs)
TEST_F(AriesMotorTest, OneShot125MinThrottle) {
    test_activeMotorProtocol = MOTOR_PROTOCOL_ONESHOT125;
    test_pwmWriteMotor(0, 1000.0f);
    EXPECT_EQ(test_ariesMotorPulseCycles[0], 12500U);
}

// T7-05: OneShot125 2000 sets 25,000 cycles (250 µs)
TEST_F(AriesMotorTest, OneShot125MaxThrottle) {
    test_activeMotorProtocol = MOTOR_PROTOCOL_ONESHOT125;
    test_pwmWriteMotor(0, 2000.0f);
    EXPECT_EQ(test_ariesMotorPulseCycles[0], 25000U);
}

// OneShot125 mid-point test: 1500 -> 187.5 µs = 18,750 cycles
TEST_F(AriesMotorTest, OneShot125MidThrottle) {
    test_activeMotorProtocol = MOTOR_PROTOCOL_ONESHOT125;
    test_pwmWriteMotor(0, 1500.0f);
    EXPECT_EQ(test_ariesMotorPulseCycles[0], 18750U);
}

// T7-06: Value below 1000 clamps to 1000 (100,000 cycles)
TEST_F(AriesMotorTest, ClampLowUnderflow) {
    test_activeMotorProtocol = MOTOR_PROTOCOL_PWM;
    test_pwmWriteMotor(0, 500.0f);
    EXPECT_EQ(test_ariesMotorPulseCycles[0], 100000U);

    test_pwmWriteMotor(0, 0.0f);
    EXPECT_EQ(test_ariesMotorPulseCycles[0], 100000U);
}

// T7-07: Value above 2000 clamps to 2000 (200,000 cycles)
TEST_F(AriesMotorTest, ClampHighOverflow) {
    test_activeMotorProtocol = MOTOR_PROTOCOL_PWM;
    test_pwmWriteMotor(0, 2500.0f);
    EXPECT_EQ(test_ariesMotorPulseCycles[0], 200000U);

    test_pwmWriteMotor(0, 5000.0f);
    EXPECT_EQ(test_ariesMotorPulseCycles[0], 200000U);
}

// T7-08: CompleteMotorUpdate sorts active motors ascending by pulse width
TEST_F(AriesMotorTest, UpdateSortsMotorsAscending) {
    test_activeMotorProtocol = MOTOR_PROTOCOL_PWM;
    test_pwmWriteMotor(0, 1800.0f); // 180,000
    test_pwmWriteMotor(1, 1200.0f); // 120,000
    test_pwmWriteMotor(2, 1900.0f); // 190,000
    test_pwmWriteMotor(3, 1100.0f); // 110,000

    test_pwmCompleteMotorUpdate();

    EXPECT_EQ(test_sortedMotorCount, 4U);
    EXPECT_EQ(test_sortedMotors[0].index, 3U);
    EXPECT_EQ(test_sortedMotors[0].pulseCycles, 110000U);

    EXPECT_EQ(test_sortedMotors[1].index, 1U);
    EXPECT_EQ(test_sortedMotors[1].pulseCycles, 120000U);

    EXPECT_EQ(test_sortedMotors[2].index, 0U);
    EXPECT_EQ(test_sortedMotors[2].pulseCycles, 180000U);

    EXPECT_EQ(test_sortedMotors[3].index, 2U);
    EXPECT_EQ(test_sortedMotors[3].pulseCycles, 190000U);
}

// T7-09: CompleteMotorUpdate drives all active motor pins HIGH
TEST_F(AriesMotorTest, UpdateDrivesAllPinsHigh) {
    for (int i = 0; i < 4; i++) {
        test_pwmWriteMotor(i, 1500.0f);
        EXPECT_FALSE(test_motor_is_pin_high(i));
    }

    test_pwmCompleteMotorUpdate();

    for (int i = 0; i < 4; i++) {
        EXPECT_TRUE(test_motor_is_pin_high(i));
    }
}

// T7-10: CompleteMotorUpdate arms Timer 1 with first deadline
TEST_F(AriesMotorTest, UpdateArmsTimer1WithFirstDeadline) {
    test_pwmWriteMotor(0, 1600.0f); // 160,000
    test_pwmWriteMotor(1, 1300.0f); // 130,000 (earliest)
    test_pwmWriteMotor(2, 1700.0f);
    test_pwmWriteMotor(3, 1900.0f);

    test_pwmCompleteMotorUpdate();

    EXPECT_EQ(test_mock_timer1.LoadCount, 130000U);
    EXPECT_EQ(test_mock_timer1.Control, 0x03U); // Periodic & unmasked
}

// T7-11: Timer 1 ISR drops pin LOW for expired motor
TEST_F(AriesMotorTest, IsrDropsPinForExpiredMotor) {
    test_pwmWriteMotor(0, 1500.0f);
    test_pwmWriteMotor(1, 1200.0f); // Earliest (Motor 1)
    test_pwmWriteMotor(2, 1700.0f);
    test_pwmWriteMotor(3, 1900.0f);

    test_pwmCompleteMotorUpdate();
    EXPECT_TRUE(test_motor_is_pin_high(1));

    // First timer ISR fires (deadline for Motor 1 expired)
    test_thejas32_pwm_timer1_isr();

    EXPECT_FALSE(test_motor_is_pin_high(1)); // Motor 1 dropped LOW
    EXPECT_TRUE(test_motor_is_pin_high(0));  // Others still HIGH
    EXPECT_TRUE(test_motor_is_pin_high(2));
    EXPECT_TRUE(test_motor_is_pin_high(3));
}

// T7-12: Timer 1 ISR reloads Timer 1 with delta for next motor
TEST_F(AriesMotorTest, IsrReloadsDeltaForNextMotor) {
    test_pwmWriteMotor(0, 1500.0f); // 150,000
    test_pwmWriteMotor(1, 1200.0f); // 120,000
    test_pwmWriteMotor(2, 1700.0f); // 170,000
    test_pwmWriteMotor(3, 1900.0f); // 190,000

    test_pwmCompleteMotorUpdate();
    EXPECT_EQ(test_mock_timer1.LoadCount, 120000U);

    // Motor 1 fires: next is Motor 0 (150,000). Delta = 150,000 - 120,000 = 30,000
    test_thejas32_pwm_timer1_isr();
    EXPECT_EQ(test_mock_timer1.LoadCount, 30000U);
    EXPECT_EQ(test_mock_timer1.Control, 0x03U);

    // Motor 0 fires: next is Motor 2 (170,000). Delta = 170,000 - 150,000 = 20,000
    test_thejas32_pwm_timer1_isr();
    EXPECT_EQ(test_mock_timer1.LoadCount, 20000U);
    EXPECT_EQ(test_mock_timer1.Control, 0x03U);

    // Motor 2 fires: next is Motor 3 (190,000). Delta = 190,000 - 170,000 = 20,000
    test_thejas32_pwm_timer1_isr();
    EXPECT_EQ(test_mock_timer1.LoadCount, 20000U);
    EXPECT_EQ(test_mock_timer1.Control, 0x03U);
}

// T7-13: Timer 1 ISR disables Timer 1 after last motor completes
TEST_F(AriesMotorTest, IsrDisablesTimer1AfterLastMotor) {
    test_pwmWriteMotor(0, 1100.0f);
    test_pwmWriteMotor(1, 1200.0f);
    test_pwmWriteMotor(2, 1300.0f);
    test_pwmWriteMotor(3, 1400.0f);

    test_pwmCompleteMotorUpdate();

    test_thejas32_pwm_timer1_isr(); // Motor 0 done
    EXPECT_EQ(test_mock_timer1.Control, 0x03U);

    test_thejas32_pwm_timer1_isr(); // Motor 1 done
    EXPECT_EQ(test_mock_timer1.Control, 0x03U);

    test_thejas32_pwm_timer1_isr(); // Motor 2 done
    EXPECT_EQ(test_mock_timer1.Control, 0x03U);

    test_thejas32_pwm_timer1_isr(); // Motor 3 done (final motor)
    EXPECT_EQ(test_mock_timer1.Control, 0x00U); // Disabled!

    for (int i = 0; i < 4; i++) {
        EXPECT_FALSE(test_motor_is_pin_high(i));
    }
}

// T7-14: Timer 1 ISR coalesces motors with delta < 50 cycles
TEST_F(AriesMotorTest, IsrCoalescesSub50CycleDelta) {
    test_pwmWriteMotor(0, 1500.0f);
    // Directly inject a pulse cycle count differing by only 20 cycles (< 50)
    test_ariesMotorPulseCycles[0] = 150000;
    test_ariesMotorPulseCycles[1] = 150020; // delta = 20 cycles (< 50)
    test_ariesMotorPulseCycles[2] = 180000;
    test_ariesMotorPulseCycles[3] = 190000;

    test_pwmCompleteMotorUpdate();

    // In first ISR call, motor 0 drops, delta to motor 1 is 20 (< 50),
    // so motor 1 drops in the SAME ISR without setting a new timer interrupt!
    test_thejas32_pwm_timer1_isr();

    EXPECT_FALSE(test_motor_is_pin_high(0));
    EXPECT_FALSE(test_motor_is_pin_high(1)); // Coalesced in same ISR!
    EXPECT_TRUE(test_motor_is_pin_high(2));
    EXPECT_TRUE(test_motor_is_pin_high(3));

    // Next deadline should be delta from motor 1 to motor 2: 180,000 - 150,020 = 29,980
    EXPECT_EQ(test_mock_timer1.LoadCount, 29980U);
}

// T7-15: pwmShutdownPulsesForAllMotors drives all pins LOW
TEST_F(AriesMotorTest, ShutdownDrivesAllPinsLow) {
    for (int i = 0; i < 4; i++) {
        test_pwmWriteMotor(i, 1500.0f);
    }
    test_pwmCompleteMotorUpdate();

    for (int i = 0; i < 4; i++) {
        EXPECT_TRUE(test_motor_is_pin_high(i));
    }

    test_pwmShutdownPulsesForAllMotors();

    for (int i = 0; i < 4; i++) {
        EXPECT_FALSE(test_motor_is_pin_high(i));
    }
}

// T7-16: pwmShutdownPulsesForAllMotors disables Timer 1
TEST_F(AriesMotorTest, ShutdownDisablesTimer1) {
    test_pwmWriteMotor(0, 1500.0f);
    test_pwmCompleteMotorUpdate();
    EXPECT_EQ(test_mock_timer1.Control, 0x03U);

    test_pwmShutdownPulsesForAllMotors();
    EXPECT_EQ(test_mock_timer1.Control, 0x00U);
}

// T7-17: Disabled motor index is rejected by pwmWriteMotor
TEST_F(AriesMotorTest, DisabledMotorIsRejected) {
    test_pwmMotors[1].enabled = false;
    test_ariesMotorPulseCycles[1] = 100000;

    test_pwmWriteMotor(1, 1800.0f);
    // Must remain unchanged at 100,000
    EXPECT_EQ(test_ariesMotorPulseCycles[1], 100000U);

    // Out-of-bounds index rejected
    test_pwmWriteMotor(4, 1800.0f);
    test_pwmWriteMotor(255, 1800.0f);
}

// T7-18: motorPwmDevInit registers ISR on IRQ 8 and enables IRQ 8
TEST_F(AriesMotorTest, DevInitRegistersTimer1Irq8) {
    EXPECT_FALSE(mock_irq_is_enabled(TEST_THEJAS32_IRQ_TIMER1));

    bool ok = test_motorPwmDevInit(4, MOTOR_PROTOCOL_PWM);
    EXPECT_TRUE(ok);

    EXPECT_TRUE(mock_irq_is_enabled(TEST_THEJAS32_IRQ_TIMER1));
    EXPECT_EQ(mock_irq_table[TEST_THEJAS32_IRQ_TIMER1], test_thejas32_pwm_timer1_isr);
}

// Integer API delegation test
TEST_F(AriesMotorTest, WriteMotorIntDelegatesCorrectly) {
    test_activeMotorProtocol = MOTOR_PROTOCOL_PWM;
    test_pwmWriteMotorInt(0, 1750);
    EXPECT_EQ(test_ariesMotorPulseCycles[0], 175000U);
}
