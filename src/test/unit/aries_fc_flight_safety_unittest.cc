/*
 * Test Suite: C-DAC ARIES v3.0 Full Flight Control Safety Pipeline Integration (Wave 2 - Suite 6)
 * Target Silicon: C-DAC THEJAS32 SoC (VEGA ET1031 RISC-V RV32IM @ 100 MHz, 248 KB SRAM, soft-float)
 * Integration Triggers: Trigger 2 (Bench Integration) & Trigger 3 (Motor Bench Spin)
 * References & Citations:
 *   - PLAN/02_MOTOR_PWM_SCHEDULING.md
 *   - PLAN/06_SENSORS_RC_TELEMETRY_AND_SCHEDULER.md
 *   - src/main/flight/imu.c, src/main/flight/pid.c, src/main/flight/pid_init.c, src/main/flight/mixer.c
 *   - src/platform/ARIES/pwm_motor_aries.c (lines 208-217 NAN clamping defect!)
 *
 * Methodology:
 *   Strict Test-Driven Development (TDD) - Step 1: Living Documentation & TDD-RED.
 *   Tests real soft-float IMU -> PID -> Mixer -> ARIES Timer 1 PWM motor pipeline.
 *   Exposes the real NAN/Inf safety defect in pwm_motor_aries.c:ariesPwmMotorWrite()
 *   as an intentional TDD-RED assertion until Step 3 fix is approved.
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <cmath>
#include <limits.h>

#include "unittest_macros.h"
#include "gtest/gtest.h"

extern "C" {
    #include "../main/platform.h"
    #include "build/build_config.h"
    #include "build/debug.h"
    #include "common/axis.h"
    #include "common/filter.h"
    #include "common/maths.h"
    #include "common/vector.h"
    #include "config/config.h"
    #include "config/feature.h"
    #include "drivers/accgyro/accgyro.h"
    #include "drivers/compass/compass.h"
    #include "drivers/io.h"
    #include "drivers/motor.h"
    #include "drivers/motor_types.h"
    #include "drivers/pwm_output.h"
    #include "drivers/sensor.h"
    #include "drivers/sound_beeper.h"
    #include "drivers/system.h"
    #include "drivers/time.h"
    #include "fc/controlrate_profile.h"
    #include "fc/core.h"
    #include "fc/rc.h"
    #include "fc/rc_controls.h"
    #include "fc/rc_modes.h"
    #include "fc/runtime_config.h"
    #include "flight/imu.h"
    #include "flight/mixer.h"
    #include "flight/mixer_init.h"
    #include "flight/pid.h"
    #include "flight/pid_init.h"
    #include "flight/position.h"
    #include "flight/position_estimator.h"
    #include "io/beeper.h"
    #include "io/gps.h"
    #include "pg/autopilot.h"
    #include "pg/autopilot_wing.h"
    #include "pg/motor.h"
    #include "pg/pg.h"
    #include "pg/pg_ids.h"
    #include "pg/rx.h"
    #include "rx/rx.h"
    #include "sensors/acceleration.h"
    #include "sensors/barometer.h"
    #include "sensors/compass.h"
    #include "sensors/gyro.h"
    #include "sensors/sensors.h"
    #include "platform/ARIES/io_def_generated.h"
    #include "platform/ARIES/include/platform/bsp_thejas32.h"
    #include "aries_host_mmio.h"

    void imuComputeRotationMatrix(void);
    void imuUpdateEulerAngles(void);
    void imuMahonyAHRSupdate(float dt, float gx, float gy, float gz,
                             bool useAcc, float ax, float ay, float az,
                             float headingErrMag, float headingErrCog,
                             const float dcmKpGain);
    uint32_t handle_trap(uint32_t mcause, uint32_t mepc, uint32_t *sp);
    bool motorPwmDevInit(motorDevice_t *device, const motorDevConfig_t *motorDevConfig, uint16_t idlePulse);
    void ariesPwmEmergencyStop(void);
    extern quaternion_t q;
    extern matrix33_t rMat;
    extern bool attitudeIsEstablished;

    // Shared globals & minimal stubs for real imu.c + pid.c + mixer.c + pwm_motor_aries.c
    int16_t debug[DEBUG16_VALUE_COUNT];
    uint8_t debugMode = 0;
    float rcCommand[4] = { 0.0f, 0.0f, 0.0f, 1000.0f };
    float rcData[MAX_SUPPORTED_RC_CHANNEL_COUNT] = { 1500.0f, 1500.0f, 1500.0f, 1000.0f };
    gyro_t gyro;
    acc_t acc;
    mag_t mag;
    gpsSolutionData_t gpsSol;
    mixerRuntime_t mixerRuntime;

    static pidProfile_t s_testPidProfile;
    pidProfile_t *currentPidProfile = &s_testPidProfile;

    static float s_setpointRate[3] = { 0.0f, 0.0f, 0.0f };
    static float s_gyroADCf[3] = { 0.0f, 0.0f, 0.0f };

    PG_REGISTER(rxConfig_t, rxConfig, PG_RX_CONFIG, 0);
    PG_REGISTER(rcControlsConfig_t, rcControlsConfig, PG_RC_CONTROLS_CONFIG, 0);
    PG_REGISTER(accelerometerConfig_t, accelerometerConfig, PG_ACCELEROMETER_CONFIG, 0);
    PG_REGISTER(systemConfig_t, systemConfig, PG_SYSTEM_CONFIG, 0);
    PG_REGISTER(barometerConfig_t, barometerConfig, PG_BAROMETER_CONFIG, 0);
    PG_REGISTER(gpsConfig_t, gpsConfig, PG_GPS_CONFIG, 0);
    PG_REGISTER(autopilotConfig_t, autopilotConfig, PG_AUTOPILOT, 0);
    PG_REGISTER(mixerConfig_t, mixerConfig, PG_MIXER_CONFIG, 0);
    PG_REGISTER(flight3DConfig_t, flight3DConfig, PG_MOTOR_3D_CONFIG, 0);
    PG_REGISTER(motorConfig_t, motorConfig, PG_MOTOR_CONFIG, 0);

    void beeper(beeperMode_e) {}
    void beeperConfirmationBeeps(uint8_t) {}
    void systemBeep(bool) {}
    bool compassEnabledAndCalibrated(void) { return false; }
    bool baroIsCalibrated(void) { return false; }
    void performBaroCalibrationCycle(void) {}
    float baroCalculateAltitude(void) { return 0.0f; }
    float baroUpsampleAltitude(void) { return 0.0f; }
    float getBaroAltitude(void) { return 0.0f; }
    bool gpsHasNewData(uint16_t *) { return false; }
    bool gpsRescueIsRunning(void) { return false; }
    bool isFixedWing(void) { return false; }
    bool gyroGetAccumulationAverage(float *) { return false; }
    bool accGetAccumulationAverage(float *) { return false; }
    void pinioBoxTaskControl(void) {}
    void schedulerIgnoreTaskExecTime(void) {}
    void schedulerIgnoreTaskStateTime(void) {}
    void schedulerSetNextStateTime(timeDelta_t) {}
    bool schedulerGetIgnoreTaskExecTime(void) { return false; }
    float gyroGetFilteredDownsampled(int axis) { return s_gyroADCf[axis]; }
    bool gyroOverflowDetected(void) { return false; }
    bool gyroYawSpinDetected(void) { return false; }
    float getSetpointRate(int axis) { return s_setpointRate[axis]; }
    float getMaxRcRate(int) { return 670.0f; }
    float getRcDeflection(int axis) { return (rcData[axis] - 1500.0f) / 500.0f; }
    float getRcDeflectionAbs(int axis) { return fabsf(getRcDeflection(axis)); }
    float getRcDeflectionRaw(int axis) { return getRcDeflection(axis); }
    float getMaxRcDeflectionAbs(void) { return 0.0f; }
    float getRawSetpoint(int axis) { return s_setpointRate[axis]; }
    float getFeedforward(int) { return 0.0f; }
    bool wasThrottleRaised(void) { return true; }
    bool isBelowLandingAltitude(void) { return false; }
    bool isLaunchControlActive(void) { return false; }
    bool isCrashFlipModeActive(void) { return false; }
    bool isMotorsReversed(void) { return false; }
    bool failsafeIsActive(void) { return false; }
    void initRcProcessing(void) {}
    void disarm(flightLogDisarmReason_e) { DISABLE_ARMING_FLAG(ARMED); }
    bool mixerIsTricopter(void) { return false; }
    float mixerTricopterMotorCorrection(int) { return 0.0f; }
    bool motorIsEnabled(void) { return true; }
    bool motorIsMotorEnabled(unsigned) { return true; }
    uint16_t motorConvertToExternal(float v) { return (uint16_t)v; }
    void motorWriteAll(float *) {}
    void dynLpfGyroUpdate(float) {}
    float dynThrottle(float) { return 1.0f; }
    bool isMotorProtocolDshot(void) { return false; }
    bool isMotorProtocolEnabled(void) { return true; }
    void dshotSetPidLoopTime(uint32_t) {}
    void initEscEndpoints(void) {}
    void mixerInitProfile(void) {}
    bool autopilotThrottleValid(void) { return false; }
    float getAutopilotThrottle(void) { return 0.0f; }
    bool autopilotWingMotorStopRequested(void) { return false; }
    uint16_t getBatterySagCellVoltage(void) { return 0U; }
    void positionEstimatorInit(void) {}
    void positionEstimatorUpdate(void) {}
    void positionEstimatorResetZ(void) {}
    bool positionEstimatorIsValidZ(void) { return false; }
    float positionEstimatorGetAltitudeCm(void) { return 0.0f; }
    float positionEstimatorGetVerticalVelocity(void) { return 0.0f; }
    float positionEstimatorGetVerticalAcceleration(void) { return 0.0f; }
    float positionEstimatorGetTrustZ(void) { return 0.0f; }
    static positionEstimate3d_t s_posEst;
    const positionEstimate3d_t *positionEstimatorGetEstimate(void) { return &s_posEst; }
}

namespace {

constexpr uint32_t CYCLES_PER_US = 100U;

class AriesFcFlightSafetyTest : public ::testing::Test {
protected:
    motorDevice_t motorDev;
    motorDevConfig_t motorDevCfg;

    void SetUp() override {
        thejas32_host_reset_all();
        IOInitGlobal();
        pgResetAll();
        memset(&gyro, 0, sizeof(gyro));
        memset(&acc, 0, sizeof(acc));
        memset(&mag, 0, sizeof(mag));
        memset(&gpsSol, 0, sizeof(gpsSol));
        memset(&attitude, 0, sizeof(attitude));
        memset(&mixerRuntime, 0, sizeof(mixerRuntime));
        memset(&motor, 0, sizeof(motor));
        memset(&motor_disarmed, 0, sizeof(motor_disarmed));
        memset(&s_testPidProfile, 0, sizeof(s_testPidProfile));
        controlRateProfilesMutable(0)->throttle_limit_type = THROTTLE_LIMIT_TYPE_OFF;
        currentControlRateProfile = controlRateProfilesMutable(0);
        memset(&motorDev, 0, sizeof(motorDev));
        memset(&motorDevCfg, 0, sizeof(motorDevCfg));

        sensorsSet(SENSOR_ACC | SENSOR_GYRO);

        stateFlags = 0;
        flightModeFlags = 0;
        armingFlags = 0;
        for (int i = 0; i < 3; i++) {
            s_setpointRate[i] = 0.0f;
            s_gyroADCf[i] = 0.0f;
            gyro.gyroADCf[i] = 0.0f;
        }
        rcData[THROTTLE] = 1000.0f;
        rcCommand[THROTTLE] = 1000.0f;
        rxConfigMutable()->mincheck = 1050;
        rxConfigMutable()->maxcheck = 1900;

        // Quad-X mixer configuration for ARIES v3 (M1=PA1, M2=PA2, M3=PA3, M4=PA4)
        mixerRuntime.motorCount = 4;
        mixerRuntime.motorOutputLow = 1000.0f;
        mixerRuntime.motorOutputHigh = 2000.0f;
        mixerRuntime.disarmMotorOutput = 1000.0f;
        for (int i = 0; i < 4; i++) {
            motor_disarmed[i] = 1000.0f;
            mixerRuntime.currentMixer[i].throttle = 1.0f;
        }
        // Rear-R, Front-R, Rear-L, Front-L Quad X signs
        mixerRuntime.currentMixer[0].roll = -1.0f; mixerRuntime.currentMixer[0].pitch =  1.0f; mixerRuntime.currentMixer[0].yaw = -1.0f;
        mixerRuntime.currentMixer[1].roll = -1.0f; mixerRuntime.currentMixer[1].pitch = -1.0f; mixerRuntime.currentMixer[1].yaw =  1.0f;
        mixerRuntime.currentMixer[2].roll =  1.0f; mixerRuntime.currentMixer[2].pitch =  1.0f; mixerRuntime.currentMixer[2].yaw =  1.0f;
        mixerRuntime.currentMixer[3].roll =  1.0f; mixerRuntime.currentMixer[3].pitch = -1.0f; mixerRuntime.currentMixer[3].yaw = -1.0f;

        gyro.targetLooptime = 1000; // 1.0 kHz ARIES clamped loop BEFORE pidInit (sets dT=0.001s, freq=1000Hz)
        resetPidProfile(&s_testPidProfile);
        pidInitConfig(&s_testPidProfile);
        pidInit(&s_testPidProfile);
        pidStabilisationState(PID_STABILISATION_ON);
        motorDevCfg.motorPwmRate = 400U;
        ASSERT_TRUE(motorPwmDevInit(&motorDev, &motorDevCfg, 1000U));
        motorDev.vTable->enable();
        thejas32_host_mcycle_set(1000000ULL);
    }
};

// =============================================================================
// TEST 01 (TDD-RED): ariesPwmMotorWrite MUST Reject NaN / +/-Inf Without Corruption
// =============================================================================
TEST_F(AriesFcFlightSafetyTest, Test_PwmWrite_NaNAndInfinity_ClampedSafelyWithin1000To2000)
{
    // Arrange: Write valid idle (1500 us) on M1..M3, and IEEE-754 NAN on M0
    // Expected SAFE silicon behavior: NAN fails finite guard and clamps to 1000 us (100000 cycles).
    // Current buggy pwm_motor_aries.c:213-216 fails both `< 1000.0f` and `> 2000.0f` checks!
    motorDev.vTable->updateInit();
    motorDev.vTable->write(0U, NAN);
    motorDev.vTable->write(1U, 1500.0f);
    motorDev.vTable->write(2U, 1500.0f);
    motorDev.vTable->write(3U, 1500.0f);

    // Act: Push deadlines into Timer 1 LoadCount -> lowest deadline loads first!
    motorDev.vTable->updateComplete();

    // Assert (TDD-RED against current pwm_motor_aries.c!): LoadCount must sit within [100000..200000]
    EXPECT_GE(THEJAS32_TIMER(1).LoadCount, 100000UL)
        << "NAN motor command bypassed float clamp and corrupted Timer 1 LoadCount!";
    EXPECT_LE(THEJAS32_TIMER(1).LoadCount, 200000UL);

    // Also verify +INFINITY clamps to 2000 us and -INFINITY clamps to 1000 us
    THEJAS32_TIMER(1).Control = 0U;
    thejas32_host_mcycle_advance(3000ULL * CYCLES_PER_US);
    motorDev.vTable->updateInit();
    motorDev.vTable->write(0U, -INFINITY);
    motorDev.vTable->write(1U, INFINITY);
    motorDev.vTable->write(2U, 1600.0f);
    motorDev.vTable->write(3U, 1700.0f);
    motorDev.vTable->updateComplete();
    EXPECT_EQ(THEJAS32_TIMER(1).LoadCount, 100000UL);
}

// =============================================================================
// TEST 02: IMU Tilted 15 Deg + P-Controller + Quad-X Mixer -> Right Motors Rise
// =============================================================================
TEST_F(AriesFcFlightSafetyTest, Test_ImuToPidToMixerToPwm_RollTiltProducesDifferentialMotorPulses)
{
    // Arrange: Establish +15 deg right-wing-down roll quaternion in imu.c, enable ANGLE_MODE
    ENABLE_ARMING_FLAG(ARMED);
    enableFlightMode(ANGLE_MODE);
    pidStabilisationState(PID_STABILISATION_ON);
    const float halfRad = 15.0f * (3.14159265f / 180.0f) * 0.5f;
    q.w = cosf(halfRad);
    q.x = sinf(halfRad);
    q.y = 0.0f;
    q.z = 0.0f;
    imuComputeRotationMatrix();
    imuUpdateEulerAngles();
    EXPECT_GT(attitude.values.roll, 100); // > 10.0 deg

    rcData[THROTTLE] = 1350.0f;
    rcCommand[THROTTLE] = 0.35f;
    rcCommand[ROLL] = 0.0f;
    rcCommand[PITCH] = 0.0f;
    rcCommand[YAW] = 0.0f;

    // Run real pidController + real mixTable
    pidController(&s_testPidProfile, micros());
    mixTable(micros());

    // Assert: Right motors (M0, M1) and left motors (M2, M3) are differentially commanded & within ESC bounds
    EXPECT_NE(motor[0], motor[2]);
    for (int i = 0; i < 4; i++) {
        EXPECT_GE(motor[i], 1000.0f);
        EXPECT_LE(motor[i], 2000.0f);
        EXPECT_TRUE(std::isfinite(motor[i]));
        motorDev.vTable->write((uint8_t)i, motor[i]);
    }
    motorDev.vTable->updateComplete();
    EXPECT_GE(THEJAS32_TIMER(1).LoadCount, 100000UL);
    EXPECT_LE(THEJAS32_TIMER(1).LoadCount, 200000UL);
}

// =============================================================================
// TEST 03: I-Term Anti-Windup & pidSumLimit Saturation Guard On Ground
// =============================================================================
TEST_F(AriesFcFlightSafetyTest, Test_PidItermAntiWindupAndSumLimit_SaturationClampsWithin500)
{
    ENABLE_ARMING_FLAG(ARMED);
    s_gyroADCf[FD_ROLL] = -200.0f;
    gyro.gyroADCf[FD_ROLL] = -200.0f;
    s_setpointRate[FD_ROLL] = 200.0f;

    // Sustained huge rate error across 500 ticks @ 1 kHz
    for (int i = 0; i < 500; i++) {
        thejas32_host_mcycle_advance(1000ULL * CYCLES_PER_US);
        pidController(&s_testPidProfile, micros());
        EXPECT_TRUE(std::isfinite(pidData[FD_ROLL].Sum));
    }

    EXPECT_LE(fabsf(pidData[FD_ROLL].I), (float)s_testPidProfile.itermLimit + 1.0f);
    mixTable(micros());
    for (int i = 0; i < 4; i++) {
        EXPECT_GE(motor[i], 1000.0f);
        EXPECT_LE(motor[i], 2000.0f);
    }
}

// =============================================================================
// TEST 04: Disarm Override Forces 1000 us Idle Even With High Sticks & Full PID Sum
// =============================================================================
TEST_F(AriesFcFlightSafetyTest, Test_DisarmOverride_ForcesAllMotorsTo1000UsImmediately)
{
    DISABLE_ARMING_FLAG(ARMED);
    rcData[THROTTLE] = 1900.0f;
    rcCommand[THROTTLE] = 1900.0f;
    pidData[FD_ROLL].Sum = 400.0f;
    pidData[FD_PITCH].Sum = 300.0f;

    mixTable(micros());

    for (int i = 0; i < 4; i++) {
        EXPECT_FLOAT_EQ(motor[i], 1000.0f);
        motorDev.vTable->write((uint8_t)i, motor[i]);
    }
    motorDev.vTable->updateComplete();
    EXPECT_EQ(THEJAS32_TIMER(1).LoadCount, 100000UL);
}

// =============================================================================
// TEST 05: Mahony AHRS Integration Conservation (|q| == 1.0 Across 1000 Ticks)
// =============================================================================
TEST_F(AriesFcFlightSafetyTest, Test_MahonyAhrsQuaternionNorm_StaysUnitUnderSoftFloat1kHz)
{
    imuConfigure(0U, 0U);
    imuInit();
    q.w = 0.9961947f; q.x = 0.0871557f; q.y = 0.0f; q.z = 0.0f; // 10 deg roll
    for (int k = 0; k < 1000; k++) {
        imuMahonyAHRSupdate(0.001f, 0.0f, 0.0f, 0.0f, true, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.25f);
        const float normSq = q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z;
        EXPECT_NEAR(normSq, 1.0f, 1e-4f);
    }
    imuUpdateEulerAngles();
    EXPECT_LT(abs(attitude.values.roll), 90); // Kp=0.25 decays 10.0 deg (~100) monotonically toward 0 (~78)
    EXPECT_GT(abs(attitude.values.roll), 0);
}

// =============================================================================
// TEST 06: 400 Hz Timer 1 PWM Staggered Deadlines Complete In Strictly Ascending Order
// =============================================================================
TEST_F(AriesFcFlightSafetyTest, Test_400HzTimer1PwmDeadlines_SortedAndDroppedExactlyInOrder)
{
    motorDev.vTable->updateInit();
    motorDev.vTable->write(0U, 1800.0f);
    motorDev.vTable->write(1U, 1100.0f); // earliest!
    motorDev.vTable->write(2U, 1500.0f);
    motorDev.vTable->write(3U, 1300.0f);
    motorDev.vTable->updateComplete();

    // First LoadCount must equal minimum (1100 us = 110000 cycles)
    EXPECT_EQ(THEJAS32_TIMER(1).LoadCount, 110000UL);
    EXPECT_NE(THEJAS32_TIMER(1).Control & 0x01U, 0U);

    // Step through remaining 3 delta ISRs via handle_trap -> PA2(1100)->PA4(1300)->PA3(1500)->PA1(1800)
    THEJAS32_INTR_CTRL.INTR_STATUS = (1UL << THEJAS32_IRQ_TIMER1);
    handle_trap(0x8000000BUL, 0U, nullptr); // drops pin 2 (idx 1), arms delta 200us = 20000
    EXPECT_EQ(THEJAS32_TIMER(1).LoadCount, 20000UL);
    handle_trap(0x8000000BUL, 0U, nullptr); // drops pin 4 (idx 3), arms delta 200us = 20000
    EXPECT_EQ(THEJAS32_TIMER(1).LoadCount, 20000UL);
    handle_trap(0x8000000BUL, 0U, nullptr); // drops pin 3 (idx 2), arms delta 300us = 30000
    EXPECT_EQ(THEJAS32_TIMER(1).LoadCount, 30000UL);
    handle_trap(0x8000000BUL, 0U, nullptr); // drops final pin 1 (idx 0), stops Timer 1
    EXPECT_EQ(THEJAS32_TIMER(1).Control & 0x01U, 0U);
}

} // namespace
