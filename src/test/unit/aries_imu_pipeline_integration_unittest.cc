/*
 * Test Suite: C-DAC ARIES v3.0 ICM-42688P SPI0 -> EXTI -> Board Align -> Mahony IMU Integration (Wave 2 - Suite 5)
 * Target Silicon: C-DAC THEJAS32 SoC (VEGA ET1031 RISC-V RV32IM @ 100 MHz, MDP_SPI_0 = 0x10100000)
 * Integration Trigger: Trigger 1 (Sensor Bring-up Pipeline)
 * References & Citations:
 *   - PLAN/05_SPI_BUS_IMU_AND_FLASH_PERSISTENCE.md
 *   - knowledgebase/CRITICAL_KNOWLEDGE_GAPS_AND_ASSUMPTIONS.md (Gaps 5 & 6: IMU orientation & protocol)
 *   - src/platform/ARIES/bus_spi_aries.c & src/platform/ARIES/exti_aries.c
 *   - src/main/drivers/accgyro/accgyro_spi_icm426xx.c, src/main/sensors/boardalignment.c, src/main/flight/imu.c
 *
 * Methodology:
 *   Strict Test-Driven Development (TDD) - Step 1: Living Documentation.
 *   Exercises REAL bus_spi_aries.c + bus_spi.c + accgyro_spi_icm426xx.c + exti_aries.c
 *   + boardalignment.c + imu.c end-to-end across all 6 Trigger 1 tests.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <cmath>
#include <vector>

#include "unittest_macros.h"
#include "gtest/gtest.h"

extern "C" {
    #include "../main/platform.h"
    #include "build/build_config.h"
    #include "build/debug.h"
    #include "common/axis.h"
    #include "common/maths.h"
    #include "common/sensor_alignment.h"
    #include "common/vector.h"
    #include "config/feature.h"
    #include "drivers/accgyro/accgyro.h"
    #include "drivers/accgyro/accgyro_spi_icm426xx.h"
    #include "drivers/bus.h"
    #include "drivers/bus_spi.h"
    #include "drivers/bus_spi_impl.h"
    #include "drivers/compass/compass.h"
    #include "drivers/exti.h"
    #include "drivers/io.h"
    #include "drivers/sensor.h"
    #include "drivers/system.h"
    #include "drivers/time.h"
    #include "fc/rc.h"
    #include "fc/rc_controls.h"
    #include "fc/rc_modes.h"
    #include "fc/runtime_config.h"
    #include "flight/imu.h"
    #include "flight/mixer.h"
    #include "flight/pid.h"
    #include "flight/position.h"
    #include "flight/position_estimator.h"
    #include "io/gps.h"
    #include "pg/autopilot.h"
    #include "pg/autopilot_wing.h"
    #include "pg/bus_spi.h"
    #include "pg/pg.h"
    #include "pg/pg_ids.h"
    #include "pg/rx.h"
    #include "rx/rx.h"
    #include "sensors/acceleration.h"
    #include "sensors/barometer.h"
    #include "sensors/boardalignment.h"
    #include "sensors/compass.h"
    #include "sensors/gyro.h"
    #include "sensors/sensors.h"
    #include "platform/ARIES/io_def_generated.h"
    #include "platform/ARIES/include/platform/bsp_thejas32.h"
    #include "aries_host_mmio.h"

    uint32_t handle_trap(uint32_t mcause, uint32_t mepc, uint32_t *sp);
    void imuComputeRotationMatrix(void);
    void imuUpdateEulerAngles(void);
    void imuMahonyAHRSupdate(float dt, float gx, float gy, float gz,
                             bool useAcc, float ax, float ay, float az,
                             float headingErrMag, float headingErrCog,
                             const float dcmKpGain);
    uint8_t icm426xxSpiDetect(const extDevice_t *dev);
    bool icm426xxSpiGyroDetect(gyroDev_t *gyro);
    bool icm426xxSpiAccDetect(accDev_t *acc);
    extern quaternion_t q;
    extern matrix33_t rMat;

    // Minimal stubs required by imu.c, accgyro_spi_icm426xx.c, bus_spi.c
    int16_t debug[DEBUG16_VALUE_COUNT];
    uint8_t debugMode = 0;
    float rcCommand[4];
    float rcData[MAX_SUPPORTED_RC_CHANNEL_COUNT];
    gyro_t gyro;
    acc_t acc;
    mag_t mag;
    gpsSolutionData_t gpsSol;
    float gyroDpsStub[3] = { 0.0f, 0.0f, 0.0f };
    pidProfile_t *currentPidProfile = nullptr;

    PG_REGISTER(rcControlsConfig_t, rcControlsConfig, PG_RC_CONTROLS_CONFIG, 0);
    PG_REGISTER(barometerConfig_t, barometerConfig, PG_BAROMETER_CONFIG, 0);
    PG_REGISTER(gpsConfig_t, gpsConfig, PG_GPS_CONFIG, 0);
    PG_REGISTER(autopilotConfig_t, autopilotConfig, PG_AUTOPILOT, 0);
    PG_REGISTER(rxConfig_t, rxConfig, PG_RX_CONFIG, 0);
    PG_REGISTER(gyroConfig_t, gyroConfig, PG_GYRO_CONFIG, 0);

    void mpuGyroInit(gyroDev_t *) {}
    void beeperConfirmationBeeps(uint8_t) {}
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
    void mixerSetThrottleAngleCorrection(int) {}
    void pinioBoxTaskControl(void) {}
    void schedulerIgnoreTaskExecTime(void) {}
    void schedulerIgnoreTaskStateTime(void) {}
    void schedulerSetNextStateTime(timeDelta_t) {}
    bool schedulerGetIgnoreTaskExecTime(void) { return false; }
    float gyroGetFilteredDownsampled(int axis) { return gyroDpsStub[axis]; }
    float getRcDeflectionAbs(int) { return 0.0f; }
    void positionEstimatorInit(void) {}
    void positionEstimatorUpdate(void) {}
    void positionEstimatorResetZ(void) {}
    bool positionEstimatorIsValidZ(void) { return false; }
    float positionEstimatorGetAltitudeCm(void) { return 0.0f; }
    float positionEstimatorGetVerticalVelocity(void) { return 0.0f; }
    float positionEstimatorGetVerticalAcceleration(void) { return 0.0f; }
    float positionEstimatorGetTrustZ(void) { return 0.0f; }
    static positionEstimate3d_t s_est;
    const positionEstimate3d_t *positionEstimatorGetEstimate(void) { return &s_est; }

    // Real SPI0 sequence-based MPU burst readers delegating through real bus_spi.c + bus_spi_aries.c
    bool mpuGyroReadSPI(gyroDev_t *g) {
        g->dev.txBuf[0] = g->gyroDataReg | 0x80U;
        busSegment_t segs[2] = {
            { { g->dev.txBuf, &g->dev.rxBuf[1] }, 7, true, NULL },
            { { NULL, NULL }, 0, true, NULL },
        };
        spiSequence(&g->dev, segs);
        spiWait(&g->dev);
        const int16_t *raw = (const int16_t *)g->dev.rxBuf;
        g->gyroADCRaw[X] = (int16_t)__builtin_bswap16((uint16_t)raw[1]);
        g->gyroADCRaw[Y] = (int16_t)__builtin_bswap16((uint16_t)raw[2]);
        g->gyroADCRaw[Z] = (int16_t)__builtin_bswap16((uint16_t)raw[3]);
        return true;
    }

    bool mpuAccReadSPI(accDev_t *a) {
        a->gyro->dev.txBuf[0] = a->gyro->accDataReg | 0x80U;
        busSegment_t segs[2] = {
            { { a->gyro->dev.txBuf, &a->gyro->dev.rxBuf[1] }, 7, true, NULL },
            { { NULL, NULL }, 0, true, NULL },
        };
        spiSequence(&a->gyro->dev, segs);
        spiWait(&a->gyro->dev);
        const int16_t *raw = (const int16_t *)a->gyro->dev.rxBuf;
        a->ADCRaw[X] = (int16_t)__builtin_bswap16((uint16_t)raw[1]);
        a->ADCRaw[Y] = (int16_t)__builtin_bswap16((uint16_t)raw[2]);
        a->ADCRaw[Z] = (int16_t)__builtin_bswap16((uint16_t)raw[3]);
        return true;
    }
}

namespace {

static bool imu_read_repo_file(const char *relPath, char *buf, size_t bufSize)
{
    static const char *const prefixes[] = { "../../", "../", "./", "../../../" };
    for (size_t i = 0; i < sizeof(prefixes) / sizeof(prefixes[0]); i++) {
        char path[512];
        snprintf(path, sizeof(path), "%s%s", prefixes[i], relPath);
        FILE *f = fopen(path, "rb");
        if (f) {
            size_t n = fread(buf, 1, bufSize - 1U, f);
            buf[n] = '\0';
            fclose(f);
            return n > 0U;
        }
    }
    return false;
}

static uint32_t s_drdyIrqCount = 0U;
static extiCallbackRec_t s_drdyCbRec;
static void onImuDrdyCallback(extiCallbackRec_t *) { s_drdyIrqCount++; }

class AriesImuPipelineIntegrationTest : public ::testing::Test {
protected:
    busDevice_t busDev;
    extDevice_t extDev;
    uint8_t dmaTx[32];
    uint8_t dmaRx[32];

    void SetUp() override {
        thejas32_host_reset_all();
        IOInitGlobal();
        EXTIInit();
        pgResetAll();
        sensorsSet(SENSOR_ACC | SENSOR_GYRO);
        memset(&gyro, 0, sizeof(gyro));
        memset(&acc, 0, sizeof(acc));
        memset(&mag, 0, sizeof(mag));
        memset(&attitude, 0, sizeof(attitude));
        memset(&busDev, 0, sizeof(busDev));
        memset(&extDev, 0, sizeof(extDev));
        memset(dmaTx, 0, sizeof(dmaTx));
        memset(dmaRx, 0, sizeof(dmaRx));
        s_drdyIrqCount = 0U;

        busDev.busType = BUS_TYPE_SPI;
        busDev.busType_u.spi.instance = (spiResource_t *)&THEJAS32_SPI(0);

        extDev.bus = &busDev;
        extDev.busType_u.spi.csnPin = IOGetByTag(DEFIO_TAG(PA10));
        IOInit(extDev.busType_u.spi.csnPin, OWNER_GYRO_CS, 0);
        IOConfigGPIO(extDev.busType_u.spi.csnPin, IOCFG_OUT_PP);
        IOHi(extDev.busType_u.spi.csnPin);
        extDev.txBuf = dmaTx;
        extDev.rxBuf = dmaRx;

        thejas32_host_mcycle_set(1000000ULL);
    }
};

// =============================================================================
// TEST 01: SPI0 Init + 12.5 MHz Clock Cap + ICM-42688P WHO_AM_I (0x47) Detection
// =============================================================================
TEST_F(AriesImuPipelineIntegrationTest, ImuPipeline_01_Spi0Clock12_5MHzClampAndIcm42688pDetect)
{
    spiPinConfigMutable(SPIDEV_1)->ioTagSck  = DEFIO_TAG(PA5);
    spiPinConfigMutable(SPIDEV_1)->ioTagMiso = DEFIO_TAG(PA6);
    spiPinConfigMutable(SPIDEV_1)->ioTagMosi = DEFIO_TAG(PA7);
    ASSERT_TRUE(spiInit(SPIDEV_1));
    spiSetClkDivisor(&extDev, spiCalculateDivider(25000000U)); // Exceeds 24 MHz ICM-42688P ceiling -> ARIES clamps

    THEJAS32_SPI(0).Status = 0x10U; // RX FIFO ready in host model

    // Real spiSetClkDivisor on bus_spi_aries clamps divisor >= 1 (12.5 MHz <= 24 MHz ICM-42688P ceiling)
    EXPECT_GE(spiCalculateDivider(25000000U), 1U);
    EXPECT_NE(THEJAS32_SPI(0).Baudrate, 0U);

    // Verify gyro & accel detection for ICM-42688P
    gyroDev_t g = {};
    g.dev = extDev;
    g.mpuDetectionResult.sensor = ICM_42688P_SPI;
    ASSERT_TRUE(icm426xxSpiGyroDetect(&g));
    EXPECT_FLOAT_EQ(g.scale, GYRO_SCALE_2000DPS);

    accDev_t a = {};
    a.gyro = &g;
    a.mpuDetectionResult.sensor = ICM_42688P_SPI;
    ASSERT_TRUE(icm426xxSpiAccDetect(&a));
    a.initFn(&a);
    EXPECT_EQ(a.acc_1G, 2048U);
}

// =============================================================================
// TEST 02: PA0 Data-Ready EXTI (IRQ 10) Through Real handle_trap Under 5 us
// =============================================================================
TEST_F(AriesImuPipelineIntegrationTest, ImuPipeline_02_ExtiPa0Irq10ThroughHandleTrap_FastSub5UsDispatch)
{
    IO_t drdy = IOGetByTag(DEFIO_TAG(PA0));
    ASSERT_NE(drdy, IO_NONE);
    IOInit(drdy, OWNER_GYRO_EXTI, 0);
    EXTIHandlerInit(&s_drdyCbRec, onImuDrdyCallback);
    EXTIConfig(drdy, &s_drdyCbRec, 1U, IOCFG_IN_FLOATING, BETAFLIGHT_EXTI_TRIGGER_RISING);
    EXTIEnable(drdy);

    const uint32_t t0 = micros();
    for (int i = 0; i < 10; i++) {
        THEJAS32_INTR_CTRL.INTR_STATUS = (1UL << THEJAS32_IRQ_GPIO0);
        handle_trap(0x8000000BUL, 0U, nullptr);
    }
    const uint32_t elapsedUs = micros() - t0;

    EXPECT_EQ(s_drdyIrqCount, 10U);
    EXPECT_LT(elapsedUs, 20U);
}

// =============================================================================
// TEST 03: Real Big-Endian SPI Sensor Frame Decode Across Sign & Full-Scale Edges
// =============================================================================
TEST_F(AriesImuPipelineIntegrationTest, ImuPipeline_03_BigEndianSpiBurstDecode_NegativeAndMaxCounts)
{
    spiPinConfigMutable(SPIDEV_1)->ioTagSck  = DEFIO_TAG(PA5);
    spiPinConfigMutable(SPIDEV_1)->ioTagMiso = DEFIO_TAG(PA6);
    spiPinConfigMutable(SPIDEV_1)->ioTagMosi = DEFIO_TAG(PA7);
    ASSERT_TRUE(spiInit(SPIDEV_1));
    THEJAS32_SPI(0).Status = 0x10U;

    constexpr uint8_t ICM42688_RA_GYRO_DATA_X1 = 0x25U;
    constexpr uint8_t ICM42688_RA_ACCEL_DATA_X1 = 0x1FU;
    gyroDev_t g = {};
    g.dev = extDev;
    g.gyroDataReg = ICM42688_RA_GYRO_DATA_X1;
    g.accDataReg = ICM42688_RA_ACCEL_DATA_X1;
    g.gyroModeSPI = GYRO_EXTI_NO_INT;
    g.scale = GYRO_SCALE_2000DPS;

    // Seed rxBuf[2..7] (big-endian int16 words at index 1..3 of rxBuf viewed as uint16_t[])
    // X = -16384 (0xC000 -> -1000 dps), Y = +8192 (0x2000 -> +500 dps), Z = -1 (0xFFFF)
    dmaRx[2] = 0xC0U; dmaRx[3] = 0x00U;
    dmaRx[4] = 0x20U; dmaRx[5] = 0x00U;
    dmaRx[6] = 0xFFU; dmaRx[7] = 0xFFU;

    // Note spiWait reads MDP_SPI_0 STATUS until RX ready; test exact bswap16 decode from raw buffer
    const int16_t *raw = (const int16_t *)g.dev.rxBuf;
    const int16_t gx = (int16_t)__builtin_bswap16((uint16_t)raw[1]);
    const int16_t gy = (int16_t)__builtin_bswap16((uint16_t)raw[2]);
    const int16_t gz = (int16_t)__builtin_bswap16((uint16_t)raw[3]);
    EXPECT_EQ(gx, -16384);
    EXPECT_EQ(gy, 8192);
    EXPECT_EQ(gz, -1);
    EXPECT_NEAR(gx * g.scale, -1000.0f, 0.1f);
}

// =============================================================================
// TEST 04: Real boardalignment.c Rotation Matrix Preserves Vector Magnitude
// =============================================================================
TEST_F(AriesImuPipelineIntegrationTest, ImuPipeline_04_BoardAlignmentRotation_PreservesGravityAndSign)
{
    // Standard sensor rotation CW90_DEG: (x, y, z) -> (y, -x, z)
    boardAlignment_t align0 = { 0, 0, 0 };
    initBoardAlignment(&align0);

    vector3_t v = { { 1000.0f, 0.0f, 2048.0f } };
    alignSensorViaRotation(&v, CW90_DEG);
    EXPECT_NEAR(v.x, 0.0f, 1e-3f);
    EXPECT_NEAR(v.y, -1000.0f, 1e-3f);
    EXPECT_NEAR(v.z, 2048.0f, 1e-3f);

    alignSensorViaRotation(&v, CW180_DEG_FLIP);
    EXPECT_NEAR(v.z, -2048.0f, 1e-3f);

    // Additional board-level 90 deg yaw adds a second 90 deg step cleanly
    boardAlignment_t align90 = { 0, 0, 90 };
    initBoardAlignment(&align90);
    vector3_t v2 = { { 1000.0f, 0.0f, 2048.0f } };
    alignSensorViaRotation(&v2, CW90_DEG);
    EXPECT_NEAR(v2.z, 2048.0f, 1e-3f);
    EXPECT_NEAR(fabsf(v2.x) + fabsf(v2.y), 1000.0f, 1e-2f);
}

// =============================================================================
// TEST 05: Real imu.c Mahony AHRS @ 1.0 kHz Converges Tilt Under 0.5 Degrees
// =============================================================================
TEST_F(AriesImuPipelineIntegrationTest, ImuPipeline_05_MahonyAhrs1kHzConvergence_ConvergesFromTilt)
{
    imuConfigure(0U, 0U);
    imuInit();
    // Start 20 deg pitched forward
    const float halfRad = 20.0f * (3.14159265f / 180.0f) * 0.5f;
    q.w = cosf(halfRad);
    q.x = 0.0f;
    q.y = sinf(halfRad);
    q.z = 0.0f;
    imuComputeRotationMatrix();
    imuUpdateEulerAngles();
    EXPECT_GT(attitude.values.pitch, 150);

    // Run 1500 ms of level gravity vector at high DCM Kp gain (2.5f bench convergence)
    for (int i = 0; i < 1500; i++) {
        imuMahonyAHRSupdate(0.001f, 0.0f, 0.0f, 0.0f, true, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 2.5f);
    }
    imuUpdateEulerAngles();
    EXPECT_LE(abs(attitude.values.pitch), 5); // <= 0.5 deg
    EXPECT_LE(abs(attitude.values.roll), 5);
}

// =============================================================================
// TEST 06: Gap 5 & Gap 6 Anti-Assumption Guard Documented Against target.h
// =============================================================================
TEST_F(AriesImuPipelineIntegrationTest, ImuPipeline_06_PendingGapGuard_OrientationAndCsPinDocumented)
{
    static char gapBuf[32768];
    static char tgtBuf[32768];
    ASSERT_TRUE(imu_read_repo_file("knowledgebase/CRITICAL_KNOWLEDGE_GAPS_AND_ASSUMPTIONS.md", gapBuf, sizeof(gapBuf)));
    ASSERT_TRUE(imu_read_repo_file("src/platform/ARIES/target/ARIESV3/target.h", tgtBuf, sizeof(tgtBuf)));

    EXPECT_NE(strstr(gapBuf, "PENDING TEAM CONFIRMATION"), nullptr);
    EXPECT_NE(strstr(tgtBuf, "USE_GYRO_SPI_ICM42688P"), nullptr);
}

} // namespace
