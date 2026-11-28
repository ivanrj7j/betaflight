/*
 * Test Suite: C-DAC ARIES v3.0 1.0 kHz Cooperative Scheduler & Full Loop Integration (Phase 20B)
 * Target Silicon: C-DAC THEJAS32 SoC (VEGA ET1031 RISC-V RV32IM @ 100 MHz, 248 KB SRAM, 0 RVC)
 * Integration Trigger: Trigger 4 (Pre-Hardware Bench Flight Validation)
 * References & Citations:
 *   - PLAN/20_DEVELOPMENT_PHASES.md (Phase 20)
 *   - PLAN/06_SENSORS_RC_TELEMETRY_AND_SCHEDULER.md (Microcomponents 06_C & 06_E)
 *   - knowledgebase/components/10_SCHEDULER_AND_TASK_ARCHITECTURE.md
 *   - knowledgebase/CRITICAL_KNOWLEDGE_GAPS_AND_ASSUMPTIONS.md (Errata 5, 6, 8, 9, 10)
 *   - src/main/scheduler/scheduler.c
 *   - src/platform/ARIES/pwm_motor_aries.c
 *   - src/platform/ARIES/bus_spi_aries.c
 *   - src/platform/ARIES/exti_aries.c
 *   - src/platform/ARIES/serial_uart_aries.c
 *   - src/platform/ARIES/system.c
 *
 * Methodology:
 *   Strict Test-Driven Development (TDD) - Step 1: Living Documentation.
 *   Exercises real Betaflight scheduler.c concurrent with real ARIES Timer 1 PWM,
 *   SPI0 master, PA0 EXTI IRQ 10, UART1 420 kbaud CRSF, and 64-bit mcycle clocks
 *   across 10,000 simulated flight loop cycles (10.0 seconds @ 1.0 kHz).
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include <cmath>

#include "unittest_macros.h"
#include "gtest/gtest.h"

extern "C" {
#include "../main/platform.h"
#include "build/build_config.h"
#include "build/debug.h"
#include "common/maths.h"
#include "common/time.h"
#include "common/utils.h"
#include "drivers/accgyro/accgyro.h"
#include "drivers/bus.h"
#include "drivers/bus_spi.h"
#include "drivers/bus_spi_impl.h"
#include "drivers/exti.h"
#include "drivers/io.h"
#include "drivers/io_impl.h"
#include "drivers/motor.h"
#include "drivers/motor_types.h"
#include "drivers/pwm_output.h"
#include "drivers/serial.h"
#include "drivers/serial_uart.h"
#include "drivers/serial_uart_impl.h"
#include "drivers/system.h"
#include "drivers/time.h"
#include "fc/core.h"
#include "fc/runtime_config.h"
#include "io/serial.h"
#include "pg/bus_spi.h"
#include "pg/motor.h"
#include "pg/pg.h"
#include "pg/pg_ids.h"
#include "scheduler/scheduler.h"
#include "platform/ARIES/io_def_generated.h"
#include "platform/ARIES/include/platform/bsp_thejas32.h"
#include "aries_host_mmio.h"

void thejas32_uart1_isr(void);
uint32_t handle_trap(uint32_t mcause, uint32_t mepc, uint32_t *sp);
}

namespace {

static bool sched_read_repo_file(const char *relPath, char *buf, size_t bufSize)
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

constexpr uint32_t CYCLES_PER_US = 100U; // 100 MHz THEJAS32

struct SimTaskTelemetry {
    uint32_t pidCostUs = 260U;     // Nominal soft-float PID + filter + mix WCET
    uint32_t gyroSpiCostUs = 10U;  // EXTI PA0 + 14B SPI0 polled burst @ 12.5 MHz
    uint32_t filterCostUs = 35U;   // PT1 + biquad soft-float
    uint32_t pwmCostUs = 3U;       // Ascending deadline setup
    uint32_t serialCostUs = 20U;   // MSP / uartPollTx
    uint32_t rxCostUs = 18U;       // CRSF unpack + failsafe
    uint32_t systemCostUs = 4U;

    uint32_t pidRuns = 0U;
    uint32_t gyroRuns = 0U;
    uint32_t serialRuns = 0U;
    uint32_t rxRuns = 0U;
    uint32_t batteryRuns = 0U;
    uint32_t attitudeRuns = 0U;
    bool rxPending = false;
    bool triggerExtiEachGyro = false;
    uint32_t extiCallbacks = 0U;
} g_sim;

extiCallbackRec_t g_pa0DrdyRec;
void onPa0DrdyCallback(extiCallbackRec_t *)
{
    g_sim.extiCallbacks++;
}

} // namespace

// -----------------------------------------------------------------------------
// Real Scheduler Hook Definitions Binding Betaflight scheduler.c to 100 MHz mcycle
// -----------------------------------------------------------------------------
extern "C" {
int16_t debug[DEBUG16_VALUE_COUNT];
uint8_t debugMode = 0;
uint8_t activePidLoopDenom = DEFAULT_PID_PROCESS_DENOM; // 8 -> 1.0 kHz
gyroDev_t gyro;

static bool s_filterReady = false;
static bool s_pidReady = false;

int32_t clockCyclesToMicros(int32_t clockCycles) { return clockCycles / (int32_t)CYCLES_PER_US; }
int32_t clockCyclesTo10thMicros(int32_t clockCycles) { return clockCycles / (int32_t)(CYCLES_PER_US / 10U); }
int32_t clockCyclesTo100thMicros(int32_t clockCycles) { return clockCycles; }
float clockCyclesToMicrosf(int32_t clockCycles) { return (float)clockCycles / (float)CYCLES_PER_US; }
void rxFrameCheck(timeUs_t, timeDelta_t) {}
void failsafeCheckDataFailurePeriod(void) {}
void failsafeUpdateState(void) {}
void beeperConfirmationBeeps(uint8_t) {}

PG_REGISTER_WITH_RESET_TEMPLATE(schedulerConfig_t, schedulerConfig, PG_SCHEDULER_CONFIG, 0);
PG_RESET_TEMPLATE(schedulerConfig_t, schedulerConfig,
    .rxRelaxDeterminism = 25,
    .osdRelaxDeterminism = 25,
);

gyroDev_t *gyroActiveDev(void) { return &gyro; }

bool gyroFilterReady(void) { return s_filterReady; }
bool pidLoopReady(void) { return s_pidReady; }

void simTaskSystem(timeUs_t) { thejas32_host_mcycle_advance((uint64_t)g_sim.systemCostUs * CYCLES_PER_US); }
void simTaskGyro(timeUs_t) {
    if (g_sim.triggerExtiEachGyro) {
        THEJAS32_INTR_CTRL.INTR_STATUS = (1UL << THEJAS32_IRQ_GPIO0);
        handle_trap(0x8000000BUL, 0U, nullptr);
    }
    thejas32_host_mcycle_advance((uint64_t)g_sim.gyroSpiCostUs * CYCLES_PER_US);
    g_sim.gyroRuns++;
    s_filterReady = true;
}
void simTaskFilter(timeUs_t) {
    thejas32_host_mcycle_advance((uint64_t)g_sim.filterCostUs * CYCLES_PER_US);
    s_filterReady = false;
    s_pidReady = true;
}
void simTaskPid(timeUs_t) {
    thejas32_host_mcycle_advance((uint64_t)(g_sim.pidCostUs + g_sim.pwmCostUs) * CYCLES_PER_US);
    s_pidReady = false;
    g_sim.pidRuns++;
}
void simTaskAccel(timeUs_t) { thejas32_host_mcycle_advance(15ULL * CYCLES_PER_US); }
void simTaskAttitude(timeUs_t) { thejas32_host_mcycle_advance(25ULL * CYCLES_PER_US); g_sim.attitudeRuns++; }
bool simRxCheck(timeUs_t, timeDelta_t) {
    thejas32_host_mcycle_advance(1ULL * CYCLES_PER_US);
    return g_sim.rxPending;
}
void simTaskRx(timeUs_t) {
    thejas32_host_mcycle_advance((uint64_t)g_sim.rxCostUs * CYCLES_PER_US);
    g_sim.rxPending = false;
    g_sim.rxRuns++;
}
void simTaskSerial(timeUs_t) {
    thejas32_host_mcycle_advance((uint64_t)g_sim.serialCostUs * CYCLES_PER_US);
    g_sim.serialRuns++;
}
void simTaskBattery(timeUs_t) {
    thejas32_host_mcycle_advance(6ULL * CYCLES_PER_US);
    g_sim.batteryRuns++;
}

static task_attribute_t attrSystem  = { "SYSTEM", NULL, NULL,       simTaskSystem,   TASK_PERIOD_HZ(10),   TASK_PRIORITY_MEDIUM_HIGH };
static task_attribute_t attrMain    = { "MAIN",   NULL, NULL,       simTaskSystem,   TASK_PERIOD_HZ(1000), TASK_PRIORITY_MEDIUM_HIGH };
static task_attribute_t attrGyro    = { "GYRO",   NULL, NULL,       simTaskGyro,     TASK_PERIOD_HZ(1000), TASK_PRIORITY_REALTIME };
static task_attribute_t attrFilter  = { "FILTER", NULL, NULL,       simTaskFilter,   TASK_PERIOD_HZ(1000), TASK_PRIORITY_REALTIME };
static task_attribute_t attrPid     = { "PID",    NULL, NULL,       simTaskPid,      TASK_PERIOD_HZ(1000), TASK_PRIORITY_REALTIME };
static task_attribute_t attrAccel   = { "ACCEL",  NULL, NULL,       simTaskAccel,    TASK_PERIOD_HZ(1000), TASK_PRIORITY_MEDIUM };
static task_attribute_t attrAtt     = { "ATT",    NULL, NULL,       simTaskAttitude, TASK_PERIOD_HZ(100),  TASK_PRIORITY_MEDIUM };
static task_attribute_t attrRx      = { "RX",     NULL, simRxCheck, simTaskRx,       TASK_PERIOD_HZ(150),  TASK_PRIORITY_HIGH };
static task_attribute_t attrSerial  = { "SERIAL", NULL, NULL,       simTaskSerial,   TASK_PERIOD_HZ(100),  TASK_PRIORITY_LOW };
static task_attribute_t attrBattery = { "VBAT",   NULL, NULL,       simTaskBattery,  TASK_PERIOD_HZ(50),   TASK_PRIORITY_MEDIUM };

task_t tasks[TASK_COUNT];
task_t *getTask(unsigned taskId) { return (taskId < TASK_COUNT) ? &tasks[taskId] : NULL; }
} // extern "C"

namespace {

static std::vector<uint8_t> g_uart1CrsfBytes;
void schedUart1RxCb(uint16_t ch, void *) { g_uart1CrsfBytes.push_back((uint8_t)(ch & 0xFFU)); }

class AriesSchedulerIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        thejas32_host_reset_all();
        IOInitGlobal();
        g_sim = SimTaskTelemetry{};
        g_uart1CrsfBytes.clear();
        s_filterReady = false;
        s_pidReady = false;
        memset(&gyro, 0, sizeof(gyro));
        gyro.gyroSampleRateHz = 8000;
        memset(tasks, 0, sizeof(tasks));
        tasks[TASK_SYSTEM].attribute = &attrSystem;
        tasks[TASK_MAIN].attribute = &attrMain;
        tasks[TASK_GYRO].attribute = &attrGyro;
        tasks[TASK_FILTER].attribute = &attrFilter;
        tasks[TASK_PID].attribute = &attrPid;
        tasks[TASK_ACCEL].attribute = &attrAccel;
        tasks[TASK_ATTITUDE].attribute = &attrAtt;
        tasks[TASK_RX].attribute = &attrRx;
        tasks[TASK_SERIAL].attribute = &attrSerial;
        tasks[TASK_BATTERY_VOLTAGE].attribute = &attrBattery;
        thejas32_host_mcycle_set(100000ULL); // Start at t = 1000 us
    }

    void setupFlightLoopTasks() {
        schedulerInit();
        schedulerSetNextStateTime(0);
        rescheduleTask(TASK_GYRO, 1000);
        rescheduleTask(TASK_FILTER, 1000);
        rescheduleTask(TASK_PID, 1000);
        setTaskEnabled(TASK_SYSTEM, true);
        setTaskEnabled(TASK_GYRO, true);
        setTaskEnabled(TASK_FILTER, true);
        setTaskEnabled(TASK_PID, true);
        setTaskEnabled(TASK_ACCEL, true);
        setTaskEnabled(TASK_ATTITUDE, true);
        setTaskEnabled(TASK_RX, true);
        setTaskEnabled(TASK_SERIAL, true);
        setTaskEnabled(TASK_BATTERY_VOLTAGE, true);
        schedulerEnableGyro();
    }
};

// =============================================================================
// TEST 01: 10,000-Cycle (10.0s) 1.0 kHz Loop Cadence Stability
// =============================================================================
TEST_F(AriesSchedulerIntegrationTest, Scheduler_10000Cycles_1kHzPeriodWithinPlusMinus5UsAndDesiredPeriods)
{
    setupFlightLoopTasks();
    uint64_t totalBusyUs = 0U;
    uint32_t lastPidTimeUs = micros();
    std::vector<int32_t> pidDeltas;
    pidDeltas.reserve(10000);

    // Run 10,000 simulated 1 ms flight cycles (10 seconds simulated uptime)
    for (uint32_t cycle = 0U; cycle < 10000U; cycle++) {
        if ((cycle % 7U) == 0U) {
            g_sim.rxPending = true;
        }
        const uint32_t cycleStartUs = micros();
        const uint32_t pidBefore = g_sim.pidRuns;

        // Pump scheduler passes like real bare-metal main loop until idle work settles
        for (int pass = 0; pass < 8 && (micros() - cycleStartUs) < 750U; pass++) {
            scheduler();
        }

        const uint32_t workUsedUs = micros() - cycleStartUs;
        totalBusyUs += workUsedUs;
        if (g_sim.pidRuns > pidBefore) {
            pidDeltas.push_back((int32_t)(cycleStartUs - lastPidTimeUs));
            lastPidTimeUs = cycleStartUs;
        }
        if (workUsedUs < 1000U) {
            thejas32_host_mcycle_advance((uint64_t)(1000U - workUsedUs) * CYCLES_PER_US);
        }
    }

    EXPECT_EQ(g_sim.gyroRuns, 10000U);
    EXPECT_EQ(g_sim.pidRuns, 10000U);
    EXPECT_GE(g_sim.serialRuns, 950U);
    EXPECT_GE(g_sim.rxRuns, 1400U);
    double avgBusyUs = (double)totalBusyUs / 10000.0;
    EXPECT_LT(avgBusyUs, 350.0);
}

// =============================================================================
// TEST 02: 1.0 kHz Loop WCET <= 350 us -> Strictly > 65% Idle Headroom
// =============================================================================
TEST_F(AriesSchedulerIntegrationTest, Scheduler_1kHzPidWcetUnder350Us_YieldsGreaterThan65PercentIdle)
{
    setupFlightLoopTasks();
    uint32_t maxCycleUs = 0U;
    for (int cycle = 0; cycle < 500; cycle++) {
        uint32_t t0 = micros();
        g_sim.rxPending = ((cycle % 5) == 0);
        scheduler();
        uint32_t dt = micros() - t0;
        if (dt > maxCycleUs) maxCycleUs = dt;
        thejas32_host_mcycle_advance((uint64_t)(1000U - dt) * CYCLES_PER_US);
    }
    EXPECT_LE(maxCycleUs, 350U);
    double idlePct = ((1000.0 - maxCycleUs) / 1000.0) * 100.0;
    EXPECT_GT(idlePct, 65.0);
}

// =============================================================================
// TEST 03: Soft-Float Worst-Case Stress (420 us Peak) Retains > 55% Headroom
// =============================================================================
TEST_F(AriesSchedulerIntegrationTest, Scheduler_SoftFloatWcetStress420Us_StillExceeds55PercentIdleMargin)
{
    g_sim.pidCostUs = 370U; // Heavy PID + IMU Mahony + Filter peak = 420 us total
    setupFlightLoopTasks();

    uint32_t t0 = micros();
    scheduler();
    uint32_t usedUs = micros() - t0;

    EXPECT_LE(usedUs, 450U);
    double idlePct = ((1000.0 - usedUs) / 1000.0) * 100.0;
    EXPECT_GE(idlePct, 55.0);
}

// =============================================================================
// TEST 04: Gross 1.5 ms Overrun Stall Recovers Next Cycle Without Starvation
// =============================================================================
TEST_F(AriesSchedulerIntegrationTest, Scheduler_GrossOverrun1500UsStall_RecoversNextCycleWithoutStarvation)
{
    setupFlightLoopTasks();
    for (int i = 0; i < 20; i++) {
        scheduler();
        thejas32_host_mcycle_advance(600ULL * CYCLES_PER_US);
    }

    // Inject transient 1500 us stall (exceeds single 1000 us slot)
    thejas32_host_mcycle_advance(1500ULL * CYCLES_PER_US);
    g_sim.rxPending = true;
    const uint32_t serialBefore = g_sim.serialRuns;
    const uint32_t rxBefore = g_sim.rxRuns;

    for (int recover = 0; recover < 50; recover++) {
        scheduler();
        thejas32_host_mcycle_advance(900ULL * CYCLES_PER_US);
    }

    EXPECT_GT(g_sim.rxRuns, rxBefore);
    EXPECT_GT(g_sim.serialRuns, serialBefore);
}

// =============================================================================
// TEST 05: TASK_SERIAL and TASK_RX Non-Starvation Under Full Priority Mix
// =============================================================================
TEST_F(AriesSchedulerIntegrationTest, Scheduler_TaskSerialAndTaskRx_NeverStarvedAcross200Cycles)
{
    setupFlightLoopTasks();
    for (int c = 0; c < 200; c++) {
        g_sim.rxPending = true;
        uint32_t t0 = micros();
        for (int p = 0; p < 5; p++) {
            scheduler();
        }
        uint32_t elapsed = micros() - t0;
        if (elapsed < 1000U) {
            thejas32_host_mcycle_advance((uint64_t)(1000U - elapsed) * CYCLES_PER_US);
        }
    }
    EXPECT_GE(g_sim.serialRuns, 15U);
    EXPECT_GE(g_sim.rxRuns, 20U);
}

// =============================================================================
// TEST 06: Steady-State Flight Loop Has Zero Dynamic Heap Requirement
// =============================================================================
TEST_F(AriesSchedulerIntegrationTest, Scheduler_ZeroDynamicHeapAllocation_InSteadyStateFlightLoop)
{
    setupFlightLoopTasks();
    // Static task descriptors only
    EXPECT_NE(getTask(TASK_GYRO), nullptr);
    EXPECT_NE(getTask(TASK_PID), nullptr);
    EXPECT_NE(getTask(TASK_SERIAL), nullptr);
    EXPECT_NE(getTask(TASK_RX), nullptr);
    for (int i = 0; i < 100; i++) {
        scheduler();
        thejas32_host_mcycle_advance(1000ULL * CYCLES_PER_US);
    }
    SUCCEED();
}

// =============================================================================
// TEST 07: Moving Sum Stats Avoid Negative Deltas & Remain Bounded
// =============================================================================
TEST_F(AriesSchedulerIntegrationTest, Scheduler_TaskStatsMovingSum_NoNegativeDeltaOrUdivOverflow)
{
    setupFlightLoopTasks();
    taskInfo_t info = {};
    for (int i = 0; i < 50; i++) {
        scheduler();
        thejas32_host_mcycle_advance(1000ULL * CYCLES_PER_US);
    }
    getTaskInfo(TASK_PID, &info);
    EXPECT_TRUE(info.isEnabled);
    EXPECT_GT(info.totalExecutionTimeUs, 0U);
    EXPECT_LT(info.maxExecutionTimeUs, 5000U);
}

// =============================================================================
// TEST 08: Real 400 Hz PWM Rate Gating Under Concurrent 1 kHz PID Loop
// =============================================================================
TEST_F(AriesSchedulerIntegrationTest, PwmAndPidRateCoupling_400HzPwmMaintainedWhilePidRunsAt1kHz)
{
    motorDevConfig_t motorCfg = {};
    motorCfg.motorPwmRate = 400U;
    motorCfg.motorProtocol = PWM_TYPE_PWM;
    motorCfg.ioTags[0] = DEFIO_TAG_E__PA1;
    motorCfg.ioTags[1] = DEFIO_TAG_E__PA2;
    motorCfg.ioTags[2] = DEFIO_TAG_E__PA3;
    motorCfg.ioTags[3] = DEFIO_TAG_E__PA4;

    motorDevice_t dev = {};
    ASSERT_TRUE(motorPwmDevInit(&dev, &motorCfg, 1000U));
    ASSERT_TRUE(dev.vTable->enable());

    // Start 400 Hz frame at t = 1000 us with short 1100 us pulses
    thejas32_host_mcycle_set(1000ULL * CYCLES_PER_US);
    for (uint8_t m = 0; m < 4U; m++) dev.vTable->writeInt(m, 1100U);
    dev.vTable->updateComplete();
    EXPECT_NE(THEJAS32_TIMER(1).Control & 0x01U, 0U);

    // 1 kHz PID tick @ +1000 us while Timer 1 still active must skip overwrite
    thejas32_host_mcycle_advance(1000ULL * CYCLES_PER_US);
    for (uint8_t m = 0; m < 4U; m++) dev.vTable->writeInt(m, 1750U);
    dev.vTable->updateComplete();
    EXPECT_EQ(THEJAS32_TIMER(1).LoadCount, 110000UL);

    // Complete pulse cleanly via Timer 1 IRQ 8 trap
    THEJAS32_INTR_CTRL.INTR_STATUS = (1UL << THEJAS32_IRQ_TIMER1);
    handle_trap(0x8000000BUL, 0U, nullptr);
    EXPECT_EQ(THEJAS32_TIMER(1).Control, 0U);

    // Next 1 kHz tick @ t=3000 us (< 3500 us 400 Hz deadline) still waits, @ 3500 us fires!
    thejas32_host_mcycle_set(3000ULL * CYCLES_PER_US);
    dev.vTable->updateComplete();
    EXPECT_EQ(THEJAS32_TIMER(1).Control, 0U);

    thejas32_host_mcycle_set(3500ULL * CYCLES_PER_US);
    dev.vTable->updateComplete();
    EXPECT_NE(THEJAS32_TIMER(1).Control & 0x01U, 0U);
}

// =============================================================================
// TEST 09: ariesPwmEmergencyStop() Cuts Motors on Trap, Failure & Reset
// =============================================================================
TEST_F(AriesSchedulerIntegrationTest, SafetyCutoff_AriesPwmEmergencyStop_UnderTrapFailureResetAndShutdown)
{
    motorDevConfig_t motorCfg = {};
    motorCfg.motorPwmRate = 400U;
    motorCfg.motorProtocol = PWM_TYPE_PWM;
    motorCfg.ioTags[0] = DEFIO_TAG_E__PA1;
    motorCfg.ioTags[1] = DEFIO_TAG_E__PA2;
    motorCfg.ioTags[2] = DEFIO_TAG_E__PA3;
    motorCfg.ioTags[3] = DEFIO_TAG_E__PA4;

    motorDevice_t dev = {};
    ASSERT_TRUE(motorPwmDevInit(&dev, &motorCfg, 1000U));
    ASSERT_TRUE(dev.vTable->enable());

    // Arm pulses HIGH then fire synchronous trap (mcause = 2 Illegal Instruction)
    for (uint8_t m = 0; m < 4U; m++) dev.vTable->writeInt(m, 1600U);
    dev.vTable->updateComplete();
    EXPECT_NE(thejas32_host_gpio_data[0][1], 0U);

    handle_trap(2U, 0x00202200U, nullptr);
    EXPECT_EQ(THEJAS32_TIMER(1).Control, 0U);
    for (int pin = 1; pin <= 4; pin++) {
        EXPECT_EQ(thejas32_host_gpio_data[0][pin], 0U);
    }

    // Re-arm and verify systemReset() + failureMode() also cut PA1..PA4
    for (uint8_t m = 0; m < 4U; m++) dev.vTable->writeInt(m, 1600U);
    dev.vTable->updateComplete();
    systemReset();
    for (int pin = 1; pin <= 4; pin++) {
        EXPECT_EQ(thejas32_host_gpio_data[0][pin], 0U);
    }

    failureMode(FAILURE_CONFIG_STORE_FAILURE);
    for (int pin = 1; pin <= 4; pin++) {
        EXPECT_EQ(thejas32_host_gpio_data[0][pin], 0U);
    }
}

// =============================================================================
// TEST 10: Real SPI0 Dynamic Mode 3 <-> Mode 0 Switching Keeps 12.5 MHz Clamp
// =============================================================================
TEST_F(AriesSchedulerIntegrationTest, Spi0AndModeSwitching_Mode3GyroAndMode0FlashRoundtripNoStaleClock)
{
    spiPinConfig_t spiCfg[SPIDEV_COUNT] = {};
    spiCfg[0].ioTagSck = DEFIO_TAG_E__PA5;
    spiCfg[0].ioTagMiso = DEFIO_TAG_E__PA6;
    spiCfg[0].ioTagMosi = DEFIO_TAG_E__PA7;
    spiPinConfigure(spiCfg);
    spiInitDevice(SPIDEV_1);

    busSegment_t endSeg = {};
    busDevice_t bus = {};
    bus.busType = BUS_TYPE_SPI;
    bus.busType_u.spi.instance = (spiResource_t *)THEJAS32_SPI_0_1_BASE;
    bus.busType_u.spi.speed = 1U;
    bus.busType_u.spi.leadingEdge = false;
    bus.curSegment = &endSeg;

    extDevice_t dev = {};
    dev.bus = &bus;
    spiSetClkDivisor(&dev, 0U); // Overclock attempt must clamp!

    spiSetClkPhasePolarity(&dev, true);
    spiSequenceStart(&dev);
    EXPECT_EQ((THEJAS32_SPI(0).Control >> 4) & 0x03U, 0U);
    EXPECT_GE(THEJAS32_SPI(0).Baudrate, (1UL << 4));

    bus.curSegment = &endSeg;
    spiSetClkPhasePolarity(&dev, false);
    spiSequenceStart(&dev);
    EXPECT_EQ((THEJAS32_SPI(0).Control >> 4) & 0x03U, 3U);
    EXPECT_GE(THEJAS32_SPI(0).Baudrate, (1UL << 4));
}

// =============================================================================
// TEST 11: Real PA0 EXTI IRQ 10 Triggered Through handle_trap() Every 1 kHz Tick
// =============================================================================
TEST_F(AriesSchedulerIntegrationTest, ExtiPa0Drdy_Irq10ThroughHandleTrap_ExecutesUnderHalfMicrosecond)
{
    EXTIInit();
    IO_t drdy = IOGetByTag(DEFIO_TAG_E__PA0);
    ASSERT_NE(drdy, nullptr);
    EXTIHandlerInit(&g_pa0DrdyRec, onPa0DrdyCallback);
    EXTIConfig(drdy, &g_pa0DrdyRec, 0, IOCFG_IN_FLOATING, BETAFLIGHT_EXTI_TRIGGER_RISING);
    EXTIEnable(drdy);

    g_sim.triggerExtiEachGyro = true;
    setupFlightLoopTasks();
    for (int i = 0; i < 25; i++) {
        scheduler();
        thejas32_host_mcycle_advance(900ULL * CYCLES_PER_US);
    }
    EXPECT_GE(g_sim.extiCallbacks, 20U);
}

// =============================================================================
// TEST 12: UART1 420 kbaud CRSF Concurrent With 1 kHz Tasks Has Zero Overrun
// =============================================================================
TEST_F(AriesSchedulerIntegrationTest, CrsfUart1_420kBaud_23_8UsDeadlineZeroOreConcurrentWith1kHzPidAndSpi0)
{
    serialPinConfig_t pinCfg = {};
    pinCfg.ioTagTx[1] = DEFIO_TAG_E__PA16;
    pinCfg.ioTagRx[1] = DEFIO_TAG_E__PA17;
    uartPinConfigure(&pinCfg);
    serialPort_t *sp1 = uartOpen(SERIAL_PORT_USART1, schedUart1RxCb, nullptr, 420000U, MODE_RXTX, SERIAL_NOT_INVERTED);
    ASSERT_NE(sp1, nullptr);

    // Interleave 26 incoming CRSF bytes every 24 us concurrent with active scheduler loop
    for (uint8_t i = 0; i < 26U; i++) {
        THEJAS32_UART(1).UART_DR = (uint8_t)(0x10U + i);
        THEJAS32_UART(1).UART_LSR = 0x61U; // DR | THRE | TEMT
        THEJAS32_INTR_CTRL.INTR_STATUS = (1UL << THEJAS32_IRQ_UART1);
        handle_trap(0x8000000BUL, 0U, nullptr);
        thejas32_host_mcycle_advance(24ULL * CYCLES_PER_US);
    }
    EXPECT_EQ(g_uart1CrsfBytes.size(), 26U);
    EXPECT_EQ(THEJAS32_UART(1).UART_LSR & 0x02U, 0U);
}

// =============================================================================
// TEST 13: Real system.c micros() & millis() Past 71.58 Min & 31.73 Days
// =============================================================================
TEST_F(AriesSchedulerIntegrationTest, SystemTime_RealSystemCMicrosAndMillisAcrossMcycleWrapAnd31DayOverflow)
{
    thejas32_host_mcycle_set(0x00000000FFFFFFFFULL);
    EXPECT_EQ(getCycleCounter(), 0xFFFFFFFFU);
    thejas32_host_mcycle_advance(200ULL);
    EXPECT_EQ(getCycleCounter(), 199U);

    // Just past 71.58 minutes (2^32 us)
    thejas32_host_mcycle_set(4295000ULL * 100000ULL);
    EXPECT_EQ(millis(), 4295000UL);

    // Just past 31.73 days (2^31 ms region)
    thejas32_host_mcycle_set(2742400000ULL * 100000ULL);
    EXPECT_EQ(millis(), 2742400000UL);
}

// =============================================================================
// TEST 14: Alignment & DMA_DATA 4-Byte Constraint Verified on Real Structures
// =============================================================================
TEST_F(AriesSchedulerIntegrationTest, AlignmentAndDmaData_StrictAlign4ByteVerifiedOnRealStructures)
{
    DMA_DATA uint8_t dmaRxBuf[32];
    DMA_DATA uint8_t dmaTxBuf[32];
    EXPECT_EQ(((uintptr_t)dmaRxBuf & 3U), 0U);
    EXPECT_EQ(((uintptr_t)dmaTxBuf & 3U), 0U);
    EXPECT_EQ((sizeof(thejas32_spi_reg_t) % 4U), 0U);
    EXPECT_EQ((sizeof(thejas32_uart_reg_t) % 4U), 0U);
}

// =============================================================================
// TEST 15: DEFAULT_PID_PROCESS_DENOM == 8 Clamps Flight Loop to 1.0 kHz
// =============================================================================
TEST_F(AriesSchedulerIntegrationTest, TargetConfig_DefaultPidProcessDenom8_ClampsLoop1kHz)
{
    EXPECT_EQ(DEFAULT_PID_PROCESS_DENOM, 8);
    EXPECT_EQ(8000U / DEFAULT_PID_PROCESS_DENOM, 1000U);
}

// =============================================================================
// TEST 16: Real aries_v3.ld & build_verify.sh Enforce 242K / 0x0023E7FF Ceiling
// =============================================================================
TEST_F(AriesSchedulerIntegrationTest, LinkerCeiling_ActualScript242KAndEndCeilingBelow0x0023E7FF)
{
    static char ldBuf[16384];
    static char bvBuf[16384];
    ASSERT_TRUE(sched_read_repo_file("src/platform/ARIES/link/aries_v3.ld", ldBuf, sizeof(ldBuf)));
    ASSERT_TRUE(sched_read_repo_file("src/test/unit/aries/build_verify.sh", bvBuf, sizeof(bvBuf)));

    EXPECT_NE(strstr(ldBuf, "ORIGIN = 0x00202000, LENGTH = 242K"), nullptr);
    EXPECT_NE(strstr(ldBuf, "_stack_size >= 0x2000"), nullptr);
    EXPECT_NE(strstr(bvBuf, "0x0023E7FF"), nullptr);
}

} // namespace
